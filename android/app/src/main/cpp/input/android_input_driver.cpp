#include "android_input_driver.h"
#include <rex/input/device_assignment.h>
#include <android/log.h>
#include <android/keycodes.h>
#include <cmath>
#include <algorithm>

#define TAG "NFS-AndroidInput"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

namespace rex::input::android {

static AndroidInputDriver* g_active_input_driver = nullptr;

AndroidInputDriver::AndroidInputDriver(rex::ui::Window* window)
    : InputDriver(window, 0) {
  g_active_input_driver = this;
  LOGI("AndroidInputDriver constructed");
}

AndroidInputDriver::~AndroidInputDriver() {
  if (g_active_input_driver == this) {
    g_active_input_driver = nullptr;
  }
  LOGI("AndroidInputDriver destroyed");
}

AndroidInputDriver* AndroidInputDriver::GetActiveDriver() {
  return g_active_input_driver;
}

X_STATUS AndroidInputDriver::Setup() {
  return X_STATUS_SUCCESS;
}

constexpr DeviceId kDefaultDeviceId = static_cast<DeviceId>(1);

void AndroidInputDriver::EnumerateDevices(std::vector<DeviceInfo>& out) {
  DeviceInfo info{};
  info.id = kDefaultDeviceId;
  info.ordinal = 0;
  info.name = "Android Native Gamepad & Touch";
  info.guid = "android-gamepad-touch";
  info.synthetic = false;
  out.push_back(std::move(info));
}

X_RESULT AndroidInputDriver::GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) {
  if (!out_state || id == DeviceId::kInvalid) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  std::lock_guard<std::mutex> lock(state_mutex_);

  X_INPUT_GAMEPAD state = gamepad_state_;

  // Merge virtual touch overlay controls
  touch_overlay_.UpdateGamepadState(state);

  out_state->packet_number = ++packet_number_;
  out_state->gamepad = state;

  static uint32_t poll_count = 0;
  if ((++poll_count % 300) == 1) {
    LOGI("GetDeviceState: id=%lu, poll=%u, buttons=0x%04X, lx=%d, ly=%d",
         static_cast<unsigned long>(id), poll_count, static_cast<uint16_t>(state.buttons),
         static_cast<int16_t>(state.thumb_lx), static_cast<int16_t>(state.thumb_ly));
  }

  return X_ERROR_SUCCESS;
}

X_RESULT AndroidInputDriver::GetDeviceCapabilities(DeviceId id, uint32_t flags, X_INPUT_CAPABILITIES* out_caps) {
  if (!out_caps || id == DeviceId::kInvalid) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  LOGI("GetDeviceCapabilities called for id=%lu", static_cast<unsigned long>(id));

  std::memset(out_caps, 0, sizeof(X_INPUT_CAPABILITIES));
  out_caps->type = XINPUT_DEVTYPE_GAMEPAD;
  out_caps->sub_type = 1; // Gamepad
  out_caps->flags = X_INPUT_CAPS_FFB_SUPPORTED;

  return X_ERROR_SUCCESS;
}

X_RESULT AndroidInputDriver::SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) {
  return X_ERROR_SUCCESS;
}

X_RESULT AndroidInputDriver::GetDeviceKeystroke(DeviceId id, uint32_t flags, X_INPUT_KEYSTROKE* out_keystroke) {
  return X_ERROR_EMPTY;
}

void AndroidInputDriver::SetScreenDimensions(uint32_t width, uint32_t height) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  touch_overlay_.SetScreenSize(width, height);
}

bool AndroidInputDriver::HandleInputEvent(const AInputEvent* event) {
  int32_t event_type = AInputEvent_getType(event);
  int32_t source = AInputEvent_getSource(event);

  if (event_type == AINPUT_EVENT_TYPE_KEY) {
    int32_t key_code = AKeyEvent_getKeyCode(event);
    int32_t action = AKeyEvent_getAction(event);
    LOGI("Controller Key Event: keycode=%d, action=%d, source=0x%04X", key_code, action, source);
    if (HandleGamepadKeyEvent(event)) {
      return true;
    }
  } else if (event_type == AINPUT_EVENT_TYPE_MOTION) {
    if ((source & (AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK)) != 0) {
      HandleGamepadMotionEvent(event);
      return true;
    } else if ((source & AINPUT_SOURCE_TOUCHSCREEN) == AINPUT_SOURCE_TOUCHSCREEN) {
      HandleTouchEvent(event);
      return true;
    }
  }

  return false;
}

