/**
 * @file        ui/windowed_app_main_android.cpp
 * @brief       Direct Android NDK NativeActivity / android_main entry point
 *
 * @copyright   Copyright (c) 2026 ReXGlue Authors
 * @license     BSD 3-Clause License
 */

#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/input/android/android_input_driver.h>
#include <rex/platform/android/android_bridge.h>
#include <rex/ui/window_android.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context_android.h>

namespace {

void HandleAppCommand(struct android_app* app, int32_t cmd) {
  auto* context = static_cast<rex::ui::AndroidWindowedAppContext*>(app->userData);
  if (!context) return;

  auto* window = context->GetWindow();

  switch (cmd) {
    case APP_CMD_INIT_WINDOW:
      if (app->window && window) {
        window->SetNativeWindow(app->window);
      }
      break;
    case APP_CMD_TERM_WINDOW:
      if (window) {
        window->SetNativeWindow(nullptr);
      }
      break;
    case APP_CMD_DESTROY:
      context->QuitMessageLoop();
      break;
    default:
      break;
  }
}

int32_t HandleInputEvent(struct android_app* /*app*/, AInputEvent* event) {
  auto* input_driver = rex::input::android::AndroidInputDriver::GetInstance();
  if (input_driver && input_driver->HandleAInputEvent(event)) {
    return 1;
  }
  return 0;
}

}  // namespace

extern "C" void android_main(struct android_app* state) {
  rex::InitLoggingEarly();

  rex::ui::AndroidWindowedAppContext app_context;
  if (!app_context.Initialize()) {
    REXLOG_ERROR("Failed to initialize AndroidWindowedAppContext");
    return;
  }

  state->userData = &app_context;
  state->onAppCmd = HandleAppCommand;
  state->onInputEvent = HandleInputEvent;

  std::unique_ptr<rex::ui::WindowedApp> app = rex::ui::GetWindowedAppCreator()(app_context);
  if (!app || !app->OnInitialize()) {
    REXLOG_ERROR("Failed to initialize WindowedApp");
    return;
  }

  // Native message loop handling OS events and frame presentation
  while (!state->destroyRequested) {
    int ident;
    int events;
    struct android_poll_source* source = nullptr;

    while ((ident = ALooper_pollOnce(0, nullptr, &events, reinterpret_cast<void**>(&source))) >= 0) {
      if (source) {
        source->process(state, source);
      }
      if (state->destroyRequested) {
        break;
      }
    }

    app_context.ExecutePendingFunctionsFromUIThread();

    if (auto* window = app_context.GetWindow()) {
      window->HandleFrameTick();
    }
  }

  app->InvokeOnDestroy();
}
