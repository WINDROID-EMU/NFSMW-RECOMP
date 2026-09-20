#pragma once

#include <rex/ui/windowed_app_context.h>
#include <android_native_app_glue.h>

namespace rex::ui {

class AndroidWindowedAppContext final : public WindowedAppContext {
 public:
  explicit AndroidWindowedAppContext(struct android_app* app);
  ~AndroidWindowedAppContext() override;

  bool Initialize();

  struct android_app* app() const { return app_; }

  void SetNativeWindow(ANativeWindow* window);
  ANativeWindow* GetNativeWindow() const { return native_window_; }

  void NotifyUILoopOfPendingFunctions() override;
  void PlatformQuitFromUIThread() override;

  // Runs one step of the native event pump, executing pending functions.
  // Returns true if the application should keep running, false if quit requested.
  bool PumpEvents(int timeout_millis = 0);

 private:
  struct android_app* app_ = nullptr;
  ANativeWindow* native_window_ = nullptr;
  bool quit_requested_ = false;
};

}  // namespace rex::ui
