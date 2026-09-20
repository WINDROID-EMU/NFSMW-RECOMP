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

class TouchOverlay {
 public:
  TouchOverlay() = default;

  void SetScreenSize(uint32_t width, uint32_t height);
  void ProcessPointerDown(int pointer_id, float x, float y);
  void ProcessPointerMove(int pointer_id, float x, float y);
  void ProcessPointerUp(int pointer_id);

  // Updates the virtual controller state to be merged into X_INPUT_GAMEPAD
  void UpdateGamepadState(X_INPUT_GAMEPAD& gamepad);

 private:
  uint32_t screen_width_ = 1920;
  uint32_t screen_height_ = 1080;

  static constexpr int kMaxPointers = 10;
  TouchPoint pointers_[kMaxPointers];

  // Calculated control states
  float steer_axis_ = 0.0f; // -1.0f (left) to 1.0f (right)
  float gas_ = 0.0f;        // 0.0f to 1.0f (RT)
  float brake_ = 0.0f;      // 0.0f to 1.0f (LT)
  bool handbrake_ = false;  // Button B
  bool nitro_ = false;      // Button A
  bool speedbreaker_ = false; // Left Thumb Click / DPad Down
  bool pause_ = false;      // Start Button
  bool reset_car_ = false;  // Back / Select

  void EvaluatePointers();
};

}  // namespace rex::input::android
