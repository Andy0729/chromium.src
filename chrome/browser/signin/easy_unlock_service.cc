// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/easy_unlock_service.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/prefs/pref_registry_simple.h"
#include "base/prefs/pref_service.h"
#include "base/prefs/scoped_user_pref_update.h"
#include "base/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/easy_unlock_app_manager.h"
#include "chrome/browser/signin/easy_unlock_auth_attempt.h"
#include "chrome/browser/signin/easy_unlock_service_factory.h"
#include "chrome/browser/signin/easy_unlock_service_observer.h"
#include "chrome/browser/signin/screenlock_bridge.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/proximity_auth/switches.h"
#include "components/user_manager/user.h"
#include "device/bluetooth/bluetooth_adapter.h"
#include "device/bluetooth/bluetooth_adapter_factory.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/easy_unlock/easy_unlock_key_manager.h"
#include "chrome/browser/chromeos/login/easy_unlock/easy_unlock_tpm_key_manager.h"
#include "chrome/browser/chromeos/login/easy_unlock/easy_unlock_tpm_key_manager_factory.h"
#include "chrome/browser/chromeos/login/session/user_session_manager.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/power_manager_client.h"
#include "components/user_manager/user_manager.h"
#endif

namespace {

PrefService* GetLocalState() {
  return g_browser_process ? g_browser_process->local_state() : NULL;
}

}  // namespace

EasyUnlockService::UserSettings::UserSettings()
    : require_close_proximity(false) {
}

EasyUnlockService::UserSettings::~UserSettings() {
}

// static
EasyUnlockService* EasyUnlockService::Get(Profile* profile) {
  return EasyUnlockServiceFactory::GetForProfile(profile);
}

// static
EasyUnlockService* EasyUnlockService::GetForUser(
    const user_manager::User& user) {
#if defined(OS_CHROMEOS)
  Profile* profile = chromeos::ProfileHelper::Get()->GetProfileByUser(&user);
  if (!profile)
    return NULL;
  return EasyUnlockService::Get(profile);
#else
  return NULL;
#endif
}

// static
bool EasyUnlockService::IsSignInEnabled() {
  return !base::CommandLine::ForCurrentProcess()->HasSwitch(
      proximity_auth::switches::kDisableEasyUnlock);
}

class EasyUnlockService::BluetoothDetector
    : public device::BluetoothAdapter::Observer {
 public:
  explicit BluetoothDetector(EasyUnlockService* service)
      : service_(service),
        weak_ptr_factory_(this) {
  }

  ~BluetoothDetector() override {
    if (adapter_.get())
      adapter_->RemoveObserver(this);
  }

  void Initialize() {
    if (!device::BluetoothAdapterFactory::IsBluetoothAdapterAvailable())
      return;

    device::BluetoothAdapterFactory::GetAdapter(
        base::Bind(&BluetoothDetector::OnAdapterInitialized,
                   weak_ptr_factory_.GetWeakPtr()));
  }

  bool IsPresent() const { return adapter_.get() && adapter_->IsPresent(); }

  // device::BluetoothAdapter::Observer:
  void AdapterPresentChanged(device::BluetoothAdapter* adapter,
                             bool present) override {
    service_->OnBluetoothAdapterPresentChanged();
  }

 private:
  void OnAdapterInitialized(scoped_refptr<device::BluetoothAdapter> adapter) {
    adapter_ = adapter;
    adapter_->AddObserver(this);
    service_->OnBluetoothAdapterPresentChanged();
  }

  // Owner of this class and should out-live this class.
  EasyUnlockService* service_;
  scoped_refptr<device::BluetoothAdapter> adapter_;
  base::WeakPtrFactory<BluetoothDetector> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(BluetoothDetector);
};

