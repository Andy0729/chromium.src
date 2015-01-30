// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/events/ozone/evdev/event_factory_evdev.h"

#include "base/bind.h"
#include "base/debug/trace_event.h"
#include "base/task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "base/threading/worker_pool.h"
#include "base/time/time.h"
#include "ui/events/devices/device_data_manager.h"
#include "ui/events/devices/input_device.h"
#include "ui/events/ozone/device/device_event.h"
#include "ui/events/ozone/device/device_manager.h"
#include "ui/events/ozone/evdev/cursor_delegate_evdev.h"
#include "ui/events/ozone/evdev/input_controller_evdev.h"
#include "ui/events/ozone/evdev/input_device_factory_evdev.h"
#include "ui/events/ozone/evdev/input_injector_evdev.h"

namespace ui {

namespace {

// Thread safe dispatcher proxy for EventFactoryEvdev.
class ProxyDeviceEventDispatcher : public DeviceEventDispatcherEvdev {
 public:
  ProxyDeviceEventDispatcher(
      scoped_refptr<base::SingleThreadTaskRunner> ui_thread_runner,
      base::WeakPtr<EventFactoryEvdev> event_factory_evdev)
      : ui_thread_runner_(ui_thread_runner),
        event_factory_evdev_(event_factory_evdev) {}
  ~ProxyDeviceEventDispatcher() override {}

  // DeviceEventDispatcher:
  void DispatchKeyEvent(const KeyEventParams& params) override {
    ui_thread_runner_->PostTask(FROM_HERE,
                                base::Bind(&EventFactoryEvdev::DispatchKeyEvent,
                                           event_factory_evdev_, params));
  }

  void DispatchMouseMoveEvent(const MouseMoveEventParams& params) override {
    ui_thread_runner_->PostTask(
        FROM_HERE, base::Bind(&EventFactoryEvdev::DispatchMouseMoveEvent,
                              event_factory_evdev_, params));
  }

  void DispatchMouseButtonEvent(const MouseButtonEventParams& params) override {
    ui_thread_runner_->PostTask(
        FROM_HERE, base::Bind(&EventFactoryEvdev::DispatchMouseButtonEvent,
                              event_factory_evdev_, params));
  }

  void DispatchMouseWheelEvent(const MouseWheelEventParams& params) override {
    ui_thread_runner_->PostTask(
        FROM_HERE, base::Bind(&EventFactoryEvdev::DispatchMouseWheelEvent,
                              event_factory_evdev_, params));
  }

  void DispatchScrollEvent(const ScrollEventParams& params) override {
    ui_thread_runner_->PostTask(
        FROM_HERE, base::Bind(&EventFactoryEvdev::DispatchScrollEvent,
                              event_factory_evdev_, params));
  }

  void DispatchTouchEvent(const TouchEventParams& params) override {
    ui_thread_runner_->PostTask(
        FROM_HERE, base::Bind(&EventFactoryEvdev::DispatchTouchEvent,
                              event_factory_evdev_, params));
  }

  void DispatchKeyboardDevicesUpdated(
      const std::vector<KeyboardDevice>& devices) override {
    ui_thread_runner_->PostTask(
        FROM_HERE,
        base::Bind(&EventFactoryEvdev::DispatchKeyboardDevicesUpdated,
                   event_factory_evdev_, devices));
  }
  void DispatchTouchscreenDevicesUpdated(
      const std::vector<TouchscreenDevice>& devices) override {
    ui_thread_runner_->PostTask(
        FROM_HERE,
        base::Bind(&EventFactoryEvdev::DispatchTouchscreenDevicesUpdated,
                   event_factory_evdev_, devices));
  }
  void DispatchMouseDevicesUpdated(
      const std::vector<InputDevice>& devices) override {
    ui_thread_runner_->PostTask(
        FROM_HERE, base::Bind(&EventFactoryEvdev::DispatchMouseDevicesUpdated,
                              event_factory_evdev_, devices));
  }
  void DispatchTouchpadDevicesUpdated(
      const std::vector<InputDevice>& devices) override {
    ui_thread_runner_->PostTask(
        FROM_HERE,
        base::Bind(&EventFactoryEvdev::DispatchTouchpadDevicesUpdated,
                   event_factory_evdev_, devices));
  }