bool AndroidInputDriver::HandleGamepadKeyEvent(const AInputEvent* event) {
  int32_t action = AKeyEvent_getAction(event);
  int32_t key_code = AKeyEvent_getKeyCode(event);
  bool is_down = (action == AKEY_EVENT_ACTION_DOWN);

  std::lock_guard<std::mutex> lock(state_mutex_);
  uint16_t mask = 0;

  switch (key_code) {
    // Face buttons
    case AKEYCODE_BUTTON_A:
    case AKEYCODE_DPAD_CENTER:
      mask = X_INPUT_GAMEPAD_A;
      break;
    case AKEYCODE_BUTTON_B:
      mask = X_INPUT_GAMEPAD_B;
      break;
    case AKEYCODE_BUTTON_X:
      mask = X_INPUT_GAMEPAD_X;
      break;
    case AKEYCODE_BUTTON_Y:
      mask = X_INPUT_GAMEPAD_Y;
      break;

    // Bumpers / Shoulders
    case AKEYCODE_BUTTON_L1:
      mask = X_INPUT_GAMEPAD_LEFT_SHOULDER;
      break;
    case AKEYCODE_BUTTON_R1:
      mask = X_INPUT_GAMEPAD_RIGHT_SHOULDER;
      break;

    // Digital Triggers
    case AKEYCODE_BUTTON_L2:
      gamepad_state_.left_trigger = is_down ? 255 : 0;
      return true;
    case AKEYCODE_BUTTON_R2:
      gamepad_state_.right_trigger = is_down ? 255 : 0;
      return true;

    // Thumbstick clicks
    case AKEYCODE_BUTTON_THUMBL:
      mask = X_INPUT_GAMEPAD_LEFT_THUMB;
      break;
    case AKEYCODE_BUTTON_THUMBR:
      mask = X_INPUT_GAMEPAD_RIGHT_THUMB;
      break;

    // Start / Menu
    case AKEYCODE_BUTTON_START:
    case AKEYCODE_MENU:
    case AKEYCODE_ENTER:
      mask = X_INPUT_GAMEPAD_START;
      break;

    // Select / Back
    case AKEYCODE_BUTTON_SELECT:
    case AKEYCODE_BACK:
    case AKEYCODE_ESCAPE:
      mask = X_INPUT_GAMEPAD_BACK;
      break;

    // D-Pad buttons
    case AKEYCODE_DPAD_UP:
      mask = X_INPUT_GAMEPAD_DPAD_UP;
      break;
    case AKEYCODE_DPAD_DOWN:
      mask = X_INPUT_GAMEPAD_DPAD_DOWN;
      break;
    case AKEYCODE_DPAD_LEFT:
      mask = X_INPUT_GAMEPAD_DPAD_LEFT;
      break;
    case AKEYCODE_DPAD_RIGHT:
      mask = X_INPUT_GAMEPAD_DPAD_RIGHT;
      break;

    default:
      return false;
  }

  if (mask != 0) {
    uint16_t b = gamepad_state_.buttons;
    if (is_down) {
      b |= mask;
    } else {
      b &= ~mask;
    }
    gamepad_state_.buttons = b;
    return true;
  }

  return false;
}