#if defined(OS_CHROMEOS)
class EasyUnlockService::PowerMonitor
    : public chromeos::PowerManagerClient::Observer {
 public:
  explicit PowerMonitor(EasyUnlockService* service)
      : service_(service),
        waking_up_(false),
        weak_ptr_factory_(this) {
    chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->
        AddObserver(this);
  }

  ~PowerMonitor() override {
    chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->
        RemoveObserver(this);
  }

  // Called when the remote device has been authenticated to record the time
  // delta from waking up. No time will be recorded if the start-up time has
  // already been recorded or if the system never went to sleep previously.
  void RecordStartUpTime() {
    if (wake_up_time_.is_null())
      return;
    UMA_HISTOGRAM_MEDIUM_TIMES(
        "EasyUnlock.StartupTimeFromSuspend",
        base::Time::Now() - wake_up_time_);
    wake_up_time_ = base::Time();
  }

  bool waking_up() const { return waking_up_; }

 private:
  // chromeos::PowerManagerClient::Observer:
  void SuspendImminent() override { service_->PrepareForSuspend(); }

  void SuspendDone(const base::TimeDelta& sleep_duration) override {
    waking_up_ = true;
    wake_up_time_ = base::Time::Now();
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&PowerMonitor::ResetWakingUp,
                   weak_ptr_factory_.GetWeakPtr()),
        base::TimeDelta::FromSeconds(5));
    service_->UpdateAppState();
    // Note that |this| may get deleted after |UpdateAppState| is called.
  }

  void ResetWakingUp() {
    waking_up_ = false;
    service_->UpdateAppState();
  }

  EasyUnlockService* service_;
  bool waking_up_;
  base::Time wake_up_time_;
  base::WeakPtrFactory<PowerMonitor> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(PowerMonitor);
};
#endif

EasyUnlockService::EasyUnlockService(Profile* profile)
    : profile_(profile),
      bluetooth_detector_(new BluetoothDetector(this)),
      shut_down_(false),
      tpm_key_checked_(false),
      weak_ptr_factory_(this) {
}

EasyUnlockService::~EasyUnlockService() {
}

// static
void EasyUnlockService::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(
      prefs::kEasyUnlockAllowed,
      true,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterBooleanPref(
      prefs::kEasyUnlockEnabled,
      false,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterDictionaryPref(
      prefs::kEasyUnlockPairing,
      new base::DictionaryValue(),
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
  registry->RegisterBooleanPref(
      prefs::kEasyUnlockProximityRequired,
      false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
}

// static
void EasyUnlockService::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(prefs::kEasyUnlockLocalStateUserPrefs);
  registry->RegisterDictionaryPref(prefs::kEasyUnlockHardlockState);
#if defined(OS_CHROMEOS)
  EasyUnlockTpmKeyManager::RegisterLocalStatePrefs(registry);
#endif
}

// static
void EasyUnlockService::ResetLocalStateForUser(const std::string& user_id) {
  DCHECK(!user_id.empty());

  PrefService* local_state = GetLocalState();
  if (!local_state)
    return;

  DictionaryPrefUpdate update(local_state, prefs::kEasyUnlockHardlockState);
  update->RemoveWithoutPathExpansion(user_id, NULL);

#if defined(OS_CHROMEOS)
  EasyUnlockTpmKeyManager::ResetLocalStateForUser(user_id);
#endif
}

// static
EasyUnlockService::UserSettings EasyUnlockService::GetUserSettings(
    const std::string& user_id) {
  DCHECK(!user_id.empty());
  UserSettings user_settings;

  PrefService* local_state = GetLocalState();
  if (!local_state)
    return user_settings;

  const base::DictionaryValue* all_user_prefs_dict =
      local_state->GetDictionary(prefs::kEasyUnlockLocalStateUserPrefs);
  if (!all_user_prefs_dict)
    return user_settings;

  const base::DictionaryValue* user_prefs_dict;
  if (!all_user_prefs_dict->GetDictionaryWithoutPathExpansion(user_id,
                                                              &user_prefs_dict))
    return user_settings;

  user_prefs_dict->GetBooleanWithoutPathExpansion(
      prefs::kEasyUnlockProximityRequired,
      &user_settings.require_close_proximity);

  return user_settings;
}

void EasyUnlockService::Initialize(
    scoped_ptr<EasyUnlockAppManager> app_manager) {
  app_manager_ = app_manager.Pass();
  app_manager_->EnsureReady(
      base::Bind(&EasyUnlockService::InitializeOnAppManagerReady,
                 weak_ptr_factory_.GetWeakPtr()));
}

bool EasyUnlockService::IsAllowed() const {
  if (shut_down_)
    return false;

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          proximity_auth::switches::kDisableEasyUnlock)) {
    return false;
  }

  if (!IsAllowedInternal())
    return false;

#if defined(OS_CHROMEOS)
  if (!bluetooth_detector_->IsPresent())
    return false;

  return true;
#else
  // TODO(xiyuan): Revisit when non-chromeos platforms are supported.
  return false;
#endif
}

bool EasyUnlockService::IsEnabled() const {
  // The feature is enabled iff there are any paired devices.
  const base::ListValue* devices = GetRemoteDevices();
  return devices && !devices->empty();
}

