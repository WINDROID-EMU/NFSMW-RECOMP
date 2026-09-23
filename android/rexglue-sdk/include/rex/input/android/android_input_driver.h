/**
 * @file        input/android/android_input_driver.h
 * @brief       Direct Android NDK / AInputEvent input driver
 *
 * @copyright   Copyright (c) 2026 ReXGlue Authors
 * @license     BSD 3-Clause License
 */

#pragma once

#include <atomic>
#include <mutex>
#include <vector>

#include <android/input.h>
#include <android/keycodes.h>

#include <rex/input/input_driver.h>

namespace rex::input::android {

class AndroidInputDriver : public InputDriver {
 public:
  explicit AndroidInputDriver(rex::ui::Window* window, size_t window_z_order = 0);
  ~AndroidInputDriver() override;

  X_STATUS Setup() override;

  void EnumerateDevices(std::vector<DeviceInfo>& out) override;

  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override;
  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                 X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags,
                              X_INPUT_KEYSTROKE* out_keystroke) override;

  // Direct Android event dispatch methods
  bool HandleAInputEvent(const AInputEvent* event);
  void HandleKeyEvent(int32_t action, int32_t key_code);
  void HandleMotionEvent(int32_t axis, float value);

  // Direct programmatic control (e.g. for touch virtual gamepads)
  void SetButtonState(uint16_t button_mask, bool pressed);
  void SetTrigger(bool is_right, uint8_t value);
  void SetThumb(bool is_right, int16_t x, int16_t y);

  static AndroidInputDriver* GetInstance() { return instance_; }

 private:
  static AndroidInputDriver* instance_;

  alignas(64) std::atomic<uint32_t> packet_number_{0};
  std::atomic<uint16_t> buttons_{0};
  std::atomic<uint8_t> left_trigger_{0};
  std::atomic<uint8_t> right_trigger_{0};
  std::atomic<int16_t> thumb_lx_{0};
  std::atomic<int16_t> thumb_ly_{0};
  std::atomic<int16_t> thumb_rx_{0};
  std::atomic<int16_t> thumb_ry_{0};

  DeviceId device_id_ = static_cast<DeviceId>(1);
};

}  // namespace rex::input::android
