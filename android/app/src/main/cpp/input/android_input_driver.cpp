#include "android_input_driver.h"
#include <android/log.h>
#include <android/keycodes.h>
#include <rex/input/device_assignment.h>
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

#include <jni.h>

extern "C" JNIEXPORT void JNICALL
Java_com_ea_nfsmw_GameActivity_nativeSetVirtualGamepad(
    JNIEnv* env, jclass clazz, jint buttons, jfloat thumb_lx, jfloat thumb_ly,
    jfloat left_trigger, jfloat right_trigger) {
  auto* driver = rex::input::android::AndroidInputDriver::GetActiveDriver();
  if (driver) {
    driver->SetVirtualControlsState(static_cast<uint16_t>(buttons), thumb_lx, thumb_ly, left_trigger, right_trigger);
  } else {
    static int s_null_cnt = 0;
    if (++s_null_cnt % 30 == 1) {
      LOGI("nativeSetVirtualGamepad: driver is NULL! (cnt=%d)", s_null_cnt);
    }
  }
}

void RegisterVirtualGamepadJNI(void* java_vm, void* activity_obj) {
  if (!java_vm || !activity_obj) {
    LOGI("RegisterVirtualGamepadJNI: null parameters");
    return;
  }
  JavaVM* vm = reinterpret_cast<JavaVM*>(java_vm);
  jobject act = reinterpret_cast<jobject>(activity_obj);

  JNIEnv* env = nullptr;
  jint res = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
  if (res == JNI_EDETACHED) {
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) {
      LOGI("RegisterVirtualGamepadJNI: AttachCurrentThread failed");
      return;
    }
  } else if (res != JNI_OK || !env) {
    LOGI("RegisterVirtualGamepadJNI: GetEnv failed (%d)", res);
    return;
  }

  jclass clazz = env->GetObjectClass(act);
  if (!clazz) {
    LOGI("RegisterVirtualGamepadJNI: GetObjectClass returned null");
    return;
  }

  static const JNINativeMethod methods[] = {
      { const_cast<char*>("nativeSetVirtualGamepad"),
        const_cast<char*>("(IFFFF)V"),
        reinterpret_cast<void*>(&Java_com_ea_nfsmw_GameActivity_nativeSetVirtualGamepad) }
  };

  if (env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0])) < 0) {
    LOGI("RegisterVirtualGamepadJNI: RegisterNatives FAILED");
    if (env->ExceptionCheck()) {
      env->ExceptionDescribe();
      env->ExceptionClear();
    }
  } else {
    LOGI("RegisterVirtualGamepadJNI: RegisterNatives SUCCEEDED for GameActivity.nativeSetVirtualGamepad");
  }

  env->DeleteLocalRef(clazz);
}

void AndroidInputDriver::SetVirtualControlsState(uint16_t buttons, float lx, float ly, float lt, float rt) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  virtual_buttons_ = buttons;
  virtual_lx_ = lx;
  virtual_ly_ = ly;
  virtual_lt_ = lt;
  virtual_rt_ = rt;

  static uint16_t s_prev_btn = 0;
  if (buttons != s_prev_btn || lt > 0.0f || rt > 0.0f || std::abs(lx) > 0.1f) {
    s_prev_btn = buttons;
    LOGI("SetVirtualControlsState: btn=0x%04x lx=%.2f ly=%.2f lt=%.2f rt=%.2f", buttons, lx, ly, lt, rt);
  }
}

X_RESULT AndroidInputDriver::GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) {
  if (!out_state) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  std::lock_guard<std::mutex> lock(state_mutex_);

  X_INPUT_GAMEPAD state = gamepad_state_;

  // Merge Java VirtualControlsView state
  uint16_t buttons = state.buttons;
  buttons |= virtual_buttons_;
  state.buttons = buttons;
  if (std::abs(virtual_lx_) > 0.05f) {
    state.thumb_lx = static_cast<int16_t>(std::clamp(virtual_lx_, -1.0f, 1.0f) * 32767.0f);
  }
  if (std::abs(virtual_ly_) > 0.05f) {
    state.thumb_ly = static_cast<int16_t>(std::clamp(virtual_ly_, -1.0f, 1.0f) * 32767.0f);
  }
  if (virtual_lt_ > 0.0f) {
    state.left_trigger = std::max(state.left_trigger, static_cast<uint8_t>(std::clamp(virtual_lt_, 0.0f, 1.0f) * 255.0f));
  }
  if (virtual_rt_ > 0.0f) {
    state.right_trigger = std::max(state.right_trigger, static_cast<uint8_t>(std::clamp(virtual_rt_, 0.0f, 1.0f) * 255.0f));
  }

  // Merge virtual touch overlay controls
  touch_overlay_.UpdateGamepadState(state);

  out_state->packet_number = ++packet_number_;
  out_state->gamepad = state;

  static int s_poll_count = 0;
  if (++s_poll_count % 180 == 0 || state.buttons != 0 || state.left_trigger > 0 || state.right_trigger > 0) {
    LOGI("GetDeviceState: id=%llu SUCCESS btn=0x%04x lx=%d ly=%d lt=%u rt=%u",
         static_cast<unsigned long long>(id), state.buttons, state.thumb_lx, state.thumb_ly, state.left_trigger, state.right_trigger);
  }

  return X_ERROR_SUCCESS;
}

X_RESULT AndroidInputDriver::GetDeviceCapabilities(DeviceId id, uint32_t flags, X_INPUT_CAPABILITIES* out_caps) {
  if (!out_caps) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  std::memset(out_caps, 0, sizeof(X_INPUT_CAPABILITIES));
  out_caps->type = XINPUT_DEVTYPE_GAMEPAD;
  out_caps->sub_type = 1; // Gamepad
  out_caps->flags = X_INPUT_CAPS_FFB_SUPPORTED;

  return X_ERROR_SUCCESS;
}