void EasyUnlockService::OpenSetupApp() {
  app_manager_->LaunchSetup();
}

void EasyUnlockService::SetHardlockState(
    EasyUnlockScreenlockStateHandler::HardlockState state) {
  const std::string user_id = GetUserEmail();
  if (user_id.empty())
    return;

  SetHardlockStateForUser(user_id, state);
}

EasyUnlockScreenlockStateHandler::HardlockState
EasyUnlockService::GetHardlockState() const {
  EasyUnlockScreenlockStateHandler::HardlockState state;
  if (GetPersistedHardlockState(&state))
    return state;

  return EasyUnlockScreenlockStateHandler::NO_HARDLOCK;
}

bool EasyUnlockService::GetPersistedHardlockState(
    EasyUnlockScreenlockStateHandler::HardlockState* state) const {
  std::string user_id = GetUserEmail();
  if (user_id.empty())
    return false;

  PrefService* local_state = GetLocalState();
  if (!local_state)
    return false;

  const base::DictionaryValue* dict =
      local_state->GetDictionary(prefs::kEasyUnlockHardlockState);
  int state_int;
  if (dict && dict->GetIntegerWithoutPathExpansion(user_id, &state_int)) {
    *state =
        static_cast<EasyUnlockScreenlockStateHandler::HardlockState>(state_int);
    return true;
  }

  return false;
}

void EasyUnlockService::ShowInitialUserState() {
  if (!GetScreenlockStateHandler())
    return;

  EasyUnlockScreenlockStateHandler::HardlockState state;
  bool has_persisted_state = GetPersistedHardlockState(&state);
  if (!has_persisted_state)
    return;

  if (state == EasyUnlockScreenlockStateHandler::NO_HARDLOCK) {
    // Show connecting icon early when there is a persisted non hardlock state.
    UpdateScreenlockState(
        EasyUnlockScreenlockStateHandler::STATE_BLUETOOTH_CONNECTING);
  } else {
    screenlock_state_handler_->MaybeShowHardlockUI();
  }
}

EasyUnlockScreenlockStateHandler*
    EasyUnlockService::GetScreenlockStateHandler() {
  if (!IsAllowed())
    return NULL;
  if (!screenlock_state_handler_) {
    screenlock_state_handler_.reset(new EasyUnlockScreenlockStateHandler(
        GetUserEmail(),
        GetHardlockState(),
        ScreenlockBridge::Get()));
  }
  return screenlock_state_handler_.get();
}

bool EasyUnlockService::UpdateScreenlockState(
    EasyUnlockScreenlockStateHandler::State state) {
  EasyUnlockScreenlockStateHandler* handler = GetScreenlockStateHandler();
  if (!handler)
    return false;

  handler->ChangeState(state);

  if (state == EasyUnlockScreenlockStateHandler::STATE_AUTHENTICATED) {
#if defined(OS_CHROMEOS)
    if (power_monitor_)
      power_monitor_->RecordStartUpTime();
#endif
  } else if (auth_attempt_.get()) {
    // Clean up existing auth attempt if we can no longer authenticate the
    // remote device.
    auth_attempt_.reset();

    if (!handler->InStateValidOnRemoteAuthFailure())
      HandleAuthFailure(GetUserEmail());
  }
  return true;
}

void EasyUnlockService::AttemptAuth(const std::string& user_id) {
  CHECK_EQ(GetUserEmail(), user_id);

  auth_attempt_.reset(new EasyUnlockAuthAttempt(
      app_manager_.get(), user_id, GetType() == TYPE_REGULAR
                                       ? EasyUnlockAuthAttempt::TYPE_UNLOCK
                                       : EasyUnlockAuthAttempt::TYPE_SIGNIN));
  if (!auth_attempt_->Start())
    auth_attempt_.reset();
}

void EasyUnlockService::FinalizeUnlock(bool success) {
  if (!auth_attempt_.get())
    return;

  this->OnWillFinalizeUnlock(success);
  auth_attempt_->FinalizeUnlock(GetUserEmail(), success);
  auth_attempt_.reset();
  // TODO(isherman): If observing screen unlock events, is there a race
  // condition in terms of reading the service's state vs. the app setting the
  // state?

  // Make sure that the lock screen is updated on failure.
  if (!success) {
    RecordEasyUnlockScreenUnlockEvent(EASY_UNLOCK_FAILURE);
    HandleAuthFailure(GetUserEmail());
  }
}

