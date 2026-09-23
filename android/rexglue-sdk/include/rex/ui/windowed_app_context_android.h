/**
 * @file        ui/windowed_app_context_android.h
 * @brief       Direct Android NDK native UI loop context
 *
 * @copyright   Copyright (c) 2026 ReXGlue Authors
 * @license     BSD 3-Clause License
 */

#pragma once

#include <atomic>
#include <mutex>
#include <vector>

#include <android/looper.h>
#include <android/native_window.h>

#include <rex/ui/windowed_app_context.h>

namespace rex::ui {

class WindowAndroid;

class AndroidWindowedAppContext final : public WindowedAppContext {
 public:
  AndroidWindowedAppContext();
  ~AndroidWindowedAppContext() override;

  bool Initialize();

  int RunMainMessageLoop();
  void QuitMessageLoop();

  void RegisterWindow(WindowAndroid* window);
  void UnregisterWindow(WindowAndroid* window);
  WindowAndroid* GetWindow() const { return window_; }

 protected:
  void NotifyUILoopOfPendingFunctions() override;
  void PlatformQuitFromUIThread() override;

 private:
  static int LooperCallback(int fd, int events, void* data);

  int wakeup_event_fd_ = -1;
  ALooper* looper_ = nullptr;
  std::atomic<bool> quit_requested_ = false;
  WindowAndroid* window_ = nullptr;
};

}  // namespace rex::ui
