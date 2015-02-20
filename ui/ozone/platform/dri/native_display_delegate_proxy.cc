// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/dri/native_display_delegate_proxy.h"

#include <stdio.h>

#include "base/logging.h"
#include "base/thread_task_runner_handle.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/worker_pool.h"
#include "ui/display/types/display_snapshot.h"
#include "ui/display/types/native_display_observer.h"
#include "ui/events/ozone/device/device_event.h"
#include "ui/events/ozone/device/device_manager.h"
#include "ui/ozone/common/display_snapshot_proxy.h"
#include "ui/ozone/common/display_util.h"
#include "ui/ozone/common/gpu/ozone_gpu_messages.h"
#include "ui/ozone/platform/dri/display_manager.h"
#include "ui/ozone/platform/dri/dri_gpu_platform_support_host.h"

namespace ui {

namespace {

const char kDefaultGraphicsCardPath[] = "/dev/dri/card0";

typedef base::Callback<void(const base::FilePath&, base::File)>
    OnOpenDeviceReplyCallback;

void OpenDeviceOnWorkerThread(
    const base::FilePath& path,
    const scoped_refptr<base::TaskRunner>& reply_runner,
    const OnOpenDeviceReplyCallback& callback) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                            base::File::FLAG_WRITE);

  base::File::Info info;
  file.GetInfo(&info);

  CHECK(!info.is_directory);
  CHECK(path.DirName() == base::FilePath("/dev/dri"));

  if (file.IsValid()) {
    reply_runner->PostTask(
        FROM_HERE, base::Bind(callback, path, base::Passed(file.Pass())));
  }
}

class DriDisplaySnapshotProxy : public DisplaySnapshotProxy {
 public:
  DriDisplaySnapshotProxy(const DisplaySnapshot_Params& params,
                          DisplayManager* display_manager)
      : DisplaySnapshotProxy(params), display_manager_(display_manager) {
    display_manager_->RegisterDisplay(this);
  }

  ~DriDisplaySnapshotProxy() override {
    display_manager_->UnregisterDisplay(this);
  }

 private:
  DisplayManager* display_manager_;  // Not owned.

  DISALLOW_COPY_AND_ASSIGN(DriDisplaySnapshotProxy);
};

}  // namespace

NativeDisplayDelegateProxy::NativeDisplayDelegateProxy(
    DriGpuPlatformSupportHost* proxy,
    DeviceManager* device_manager,
    DisplayManager* display_manager)
    : proxy_(proxy),
      device_manager_(device_manager),
      display_manager_(display_manager),
      has_dummy_display_(false),
      weak_ptr_factory_(this) {
  proxy_->RegisterHandler(this);
}

NativeDisplayDelegateProxy::~NativeDisplayDelegateProxy() {
  device_manager_->RemoveObserver(this);
  proxy_->UnregisterHandler(this);
}

void NativeDisplayDelegateProxy::Initialize() {
  device_manager_->AddObserver(this);
  device_manager_->ScanDevices(this);

  if (!displays_.empty())
    return;
  DisplaySnapshot_Params params;
  bool success = false;
  {
    // The file generated by frecon that contains EDID for the 1st display.
    const base::FilePath kEDIDFile("/tmp/display_info.bin");

    // Just read it on current thread as this is necessary information
    // to start. This access only tmpfs, which is fast.
    // TODO(dnicoara|oshima): crbug.com/450886.
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    success = CreateSnapshotFromEDIDFile(kEDIDFile, &params);
  }

  // Fallback to command line if the file doesn't exit or failed to read.
  if (success || CreateSnapshotFromCommandLine(&params)) {
    DCHECK_NE(DISPLAY_CONNECTION_TYPE_NONE, params.type);
    displays_.push_back(new DriDisplaySnapshotProxy(params, display_manager_));
    has_dummy_display_ = true;
  }
}

void NativeDisplayDelegateProxy::GrabServer() {
}

void NativeDisplayDelegateProxy::UngrabServer() {
}

bool NativeDisplayDelegateProxy::TakeDisplayControl() {
  proxy_->Send(new OzoneGpuMsg_TakeDisplayControl());
  return true;
}

bool NativeDisplayDelegateProxy::RelinquishDisplayControl() {
  proxy_->Send(new OzoneGpuMsg_RelinquishDisplayControl());
  return true;
}

void NativeDisplayDelegateProxy::SyncWithServer() {
}

void NativeDisplayDelegateProxy::SetBackgroundColor(uint32_t color_argb) {
  NOTIMPLEMENTED();
}

void NativeDisplayDelegateProxy::ForceDPMSOn() {
  proxy_->Send(new OzoneGpuMsg_ForceDPMSOn());
}

void NativeDisplayDelegateProxy::GetDisplays(
    const GetDisplaysCallback& callback) {
  get_displays_callback_ = callback;
  // GetDisplays() is supposed to force a refresh of the display list.
  if (!proxy_->Send(new OzoneGpuMsg_RefreshNativeDisplays())) {
    get_displays_callback_.Run(displays_.get());
    get_displays_callback_.Reset();
  }
}

void NativeDisplayDelegateProxy::AddMode(const DisplaySnapshot& output,
                                         const DisplayMode* mode) {
}

void NativeDisplayDelegateProxy::Configure(const DisplaySnapshot& output,
                                           const DisplayMode* mode,
                                           const gfx::Point& origin,
                                           const ConfigureCallback& callback) {
  if (has_dummy_display_) {
    callback.Run(true);
    return;
  }

  configure_callback_map_[output.display_id()] = callback;

  bool status = false;
  if (mode) {
    status = proxy_->Send(new OzoneGpuMsg_ConfigureNativeDisplay(
        output.display_id(), GetDisplayModeParams(*mode), origin));
  } else {
    status =
        proxy_->Send(new OzoneGpuMsg_DisableNativeDisplay(output.display_id()));
  }

  if (!status)
    OnDisplayConfigured(output.display_id(), false);
}