void AndroidInputDriver::HandleGamepadMotionEvent(const AInputEvent* event) {
  std::lock_guard<std::mutex> lock(state_mutex_);

  // Left stick (X, Y)
  float lx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
  float ly = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);

  // Right stick (Z, RZ or RX, RY)
  float rx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
  float ry = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
  if (rx == 0.0f && ry == 0.0f) {
    rx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RX, 0);
    ry = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RY, 0);
  }

  // Triggers (LTRIGGER / BRAKE and RTRIGGER / GAS)
  float lt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0);
  if (lt <= 0.0f) {
    lt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_BRAKE, 0);
  }
  float rt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0);
  if (rt <= 0.0f) {
    rt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_GAS, 0);
  }

  // Hat (D-Pad)
  float hat_x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
  float hat_y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);

  // Apply deadzone to analog sticks (12%)
  auto apply_deadzone = [](float val) -> float {
    constexpr float kDeadzone = 0.12f;
    if (std::abs(val) < kDeadzone) return 0.0f;
    return val;
  };

  lx = apply_deadzone(lx);
  ly = apply_deadzone(ly);
  rx = apply_deadzone(rx);
  ry = apply_deadzone(ry);

  gamepad_state_.thumb_lx = static_cast<int16_t>(std::clamp(lx, -1.0f, 1.0f) * 32767.0f);
  gamepad_state_.thumb_ly = static_cast<int16_t>(std::clamp(-ly, -1.0f, 1.0f) * 32767.0f); // Invert Y
  gamepad_state_.thumb_rx = static_cast<int16_t>(std::clamp(rx, -1.0f, 1.0f) * 32767.0f);
  gamepad_state_.thumb_ry = static_cast<int16_t>(std::clamp(-ry, -1.0f, 1.0f) * 32767.0f); // Invert Y

  if (lt > 0.0f) {
    gamepad_state_.left_trigger = static_cast<uint8_t>(std::clamp(lt, 0.0f, 1.0f) * 255.0f);
  }
  if (rt > 0.0f) {
    gamepad_state_.right_trigger = static_cast<uint8_t>(std::clamp(rt, 0.0f, 1.0f) * 255.0f);
  }

  // Hat D-Pad buttons
  uint16_t b = gamepad_state_.buttons;
  if (hat_x < -0.5f) {
    b |= X_INPUT_GAMEPAD_DPAD_LEFT;
    b &= ~X_INPUT_GAMEPAD_DPAD_RIGHT;
  } else if (hat_x > 0.5f) {
    b |= X_INPUT_GAMEPAD_DPAD_RIGHT;
    b &= ~X_INPUT_GAMEPAD_DPAD_LEFT;
  }

  if (hat_y < -0.5f) {
    b |= X_INPUT_GAMEPAD_DPAD_UP;
    b &= ~X_INPUT_GAMEPAD_DPAD_DOWN;
  } else if (hat_y > 0.5f) {
    b |= X_INPUT_GAMEPAD_DPAD_DOWN;
    b &= ~X_INPUT_GAMEPAD_DPAD_UP;
  }
  gamepad_state_.buttons = b;
}

void AndroidInputDriver::HandleTouchEvent(const AInputEvent* event) {
  int32_t action = AMotionEvent_getAction(event);
  int32_t action_code = action & AMOTION_EVENT_ACTION_MASK;
  size_t pointer_index = static_cast<size_t>((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);

  std::lock_guard<std::mutex> lock(state_mutex_);

  switch (action_code) {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_DOWN: {
      int id = AMotionEvent_getPointerId(event, pointer_index);
      float x = AMotionEvent_getX(event, pointer_index);
      float y = AMotionEvent_getY(event, pointer_index);
      touch_overlay_.ProcessPointerDown(id, x, y);
      break;
    }
    case AMOTION_EVENT_ACTION_MOVE: {
      size_t count = AMotionEvent_getPointerCount(event);
      for (size_t i = 0; i < count; ++i) {
        int id = AMotionEvent_getPointerId(event, i);
        float x = AMotionEvent_getX(event, i);
        float y = AMotionEvent_getY(event, i);
        touch_overlay_.ProcessPointerMove(id, x, y);
      }
      break;
    }
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP:
    case AMOTION_EVENT_ACTION_CANCEL: {
      int id = AMotionEvent_getPointerId(event, pointer_index);
      touch_overlay_.ProcessPointerUp(id);
      break;
    }
    default: break;
  }
}

// ---------------------------------------------------------------------------
//  AndroidInputSystem implementation
// ---------------------------------------------------------------------------

AndroidInputSystem::AndroidInputSystem(rex::ui::Window* window)
    : InputSystem(window) {
  auto driver = std::make_unique<AndroidInputDriver>(window);
  driver_ = driver.get();
  AddDriver(std::move(driver));
  SetDeviceAssignment(std::make_unique<SlotAssignment>());
  LOGI("AndroidInputSystem initialized with native AndroidInputDriver and SlotAssignment");
}

AndroidInputSystem::~AndroidInputSystem() = default;

std::unique_ptr<system::IInputSystem> AndroidInputSystem::Create(bool tool_mode) {
  return std::make_unique<AndroidInputSystem>(nullptr);
}

}  // namespace rex::input::android
