#include "touch_overlay.h"
#include <algorithm>
#include <cmath>

namespace rex::input::android {

void TouchOverlay::SetScreenSize(uint32_t width, uint32_t height) {
  if (width > 0 && height > 0) {
    screen_width_ = width;
    screen_height_ = height;
  }
}

void TouchOverlay::ProcessPointerDown(int pointer_id, float x, float y) {
  for (auto& pt : pointers_) {
    if (!pt.active) {
      pt.id = pointer_id;
      pt.x = x;
      pt.y = y;
      pt.active = true;
      break;
    }
  }
  EvaluatePointers();
}

void TouchOverlay::ProcessPointerMove(int pointer_id, float x, float y) {
  for (auto& pt : pointers_) {
    if (pt.active && pt.id == pointer_id) {
      pt.x = x;
      pt.y = y;
      break;
    }
  }
  EvaluatePointers();
}

void TouchOverlay::ProcessPointerUp(int pointer_id) {
  for (auto& pt : pointers_) {
    if (pt.active && pt.id == pointer_id) {
      pt.active = false;
      pt.id = -1;
      break;
    }
  }
  EvaluatePointers();
}

void TouchOverlay::EvaluatePointers() {
  steer_axis_ = 0.0f;
  gas_ = 0.0f;
  brake_ = 0.0f;
  handbrake_ = false;
  nitro_ = false;
  speedbreaker_ = false;
  pause_ = false;

  const float w = static_cast<float>(screen_width_);
  const float h = static_cast<float>(screen_height_);

  for (const auto& pt : pointers_) {
    if (!pt.active) continue;

    const float nx = pt.x / w;
    const float ny = pt.y / h;

    // 1. Left side of screen (Steering)
    if (nx < 0.45f) {
      // Virtual steering center around nx = 0.20f, ny = 0.75f
      constexpr float center_x = 0.20f;
      float delta_x = (nx - center_x) / 0.18f;
      delta_x = std::clamp(delta_x, -1.0f, 1.0f);
      steer_axis_ = delta_x;
    }

    // 2. Pause button (Top Right)
    if (nx > 0.88f && ny < 0.20f) {
      pause_ = true;
    }

    // 3. Right side pedals and buttons (Driving controls)
    // Gas (RT) - Bottom far right
    if (nx > 0.82f && ny > 0.58f) {
      gas_ = 1.0f;
    }
    // Brake/Reverse (LT) - Bottom center-right
    else if (nx > 0.65f && nx <= 0.82f && ny > 0.58f) {
      brake_ = 1.0f;
    }
    // Handbrake (B) - Mid far right
    else if (nx > 0.82f && ny > 0.32f && ny <= 0.58f) {
      handbrake_ = true;
    }
    // Nitro (A / LB) - Mid center-right
    else if (nx > 0.65f && nx <= 0.82f && ny > 0.32f && ny <= 0.58f) {
      nitro_ = true;
    }
    // Speedbreaker (Down on Dpad) - Center bottom
    else if (nx > 0.45f && nx <= 0.65f && ny > 0.70f) {
      speedbreaker_ = true;
    }
  }
}

void TouchOverlay::UpdateGamepadState(X_INPUT_GAMEPAD& gamepad) {
  // Merge touch inputs into the gamepad struct
  uint16_t buttons = gamepad.buttons;

  if (handbrake_) {
    buttons |= X_INPUT_GAMEPAD_B;
  }
  if (nitro_) {
    buttons |= X_INPUT_GAMEPAD_A;
  }
  if (speedbreaker_) {
    buttons |= X_INPUT_GAMEPAD_LEFT_THUMB;
    buttons |= X_INPUT_GAMEPAD_DPAD_DOWN;
  }
  if (pause_) {
    buttons |= X_INPUT_GAMEPAD_START;
  }

  gamepad.buttons = buttons;

  // Steering maps to left analog stick X
  if (std::abs(steer_axis_) > 0.05f) {
    int16_t thumb_x = static_cast<int16_t>(steer_axis_ * 32767.0f);
    gamepad.thumb_lx = thumb_x;
  }

  // Gas maps to Right Trigger (0 - 255)
  if (gas_ > 0.0f) {
    gamepad.right_trigger = std::max(gamepad.right_trigger, static_cast<uint8_t>(gas_ * 255.0f));
  }

  // Brake maps to Left Trigger (0 - 255)
  if (brake_ > 0.0f) {
    gamepad.left_trigger = std::max(gamepad.left_trigger, static_cast<uint8_t>(brake_ * 255.0f));
  }
}

}  // namespace rex::input::android
