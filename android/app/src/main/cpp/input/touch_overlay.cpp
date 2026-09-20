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

bool TouchOverlay::IsPointInButton(float px, float py, const VirtualButton& btn) const {
  const float bx = btn.nx * static_cast<float>(screen_width_);
  const float by = btn.ny * static_cast<float>(screen_height_);
  const float radius = btn.radius_rel * static_cast<float>(screen_height_);
  const float dx = px - bx;
  const float dy = py - by;
  return (dx * dx + dy * dy) <= (radius * radius);
}

void TouchOverlay::EvaluatePointers() {
  steer_axis_ = 0.0f;
  steer_left_ = false;
  steer_right_ = false;
  gas_ = 0.0f;
  brake_ = 0.0f;
  handbrake_ = false;
  nitro_ = false;
  speedbreaker_ = false;
  pause_ = false;
  reset_car_ = false;
  camera_ = false;

  const float w = static_cast<float>(screen_width_);
  const float h = static_cast<float>(screen_height_);

  for (const auto& pt : pointers_) {
    if (!pt.active) continue;

    const float px = pt.x;
    const float py = pt.y;
    const float nx = px / w;
    const float ny = py / h;

    // 1. Top Utility Controls
    if (IsPointInButton(px, py, layout_.pause)) {
      pause_ = true;
      continue;
    }
    if (IsPointInButton(px, py, layout_.camera)) {
      camera_ = true;
      continue;
    }
    if (IsPointInButton(px, py, layout_.reset_car)) {
      reset_car_ = true;
      continue;
    }

    // 2. Right Driving Cluster
    if (IsPointInButton(px, py, layout_.gas)) {
      gas_ = 1.0f;
    } else if (IsPointInButton(px, py, layout_.brake)) {
      brake_ = 1.0f;
    } else if (IsPointInButton(px, py, layout_.nitro)) {
      nitro_ = true;
    } else if (IsPointInButton(px, py, layout_.handbrake)) {
      handbrake_ = true;
    } else if (IsPointInButton(px, py, layout_.speedbreaker)) {
      speedbreaker_ = true;
    }

    // 3. Left Steering Cluster
    if (IsPointInButton(px, py, layout_.steer_left)) {
      steer_left_ = true;
      steer_axis_ = -1.0f;
    } else if (IsPointInButton(px, py, layout_.steer_right)) {
      steer_right_ = true;
      steer_axis_ = 1.0f;
    } else if (nx < 0.38f && ny > 0.45f) {
      // Proportional steering if sliding between/around the steering buttons
      const float mid_x = (layout_.steer_left.nx + layout_.steer_right.nx) * 0.5f;
      float delta_x = (nx - mid_x) / 0.14f;
      delta_x = std::clamp(delta_x, -1.0f, 1.0f);
      steer_axis_ = delta_x;
      if (steer_axis_ < -0.20f) {
        steer_left_ = true;
      } else if (steer_axis_ > 0.20f) {
        steer_right_ = true;
      }
    }
  }
}

void TouchOverlay::UpdateGamepadState(X_INPUT_GAMEPAD& gamepad) {
  if (!enabled_) {
    return;
  }

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
  if (reset_car_) {
    buttons |= X_INPUT_GAMEPAD_BACK;
  }
  if (camera_) {
    buttons |= X_INPUT_GAMEPAD_RIGHT_SHOULDER;
  }
  if (steer_left_) {
    buttons |= X_INPUT_GAMEPAD_DPAD_LEFT;
  }
  if (steer_right_) {
    buttons |= X_INPUT_GAMEPAD_DPAD_RIGHT;
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