void NativeDisplayDelegateProxy::CreateFrameBuffer(const gfx::Size& size) {
}

bool NativeDisplayDelegateProxy::GetHDCPState(const DisplaySnapshot& output,
                                              HDCPState* state) {
  NOTIMPLEMENTED();
  return false;
}

bool NativeDisplayDelegateProxy::SetHDCPState(const DisplaySnapshot& output,
                                              HDCPState state) {
  NOTIMPLEMENTED();
  return false;
}

std::vector<ColorCalibrationProfile>
NativeDisplayDelegateProxy::GetAvailableColorCalibrationProfiles(
    const DisplaySnapshot& output) {
  NOTIMPLEMENTED();
  return std::vector<ColorCalibrationProfile>();
}

bool NativeDisplayDelegateProxy::SetColorCalibrationProfile(
    const DisplaySnapshot& output,
    ColorCalibrationProfile new_profile) {
  NOTIMPLEMENTED();
  return false;
}

void NativeDisplayDelegateProxy::AddObserver(NativeDisplayObserver* observer) {
  observers_.AddObserver(observer);
}

void NativeDisplayDelegateProxy::RemoveObserver(
    NativeDisplayObserver* observer) {
  observers_.RemoveObserver(observer);
}

void NativeDisplayDelegateProxy::OnDeviceEvent(const DeviceEvent& event) {
  if (event.device_type() != DeviceEvent::DISPLAY)
    return;

  switch (event.action_type()) {
    case DeviceEvent::ADD:
      VLOG(1) << "Got display added event for " << event.path().value();
      // The default card is a special case since it needs to be opened early on
      // the GPU process in order to initialize EGL. If it is opened here as
      // well, it will cause a race with opening it in the GPU process and the
      // GPU process may fail initialization.
      // TODO(dnicoara) Remove this when EGL_DEFAULT_DISPLAY is the only native
      // display we return in GbmSurfaceFactory.
      if (event.path().value() == kDefaultGraphicsCardPath)
        return;

      base::WorkerPool::PostTask(
          FROM_HERE,
          base::Bind(
              &OpenDeviceOnWorkerThread, event.path(),
              base::ThreadTaskRunnerHandle::Get(),
              base::Bind(&NativeDisplayDelegateProxy::OnNewGraphicsDevice,
                         weak_ptr_factory_.GetWeakPtr())),
          false /* task_is_slow */);
      return;
    case DeviceEvent::CHANGE:
      VLOG(1) << "Got display changed event for " << event.path().value();
      break;
    case DeviceEvent::REMOVE:
      VLOG(1) << "Got display removed event for " << event.path().value();
      // It shouldn't be possible to remove this device.
      DCHECK_NE(kDefaultGraphicsCardPath, event.path().value());
      proxy_->Send(new OzoneGpuMsg_RemoveGraphicsDevice(event.path()));
      break;
  }

  FOR_EACH_OBSERVER(NativeDisplayObserver, observers_,
                    OnConfigurationChanged());
}

void NativeDisplayDelegateProxy::OnNewGraphicsDevice(const base::FilePath& path,
                                                     base::File file) {
  DCHECK(file.IsValid());
  proxy_->Send(new OzoneGpuMsg_AddGraphicsDevice(
      path, base::FileDescriptor(file.Pass())));

  FOR_EACH_OBSERVER(NativeDisplayObserver, observers_,
                    OnConfigurationChanged());
}

void NativeDisplayDelegateProxy::OnChannelEstablished(
    int host_id,
    scoped_refptr<base::SingleThreadTaskRunner> send_runner,
    const base::Callback<void(IPC::Message*)>& send_callback) {
  device_manager_->ScanDevices(this);
  FOR_EACH_OBSERVER(NativeDisplayObserver, observers_,
                    OnConfigurationChanged());
}

void NativeDisplayDelegateProxy::OnChannelDestroyed(int host_id) {
  // If the channel got destroyed in the middle of a configuration then just
  // respond with failure.
  if (!get_displays_callback_.is_null()) {
    get_displays_callback_.Run(std::vector<DisplaySnapshot*>());
    get_displays_callback_.Reset();
  }

  for (const auto& pair : configure_callback_map_) {
    pair.second.Run(false);
  }

  configure_callback_map_.clear();
}

bool NativeDisplayDelegateProxy::OnMessageReceived(
    const IPC::Message& message) {
  bool handled = true;

  IPC_BEGIN_MESSAGE_MAP(NativeDisplayDelegateProxy, message)
  IPC_MESSAGE_HANDLER(OzoneHostMsg_UpdateNativeDisplays, OnUpdateNativeDisplays)
  IPC_MESSAGE_HANDLER(OzoneHostMsg_DisplayConfigured, OnDisplayConfigured)
  IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  return handled;
}

void NativeDisplayDelegateProxy::OnUpdateNativeDisplays(
    const std::vector<DisplaySnapshot_Params>& displays) {
  has_dummy_display_ = false;
  displays_.clear();
  for (size_t i = 0; i < displays.size(); ++i)
    displays_.push_back(
        new DriDisplaySnapshotProxy(displays[i], display_manager_));

  if (!get_displays_callback_.is_null()) {
    get_displays_callback_.Run(displays_.get());
    get_displays_callback_.Reset();
  }
}

void NativeDisplayDelegateProxy::OnDisplayConfigured(int64_t display_id,
                                                     bool status) {
  auto it = configure_callback_map_.find(display_id);
  if (it != configure_callback_map_.end()) {
    it->second.Run(status);
    configure_callback_map_.erase(it);
  }
}

}  // namespace ui