void EasyUnlockService::FinalizeSignin(const std::string& key) {
  if (!auth_attempt_.get())
    return;
  std::string wrapped_secret = GetWrappedSecret();
  if (!wrapped_secret.empty())
    auth_attempt_->FinalizeSignin(GetUserEmail(), wrapped_secret, key);
  auth_attempt_.reset();

  // Processing empty key is equivalent to auth cancellation. In this case the
  // signin request will not actually be processed by login stack, so the lock
  // screen state should be set from here.
  if (key.empty())
    HandleAuthFailure(GetUserEmail());
}

void EasyUnlockService::HandleAuthFailure(const std::string& user_id) {
  if (user_id != GetUserEmail())
    return;

  if (!screenlock_state_handler_.get())
    return;

  screenlock_state_handler_->SetHardlockState(
      EasyUnlockScreenlockStateHandler::LOGIN_FAILED);
}

void EasyUnlockService::CheckCryptohomeKeysAndMaybeHardlock() {
#if defined(OS_CHROMEOS)
  std::string user_id = GetUserEmail();
  if (user_id.empty())
    return;

  const base::ListValue* device_list = GetRemoteDevices();
  std::set<std::string> paired_devices;
  if (device_list) {
    chromeos::EasyUnlockDeviceKeyDataList parsed_paired;
    chromeos::EasyUnlockKeyManager::RemoteDeviceListToDeviceDataList(
        *device_list, &parsed_paired);
    for (const auto& device_key_data : parsed_paired)
      paired_devices.insert(device_key_data.psk);
  }
  if (paired_devices.empty()) {
    SetHardlockState(EasyUnlockScreenlockStateHandler::NO_PAIRING);
    return;
  }

  // No need to compare if a change is already recorded.
  if (GetHardlockState() == EasyUnlockScreenlockStateHandler::PAIRING_CHANGED ||
      GetHardlockState() == EasyUnlockScreenlockStateHandler::PAIRING_ADDED) {
    return;
  }

  chromeos::EasyUnlockKeyManager* key_manager =
      chromeos::UserSessionManager::GetInstance()->GetEasyUnlockKeyManager();
  DCHECK(key_manager);

  key_manager->GetDeviceDataList(
      chromeos::UserContext(user_id),
      base::Bind(&EasyUnlockService::OnCryptohomeKeysFetchedForChecking,
                 weak_ptr_factory_.GetWeakPtr(),
                 user_id,
                 paired_devices));
#endif
}

void EasyUnlockService::SetTrialRun() {
  DCHECK_EQ(GetType(), TYPE_REGULAR);

  EasyUnlockScreenlockStateHandler* handler = GetScreenlockStateHandler();
  if (handler)
    handler->SetTrialRun();
}

void EasyUnlockService::RecordClickOnLockIcon() {
  if (screenlock_state_handler_)
    screenlock_state_handler_->RecordClickOnLockIcon();
}

void EasyUnlockService::AddObserver(EasyUnlockServiceObserver* observer) {
  observers_.AddObserver(observer);
}

void EasyUnlockService::RemoveObserver(EasyUnlockServiceObserver* observer) {
  observers_.RemoveObserver(observer);
}

void  EasyUnlockService::Shutdown() {
  if (shut_down_)
    return;
  shut_down_ = true;

  ShutdownInternal();

  ResetScreenlockState();
  bluetooth_detector_.reset();
#if defined(OS_CHROMEOS)
  power_monitor_.reset();
#endif

  weak_ptr_factory_.InvalidateWeakPtrs();
}

void EasyUnlockService::ReloadAppAndLockScreen() {
  // Make sure lock screen state set by the extension gets reset.
  ResetScreenlockState();
  app_manager_->ReloadApp();
  NotifyUserUpdated();
}

void EasyUnlockService::UpdateAppState() {
  if (IsAllowed()) {
    EnsureTpmKeyPresentIfNeeded();
    app_manager_->LoadApp();
    NotifyUserUpdated();

#if defined(OS_CHROMEOS)
    if (!power_monitor_)
      power_monitor_.reset(new PowerMonitor(this));
#endif
  } else {
    bool bluetooth_waking_up = false;
#if defined(OS_CHROMEOS)
    // If the service is not allowed due to bluetooth not being detected just
    // after system suspend is done, give bluetooth more time to be detected
    // before disabling the app (and resetting screenlock state).
    bluetooth_waking_up =
        power_monitor_.get() && power_monitor_->waking_up() &&
        !bluetooth_detector_->IsPresent();
#endif

    if (!bluetooth_waking_up) {
      app_manager_->DisableAppIfLoaded();
      ResetScreenlockState();
#if defined(OS_CHROMEOS)
      power_monitor_.reset();
#endif
    }
  }
}

