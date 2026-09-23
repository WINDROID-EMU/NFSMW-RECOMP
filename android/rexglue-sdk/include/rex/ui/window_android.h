/**
 * @file        ui/window_android.h
 * @brief       Direct Android NDK Window implementation wrapping ANativeWindow
 *
 * @copyright   Copyright (c) 2026 ReXGlue Authors
 * @license     BSD 3-Clause License
 */

#pragma once

#include <atomic>
#include <memory>
#include <string_view>

#include <android/choreographer.h>
#include <android/native_window.h>

#include <rex/ui/surface_android.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context_android.h>

namespace rex::ui {

class WindowAndroid final : public Window {
 public:
  WindowAndroid(WindowedAppContext& app_context, const std::string_view title,
                uint32_t desired_logical_width, uint32_t desired_logical_height);
  ~WindowAndroid() override;

  void* GetNativeWindowHandle() const override;
  bool SetRelativeMouseMode(bool enable) override;
  bool WarpMouseToCenter(int32_t& x_out, int32_t& y_out) override;

  void SetNativeWindow(ANativeWindow* window);
  ANativeWindow* GetNativeWindow() const { return native_window_; }

  void HandleFrameTick();

 protected:
  uint32_t GetLatestDpiImpl() const override;

  bool OpenImpl() override;
  void RequestCloseImpl() override;

  void ApplyNewFullscreen() override;
  void ApplyNewTitle() override;
  void ApplyNewMouseCapture() override;
  void ApplyNewMouseRelease() override;
  void ApplyNewCursorVisibility(CursorVisibility old_cursor_visibility) override;
  void ApplyNewTextInputActive() override;
  void FocusImpl() override;

  std::unique_ptr<Surface> CreateSurfaceImpl(Surface::TypeFlags allowed_types) override;
  void RequestPaintImpl() override;

 private:
  static void ChoreographerCallback(long frame_time_nanos, void* data);

  AndroidWindowedAppContext& android_app_context() const {
    return static_cast<AndroidWindowedAppContext&>(app_context());
  }

  ANativeWindow* native_window_ = nullptr;
  AChoreographer* choreographer_ = nullptr;
  std::atomic<bool> paint_requested_ = false;
  std::atomic<bool> choreographer_scheduled_ = false;
};

}  // namespace rex::ui
