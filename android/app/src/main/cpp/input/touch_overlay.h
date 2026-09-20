#pragma once

#include <cstdint>
#include <rex/input/input.h>

namespace rex::input::android {

struct TouchPoint {
  int id = -1;
  float x = 0.0f;
  float y = 0.0f;
  bool active = false;
};

struct VirtualButton {
  float nx = 0.0f;        // Normalized X center (0.0 to 1.0)
  float ny = 0.0f;        // Normalized Y center (0.0 to 1.0)
  float radius_rel = 0.0f;// Radius relative to screen height
};

struct VirtualControlLayout {
  VirtualButton steer_left  {0.09f, 0.77f, 0.115f};
  VirtualButton steer_right {0.23f, 0.77f, 0.115f};
  VirtualButton gas         {0.90f, 0.78f, 0.120f};
  VirtualButton brake       {0.76f, 0.78f, 0.115f};
  VirtualButton nitro       {0.90f, 0.50f, 0.095f};
  VirtualButton handbrake   {0.76f, 0.52f, 0.095f};
  VirtualButton speedbreaker{0.64f, 0.65f, 0.085f};
  VirtualButton pause       {0.94f, 0.11f, 0.065f};
  VirtualButton camera      {0.86f, 0.11f, 0.065f};
  VirtualButton reset_car   {0.07f, 0.11f, 0.065f};
};

class TouchOverlay {
 public:
  TouchOverlay() = default;

  void SetScreenSize(uint32_t width, uint32_t height);
  void ProcessPointerDown(int pointer_id, float x, float y);
  void ProcessPointerMove(int pointer_id, float x, float y);
  void ProcessPointerUp(int pointer_id);

  // Updates the virtual controller state to be merged into X_INPUT_GAMEPAD
  void UpdateGamepadState(X_INPUT_GAMEPAD& gamepad);

  // Configuration and visual state getters
  bool IsEnabled() const { return enabled_; }
  void SetEnabled(bool enabled) { enabled_ = enabled; }

  float GetOpacity() const { return opacity_; }
  void SetOpacity(float opacity) { opacity_ = opacity; }

  const VirtualControlLayout& layout() const { return layout_; }

  uint32_t screen_width() const { return screen_width_; }
  uint32_t screen_height() const { return screen_height_; }

  bool IsGasPressed() const { return gas_ > 0.05f; }
  bool IsBrakePressed() const { return brake_ > 0.05f; }
  bool IsHandbrakePressed() const { return handbrake_; }
  bool IsNitroPressed() const { return nitro_; }
  bool IsSpeedbreakerPressed() const { return speedbreaker_; }
  bool IsSteerLeftPressed() const { return steer_left_; }
  bool IsSteerRightPressed() const { return steer_right_; }
  bool IsPausePressed() const { return pause_; }
  bool IsResetCarPressed() const { return reset_car_; }
  bool IsCameraPressed() const { return camera_; }

  float GetSteerAxis() const { return steer_axis_; }
  float GetGas() const { return gas_; }
  float GetBrake() const { return brake_; }

 private:
  uint32_t screen_width_ = 1920;
  uint32_t screen_height_ = 1080;

  bool enabled_ = true;
  float opacity_ = 0.82f;

  VirtualControlLayout layout_{};

  static constexpr int kMaxPointers = 10;
  TouchPoint pointers_[kMaxPointers];

  // Calculated control states
  float steer_axis_ = 0.0f;   // -1.0f (left) to 1.0f (right)
  bool steer_left_ = false;
  bool steer_right_ = false;
  float gas_ = 0.0f;          // 0.0f to 1.0f (RT)
  float brake_ = 0.0f;        // 0.0f to 1.0f (LT)
  bool handbrake_ = false;    // Button B
  bool nitro_ = false;        // Button A
  bool speedbreaker_ = false; // Left Thumb Click / DPad Down
  bool pause_ = false;        // Start Button
  bool reset_car_ = false;    // Back / Select Button
  bool camera_ = false;       // Right Shoulder (RB)

  void EvaluatePointers();
  bool IsPointInButton(float px, float py, const VirtualButton& btn) const;
};

}  // namespace rex::input::android