void EasyUnlockService::DisableAppWithoutResettingScreenlockState() {
  app_manager_->DisableAppIfLoaded();
}

void EasyUnlockService::NotifyUserUpdated() {
  std::string user_id = GetUserEmail();
  if (user_id.empty())
    return;

  // Notify the easy unlock app that the user info changed.
  bool logged_in = GetType() == TYPE_REGULAR;
  bool data_ready = logged_in || GetRemoteDevices() != NULL;
  app_manager_->SendUserUpdatedEvent(user_id, logged_in, data_ready);
}

void EasyUnlockService::NotifyTurnOffOperationStatusChanged() {
  FOR_EACH_OBSERVER(
      EasyUnlockServiceObserver, observers_, OnTurnOffOperationStatusChanged());
}

void EasyUnlockService::ResetScreenlockState() {
  screenlock_state_handler_.reset();
  auth_attempt_.reset();
}

void EasyUnlockService::SetScreenlockHardlockedState(
    EasyUnlockScreenlockStateHandler::HardlockState state) {
  if (screenlock_state_handler_)
    screenlock_state_handler_->SetHardlockState(state);
  if (state != EasyUnlockScreenlockStateHandler::NO_HARDLOCK)
    auth_attempt_.reset();
}

void EasyUnlockService::InitializeOnAppManagerReady() {
  CHECK(app_manager_.get());

  InitializeInternal();

#if defined(OS_CHROMEOS)
  // Only start Bluetooth detection for ChromeOS since the feature is
  // only offered on ChromeOS. Enabling this on non-ChromeOS platforms
  // previously introduced a performance regression: http://crbug.com/404482
  // Make sure not to reintroduce a performance regression if re-enabling on
  // additional platforms.
  // TODO(xiyuan): Revisit when non-chromeos platforms are supported.
  bluetooth_detector_->Initialize();
#endif  // defined(OS_CHROMEOS)
}

void EasyUnlockService::OnBluetoothAdapterPresentChanged() {
  UpdateAppState();
}

void EasyUnlockService::SetHardlockStateForUser(
      const std::string& user_id,
      EasyUnlockScreenlockStateHandler::HardlockState state) {
  DCHECK(!user_id.empty());

  PrefService* local_state = GetLocalState();
  if (!local_state)
    return;

  DictionaryPrefUpdate update(local_state, prefs::kEasyUnlockHardlockState);
  update->SetIntegerWithoutPathExpansion(user_id, static_cast<int>(state));

  if (GetUserEmail() == user_id)
    SetScreenlockHardlockedState(state);
}