 private:
  scoped_refptr<base::SingleThreadTaskRunner> ui_thread_runner_;
  base::WeakPtr<EventFactoryEvdev> event_factory_evdev_;
};

}  // namespace

EventFactoryEvdev::EventFactoryEvdev(CursorDelegateEvdev* cursor,
                                     DeviceManager* device_manager,
                                     KeyboardLayoutEngine* keyboard_layout)
    : last_device_id_(0),
      device_manager_(device_manager),
      dispatch_callback_(
          base::Bind(&EventFactoryEvdev::PostUiEvent, base::Unretained(this))),
      keyboard_(&modifiers_, keyboard_layout, dispatch_callback_),
      cursor_(cursor),
      input_controller_(&keyboard_, &button_map_),
      initialized_(false),
      weak_ptr_factory_(this) {
  DCHECK(device_manager_);
}

EventFactoryEvdev::~EventFactoryEvdev() {
}

void EventFactoryEvdev::Init() {
  DCHECK(!initialized_);

  // Set up device factory.
  scoped_ptr<DeviceEventDispatcherEvdev> dispatcher(
      new ProxyDeviceEventDispatcher(base::ThreadTaskRunnerHandle::Get(),
                                          weak_ptr_factory_.GetWeakPtr()));
  input_device_factory_.reset(
      new InputDeviceFactoryEvdev(dispatcher.Pass(), cursor_));

  // TODO(spang): This settings interface is really broken. crbug.com/450899
  input_controller_.SetInputDeviceFactory(input_device_factory_.get());

  // Scan & monitor devices.
  device_manager_->AddObserver(this);
  device_manager_->ScanDevices(this);

  initialized_ = true;
}

scoped_ptr<SystemInputInjector> EventFactoryEvdev::CreateSystemInputInjector() {
  // Use forwarding dispatcher for the injector rather than dispatching
  // directly. We cannot assume it is safe to (re-)enter ui::Event dispatch
  // synchronously from the injection point.
  scoped_ptr<DeviceEventDispatcherEvdev> dispatcher(
      new ProxyDeviceEventDispatcher(base::ThreadTaskRunnerHandle::Get(),
                                     weak_ptr_factory_.GetWeakPtr()));
  return make_scoped_ptr(new InputInjectorEvdev(dispatcher.Pass(), cursor_));
}

void EventFactoryEvdev::DispatchKeyEvent(const KeyEventParams& params) {
  keyboard_.OnKeyChange(params.code, params.down);
}

void EventFactoryEvdev::DispatchMouseMoveEvent(
    const MouseMoveEventParams& params) {
  scoped_ptr<MouseEvent> event(new MouseEvent(ui::ET_MOUSE_MOVED,
                                              params.location, params.location,
                                              modifiers_.GetModifierFlags(),
                                              /* changed_button_flags */ 0));
  event->set_source_device_id(params.device_id);
  PostUiEvent(event.Pass());
}

void EventFactoryEvdev::DispatchMouseButtonEvent(
    const MouseButtonEventParams& params) {
  // Mouse buttons can be remapped, touchpad taps & clicks cannot.
  unsigned int button = params.button;
  if (params.allow_remap)
    button = button_map_.GetMappedButton(button);

  int modifier = EVDEV_MODIFIER_NONE;
  switch (button) {
    case BTN_LEFT:
      modifier = EVDEV_MODIFIER_LEFT_MOUSE_BUTTON;
      break;
    case BTN_RIGHT:
      modifier = EVDEV_MODIFIER_RIGHT_MOUSE_BUTTON;
      break;
    case BTN_MIDDLE:
      modifier = EVDEV_MODIFIER_MIDDLE_MOUSE_BUTTON;
      break;
    default:
      return;
  }

  int flag = modifiers_.GetEventFlagFromModifier(modifier);
  modifiers_.UpdateModifier(modifier, params.down);

  scoped_ptr<MouseEvent> event(new MouseEvent(
      params.down ? ui::ET_MOUSE_PRESSED : ui::ET_MOUSE_RELEASED,
      params.location, params.location, modifiers_.GetModifierFlags() | flag,
      /* changed_button_flags */ flag));
  event->set_source_device_id(params.device_id);
  PostUiEvent(event.Pass());
}

void EventFactoryEvdev::DispatchMouseWheelEvent(
    const MouseWheelEventParams& params) {
  scoped_ptr<MouseWheelEvent> event(new MouseWheelEvent(
      params.delta, params.location, params.location,
      modifiers_.GetModifierFlags(), 0 /* changed_button_flags */));
  event->set_source_device_id(params.device_id);
  PostUiEvent(event.Pass());
}

void EventFactoryEvdev::DispatchScrollEvent(const ScrollEventParams& params) {
  scoped_ptr<ScrollEvent> event(new ScrollEvent(
      params.type, params.location, params.timestamp,
      modifiers_.GetModifierFlags(), params.delta.x(), params.delta.y(),
      params.ordinal_delta.x(), params.ordinal_delta.y(), params.finger_count));
  event->set_source_device_id(params.device_id);
  PostUiEvent(event.Pass());
}

void EventFactoryEvdev::DispatchTouchEvent(const TouchEventParams& params) {
  float x = params.location.x();
  float y = params.location.y();
  double radius_x = params.radii.x();
  double radius_y = params.radii.y();

  // Transform the event to align touches to the image based on display mode.
  DeviceDataManager::GetInstance()->ApplyTouchTransformer(params.device_id, &x,
                                                          &y);
  DeviceDataManager::GetInstance()->ApplyTouchRadiusScale(params.device_id,
                                                          &radius_x);
  DeviceDataManager::GetInstance()->ApplyTouchRadiusScale(params.device_id,
                                                          &radius_y);

  scoped_ptr<TouchEvent> touch_event(new TouchEvent(
      params.type, gfx::PointF(x, y), modifiers_.GetModifierFlags(),
      params.touch_id, params.timestamp, radius_x, radius_y,
      /* angle */ 0., params.pressure));
  touch_event->set_source_device_id(params.device_id);
  PostUiEvent(touch_event.Pass());
}

void EventFactoryEvdev::PostUiEvent(scoped_ptr<Event> event) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&EventFactoryEvdev::DispatchUiEventTask,
                 weak_ptr_factory_.GetWeakPtr(),
                 base::Passed(&event)));
}