X_RESULT AndroidInputDriver::SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) {
  // Can trigger Android device haptics/vibrator service here if desired
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
    if ((source & AINPUT_SOURCE_GAMEPAD) || (source & AINPUT_SOURCE_JOYSTICK)) {
      HandleGamepadKeyEvent(event);
      return true;
    }
  } else if (event_type == AINPUT_EVENT_TYPE_MOTION) {
    if ((source & AINPUT_SOURCE_GAMEPAD) || (source & AINPUT_SOURCE_JOYSTICK)) {
      HandleGamepadMotionEvent(event);
      return true;
    } else if (source & AINPUT_SOURCE_TOUCHSCREEN) {
      HandleTouchEvent(event);
      return true;
    }
  }

  return false;
}

void AndroidInputDriver::HandleGamepadKeyEvent(const AInputEvent* event) {
  int32_t action = AKeyEvent_getAction(event);
  int32_t key_code = AKeyEvent_getKeyCode(event);
  bool is_down = (action == AKEY_EVENT_ACTION_DOWN);

  std::lock_guard<std::mutex> lock(state_mutex_);
  uint16_t mask = 0;

  switch (key_code) {
    case AKEYCODE_BUTTON_A:       mask = X_INPUT_GAMEPAD_A; break;
    case AKEYCODE_BUTTON_B:       mask = X_INPUT_GAMEPAD_B; break;
    case AKEYCODE_BUTTON_X:       mask = X_INPUT_GAMEPAD_X; break;
    case AKEYCODE_BUTTON_Y:       mask = X_INPUT_GAMEPAD_Y; break;
    case AKEYCODE_BUTTON_L1:      mask = X_INPUT_GAMEPAD_LEFT_SHOULDER; break;
    case AKEYCODE_BUTTON_R1:      mask = X_INPUT_GAMEPAD_RIGHT_SHOULDER; break;
    case AKEYCODE_BUTTON_THUMBL:  mask = X_INPUT_GAMEPAD_LEFT_THUMB; break;
    case AKEYCODE_BUTTON_THUMBR:  mask = X_INPUT_GAMEPAD_RIGHT_THUMB; break;
    case AKEYCODE_BUTTON_START:   mask = X_INPUT_GAMEPAD_START; break;
    case AKEYCODE_BUTTON_SELECT:  mask = X_INPUT_GAMEPAD_BACK; break;
    case AKEYCODE_DPAD_UP:        mask = X_INPUT_GAMEPAD_DPAD_UP; break;
    case AKEYCODE_DPAD_DOWN:      mask = X_INPUT_GAMEPAD_DPAD_DOWN; break;
    case AKEYCODE_DPAD_LEFT:      mask = X_INPUT_GAMEPAD_DPAD_LEFT; break;
    case AKEYCODE_DPAD_RIGHT:     mask = X_INPUT_GAMEPAD_DPAD_RIGHT; break;
    default: break;
  }

  if (mask != 0) {
    uint16_t buttons = gamepad_state_.buttons;
    if (is_down) {
      buttons |= mask;
    } else {
      buttons &= ~mask;
    }
    gamepad_state_.buttons = buttons;
  }
}

void AndroidInputDriver::HandleGamepadMotionEvent(const AInputEvent* event) {
  std::lock_guard<std::mutex> lock(state_mutex_);

  // Left stick
  float lx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
  float ly = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);

  // Right stick
  float rx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
  float ry = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);

  // Triggers
  float lt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0);
  if (lt == 0.0f) {
    lt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_BRAKE, 0);
  }
  float rt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0);
  if (rt == 0.0f) {
    rt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_GAS, 0);
  }

  // Hat (D-Pad)
  float hat_x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
  float hat_y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);

  uint16_t buttons = gamepad_state_.buttons;
  buttons &= ~(X_INPUT_GAMEPAD_DPAD_LEFT | X_INPUT_GAMEPAD_DPAD_RIGHT |
               X_INPUT_GAMEPAD_DPAD_UP | X_INPUT_GAMEPAD_DPAD_DOWN);

  if (hat_x < -0.5f) buttons |= X_INPUT_GAMEPAD_DPAD_LEFT;
  if (hat_x > 0.5f)  buttons |= X_INPUT_GAMEPAD_DPAD_RIGHT;
  if (hat_y < -0.5f) buttons |= X_INPUT_GAMEPAD_DPAD_UP;
  if (hat_y > 0.5f)  buttons |= X_INPUT_GAMEPAD_DPAD_DOWN;

  gamepad_state_.buttons = buttons;
  gamepad_state_.thumb_lx = static_cast<int16_t>(std::clamp(lx, -1.0f, 1.0f) * 32767.0f);
  gamepad_state_.thumb_ly = static_cast<int16_t>(std::clamp(-ly, -1.0f, 1.0f) * 32767.0f); // Invert Y
  gamepad_state_.thumb_rx = static_cast<int16_t>(std::clamp(rx, -1.0f, 1.0f) * 32767.0f);
  gamepad_state_.thumb_ry = static_cast<int16_t>(std::clamp(-ry, -1.0f, 1.0f) * 32767.0f); // Invert Y
  gamepad_state_.left_trigger = static_cast<uint8_t>(std::clamp(lt, 0.0f, 1.0f) * 255.0f);
  gamepad_state_.right_trigger = static_cast<uint8_t>(std::clamp(rt, 0.0f, 1.0f) * 255.0f);
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