EasyUnlockAuthEvent EasyUnlockService::GetPasswordAuthEvent() const {
  DCHECK(IsEnabled());

  if (GetHardlockState() != EasyUnlockScreenlockStateHandler::NO_HARDLOCK) {
    switch (GetHardlockState()) {
      case EasyUnlockScreenlockStateHandler::NO_HARDLOCK:
        NOTREACHED();
        return EASY_UNLOCK_AUTH_EVENT_COUNT;
      case EasyUnlockScreenlockStateHandler::NO_PAIRING:
        return PASSWORD_ENTRY_NO_PAIRING;
      case EasyUnlockScreenlockStateHandler::USER_HARDLOCK:
        return PASSWORD_ENTRY_USER_HARDLOCK;
      case EasyUnlockScreenlockStateHandler::PAIRING_CHANGED:
        return PASSWORD_ENTRY_PAIRING_CHANGED;
      case EasyUnlockScreenlockStateHandler::LOGIN_FAILED:
        return PASSWORD_ENTRY_LOGIN_FAILED;
      case EasyUnlockScreenlockStateHandler::PAIRING_ADDED:
        return PASSWORD_ENTRY_PAIRING_ADDED;
    }
  } else if (!screenlock_state_handler()) {
    return PASSWORD_ENTRY_NO_SCREENLOCK_STATE_HANDLER;
  } else {
    switch (screenlock_state_handler()->state()) {
      case EasyUnlockScreenlockStateHandler::STATE_INACTIVE:
        return PASSWORD_ENTRY_SERVICE_NOT_ACTIVE;
      case EasyUnlockScreenlockStateHandler::STATE_NO_BLUETOOTH:
        return PASSWORD_ENTRY_NO_BLUETOOTH;
      case EasyUnlockScreenlockStateHandler::STATE_BLUETOOTH_CONNECTING:
        return PASSWORD_ENTRY_BLUETOOTH_CONNECTING;
      case EasyUnlockScreenlockStateHandler::STATE_NO_PHONE:
        return PASSWORD_ENTRY_NO_PHONE;
      case EasyUnlockScreenlockStateHandler::STATE_PHONE_NOT_AUTHENTICATED:
        return PASSWORD_ENTRY_PHONE_NOT_AUTHENTICATED;
      case EasyUnlockScreenlockStateHandler::STATE_PHONE_LOCKED:
        return PASSWORD_ENTRY_PHONE_LOCKED;
      case EasyUnlockScreenlockStateHandler::STATE_PHONE_UNLOCKABLE:
        return PASSWORD_ENTRY_PHONE_NOT_LOCKABLE;
      case EasyUnlockScreenlockStateHandler::STATE_PHONE_UNSUPPORTED:
        return PASSWORD_ENTRY_PHONE_UNSUPPORTED;
      case EasyUnlockScreenlockStateHandler::STATE_RSSI_TOO_LOW:
        return PASSWORD_ENTRY_RSSI_TOO_LOW;
      case EasyUnlockScreenlockStateHandler::STATE_TX_POWER_TOO_HIGH:
        return PASSWORD_ENTRY_TX_POWER_TOO_HIGH;
      case EasyUnlockScreenlockStateHandler::
               STATE_PHONE_LOCKED_AND_TX_POWER_TOO_HIGH:
        return PASSWORD_ENTRY_PHONE_LOCKED_AND_TX_POWER_TOO_HIGH;
      case EasyUnlockScreenlockStateHandler::STATE_AUTHENTICATED:
        return PASSWORD_ENTRY_WITH_AUTHENTICATED_PHONE;
    }
  }

  NOTREACHED();
  return EASY_UNLOCK_AUTH_EVENT_COUNT;
}

#if defined(OS_CHROMEOS)
void EasyUnlockService::OnCryptohomeKeysFetchedForChecking(
    const std::string& user_id,
    const std::set<std::string> paired_devices,
    bool success,
    const chromeos::EasyUnlockDeviceKeyDataList& key_data_list) {
  DCHECK(!user_id.empty() && !paired_devices.empty());

  if (!success) {
    SetHardlockStateForUser(user_id,
                            EasyUnlockScreenlockStateHandler::NO_PAIRING);
    return;
  }

  std::set<std::string> devices_in_cryptohome;
  for (const auto& device_key_data : key_data_list)
    devices_in_cryptohome.insert(device_key_data.psk);

  if (paired_devices != devices_in_cryptohome ||
      GetHardlockState() == EasyUnlockScreenlockStateHandler::NO_PAIRING) {
    SetHardlockStateForUser(
        user_id,
        devices_in_cryptohome.empty()
            ? EasyUnlockScreenlockStateHandler::PAIRING_ADDED
            : EasyUnlockScreenlockStateHandler::PAIRING_CHANGED);
  }
}
#endif

void EasyUnlockService::PrepareForSuspend() {
  app_manager_->DisableAppIfLoaded();
  if (screenlock_state_handler_ && screenlock_state_handler_->IsActive()) {
    UpdateScreenlockState(
        EasyUnlockScreenlockStateHandler::STATE_BLUETOOTH_CONNECTING);
  }
}

void EasyUnlockService::EnsureTpmKeyPresentIfNeeded() {
  if (tpm_key_checked_ || GetType() != TYPE_REGULAR || GetUserEmail().empty())
    return;

#if defined(OS_CHROMEOS)
  // If this is called before the session is started, the chances are Chrome
  // is restarting in order to apply user flags. Don't check TPM keys in this
  // case.
  if (!user_manager::UserManager::Get() ||
      !user_manager::UserManager::Get()->IsSessionStarted())
    return;

  // TODO(tbarzic): Set check_private_key only if previous sign-in attempt
  // failed.
  EasyUnlockTpmKeyManagerFactory::GetInstance()->Get(profile_)
      ->PrepareTpmKey(true /* check_private_key */,
                      base::Closure());
#endif  // defined(OS_CHROMEOS)

  tpm_key_checked_ = true;
}