void EventFactoryEvdev::DispatchKeyboardDevicesUpdated(
    const std::vector<KeyboardDevice>& devices) {
  DeviceHotplugEventObserver* observer = DeviceDataManager::GetInstance();
  observer->OnKeyboardDevicesUpdated(devices);
}

void EventFactoryEvdev::DispatchTouchscreenDevicesUpdated(
    const std::vector<TouchscreenDevice>& devices) {
  DeviceHotplugEventObserver* observer = DeviceDataManager::GetInstance();
  observer->OnTouchscreenDevicesUpdated(devices);
}

void EventFactoryEvdev::DispatchMouseDevicesUpdated(
    const std::vector<InputDevice>& devices) {
  // There's no list of mice in DeviceDataManager.
  input_controller_.set_has_mouse(devices.size() != 0);
}

void EventFactoryEvdev::DispatchTouchpadDevicesUpdated(
    const std::vector<InputDevice>& devices) {
  // There's no list of touchpads in DeviceDataManager.
  input_controller_.set_has_touchpad(devices.size() != 0);
}

void EventFactoryEvdev::DispatchUiEventTask(scoped_ptr<Event> event) {
  DispatchEvent(event.get());
}

void EventFactoryEvdev::OnDeviceEvent(const DeviceEvent& event) {
  if (event.device_type() != DeviceEvent::INPUT)
    return;

  switch (event.action_type()) {
    case DeviceEvent::ADD:
    case DeviceEvent::CHANGE: {
      TRACE_EVENT1("ozone", "OnDeviceAdded", "path", event.path().value());
      input_device_factory_->AddInputDevice(NextDeviceId(), event.path());
      break;
    }
    case DeviceEvent::REMOVE: {
      TRACE_EVENT1("ozone", "OnDeviceRemoved", "path", event.path().value());
      input_device_factory_->RemoveInputDevice(event.path());
      break;
    }
  }
}

void EventFactoryEvdev::OnDispatcherListChanged() {
  if (!initialized_)
    Init();
}

void EventFactoryEvdev::WarpCursorTo(gfx::AcceleratedWidget widget,
                                     const gfx::PointF& location) {
  if (cursor_) {
    cursor_->MoveCursorTo(widget, location);
    PostUiEvent(make_scoped_ptr(new MouseEvent(ET_MOUSE_MOVED,
                                               cursor_->GetLocation(),
                                               cursor_->GetLocation(),
                                               modifiers_.GetModifierFlags(),
                                               /* changed_button_flags */ 0)));
  }
}

int EventFactoryEvdev::NextDeviceId() {
  return ++last_device_id_;
}

}  // namespace ui
