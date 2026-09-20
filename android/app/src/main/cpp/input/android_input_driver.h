#pragma once

#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <android/input.h>
#include "touch_overlay.h"

#include <mutex>
#include <vector>

namespace rex::input::android {

class AndroidInputDriver final : public InputDriver {
 public:
  explicit AndroidInputDriver(rex::ui::Window* window);
  ~AndroidInputDriver() override;

  X_STATUS Setup() override;
  void EnumerateDevices(std::vector<DeviceInfo>& out) override;
  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override;
  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t flags, X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags, X_INPUT_KEYSTROKE* out_keystroke) override;

  bool HandleInputEvent(const AInputEvent* event);
  void SetScreenDimensions(uint32_t width, uint32_t height);

 private:
  std::mutex state_mutex_;
  uint32_t packet_number_ = 0;
  X_INPUT_GAMEPAD gamepad_state_{};
  TouchOverlay touch_overlay_;

  // Handles physical gamepad motion events (analog sticks & triggers)
  void HandleGamepadMotionEvent(const AInputEvent* event);
  // Handles physical gamepad key events (face buttons, d-pad, shoulders)
  void HandleGamepadKeyEvent(const AInputEvent* event);
  // Handles touch events
  void HandleTouchEvent(const AInputEvent* event);
};

class AndroidInputSystem final : public InputSystem {
 public:
  explicit AndroidInputSystem(rex::ui::Window* window);
  ~AndroidInputSystem() override;

  static std::unique_ptr<system::IInputSystem> Create(bool tool_mode);
  AndroidInputDriver* driver() const { return driver_; }

 private:
  AndroidInputDriver* driver_ = nullptr;
};

}  // namespace rex::input::android
