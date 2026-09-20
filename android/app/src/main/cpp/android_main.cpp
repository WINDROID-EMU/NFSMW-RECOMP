#include <android_native_app_glue.h>
#include <android/log.h>
#include <android/native_window.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/thread.h>
#include <rex/filesystem.h>
#include <rex/ui/windowed_app.h>

#include "ui/android_app_context.h"
#include "ui/android_window.h"
#include "audio/aaudio_driver.h"
#include "input/android_input_driver.h"
#include "vfs/android_storage.h"
#include "nfsmw_android_app.h"

#define TAG "NFS-MainAndroid"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

struct AppState {
  struct android_app* app = nullptr;
  rex::ui::AndroidWindowedAppContext* context = nullptr;
  rex::input::android::AndroidInputDriver* input_driver = nullptr;
  rex::ui::AndroidWindow* active_window = nullptr;
  bool window_ready = false;
};

static AppState g_app_state;

void OnAppCmd(struct android_app* app, int32_t cmd) {
  auto* state = static_cast<AppState*>(app->userData);

  switch (cmd) {
    case APP_CMD_INIT_WINDOW:
      LOGI("APP_CMD_INIT_WINDOW received: window=%p", app->window);
      if (app->window != nullptr) {
        state->window_ready = true;
        if (state->context) {
          state->context->SetNativeWindow(app->window);
        }
        auto* active_win = rex::ui::AndroidWindow::GetActiveWindow();
        if (active_win) {
          active_win->AttachNativeWindow(app->window);
        }
        if (state->input_driver) {
          int32_t w = ANativeWindow_getWidth(app->window);
          int32_t h = ANativeWindow_getHeight(app->window);
          state->input_driver->SetScreenDimensions(w, h);
        }
      }
      break;

    case APP_CMD_TERM_WINDOW:
      LOGI("APP_CMD_TERM_WINDOW received");
      state->window_ready = false;
      {
        auto* active_win = rex::ui::AndroidWindow::GetActiveWindow();
        if (active_win) {
          active_win->DetachNativeWindow();
        }
      }
      if (state->context) {
        state->context->SetNativeWindow(nullptr);
      }
      break;

    case APP_CMD_WINDOW_RESIZED:
      LOGI("APP_CMD_WINDOW_RESIZED received");
      if (app->window != nullptr) {
        auto* active_win = rex::ui::AndroidWindow::GetActiveWindow();
        if (active_win) {
          int32_t w = ANativeWindow_getWidth(app->window);
          int32_t h = ANativeWindow_getHeight(app->window);
          active_win->UpdateDimensions(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        }
        if (state->input_driver) {
          int32_t w = ANativeWindow_getWidth(app->window);
          int32_t h = ANativeWindow_getHeight(app->window);
          state->input_driver->SetScreenDimensions(w, h);
        }
      }
      break;

    case APP_CMD_GAINED_FOCUS:
      LOGI("APP_CMD_GAINED_FOCUS received");
      break;

    case APP_CMD_LOST_FOCUS:
      LOGI("APP_CMD_LOST_FOCUS received");
      break;

    case APP_CMD_DESTROY:
      LOGI("APP_CMD_DESTROY received");
      if (state->context) {
        state->context->PlatformQuitFromUIThread();
      }
      break;

    default:
      break;
  }
}

int32_t OnInputEvent(struct android_app* app, AInputEvent* event) {
  auto* state = static_cast<AppState*>(app->userData);
  if (state && state->input_driver) {
    if (state->input_driver->HandleInputEvent(event)) {
      return 1;
    }
  }
  return 0;
}

}  // namespace

void android_main(struct android_app* state) {
  LOGI("=== Need for Speed: Most Wanted (Pure Android NDK) Starting ===");

  g_app_state.app = state;
  state->userData = &g_app_state;
  state->onAppCmd = OnAppCmd;
  state->onInputEvent = OnInputEvent;

  // Initialize Android system hooks for memory, filesystem and threads
  rex::memory::AndroidInitialize();
  rex::thread::AndroidInitialize();
  rex::filesystem::AndroidInitialize();

  // Initialize CVars before setting flags
  char* dummy_argv[] = { const_cast<char*>("nfsmw"), nullptr };
  rex::cvar::Init(1, dummy_argv);

  // Storage initialization
  const char* internal_path = state->activity->internalDataPath ? state->activity->internalDataPath : "";
  const char* external_path = state->activity->externalDataPath ? state->activity->externalDataPath : "";
  rex::vfs::android::AndroidStorage::Initialize(internal_path, external_path);

  // Set default baseline flags for Android mobile platform
  rex::cvar::SetFlagByName("gpu_backend", "vulkan");
  rex::cvar::SetFlagByName("gpu_plugin", "xenos");
  rex::cvar::SetFlagByName("render_target_path_d3d12", "rtv");
  rex::cvar::SetFlagByName("readback_resolve", "fast");
  rex::cvar::SetFlagByName("async_shader_compilation", "false");
  rex::cvar::SetFlagByName("vulkan_pipeline_creation_threads", "4");
  rex::cvar::SetFlagByName("store_shaders", "true");
  rex::cvar::SetFlagByName("vsync", "true");
  rex::cvar::SetFlagByName("mnk_mode", "false");
  rex::cvar::SetFlagByName("video_mode_width", "1920");
  rex::cvar::SetFlagByName("video_mode_height", "1080");
  rex::cvar::SetFlagByName("resolution_scale", "1");

  // Adreno performance & logging optimizations
  rex::cvar::SetFlagByName("vulkan_validation_enabled", "false");
  rex::cvar::SetFlagByName("vulkan_log_debug_messages", "false");
  rex::cvar::SetFlagByName("gpu_allow_invalid_fetch_constants", "true");
  rex::cvar::SetFlagByName("log_level", "info");
  rex::cvar::SetFlagByName("log_file", "");
  rex::cvar::SetFlagByName("log_noisy", "false");
  rex::cvar::SetFlagByName("log_verbose", "false");

  rex::InitLoggingEarly();

  // Create UI app context
  rex::ui::AndroidWindowedAppContext app_context(state);
  if (!app_context.Initialize()) {
    LOGE("Failed to initialize AndroidWindowedAppContext");
    return;
  }
  g_app_state.context = &app_context;

  // Wait until ANativeWindow is created by the Android OS
  LOGI("Waiting for native window creation...");
  while (!g_app_state.window_ready && !state->destroyRequested) {
    if (!app_context.PumpEvents(16)) {
      break;
    }
  }

  if (state->destroyRequested) {
    LOGI("Destroy requested before window creation");
    return;
  }

  LOGI("ANativeWindow acquired (%p). Initializing game application...", state->window);

  // Create game instance
  std::unique_ptr<rex::ui::WindowedApp> app = rex::android::NfsmwAndroidApp::Create(app_context);

  if (!app->OnInitialize()) {
    LOGE("NfsmwAndroidApp::OnInitialize failed!");
    return;
  }

  LOGI("NfsmwAndroidApp initialized successfully. Entering game loop.");

  // Main native event pump loop
  while (app_context.PumpEvents(0)) {
    auto* active_win = rex::ui::AndroidWindow::GetActiveWindow();
    if (active_win) {
      active_win->PaintFrame();
    }
  }

  LOGI("Exiting game loop, shutting down application...");
  app->InvokeOnDestroy();
  app.reset();

  rex::filesystem::AndroidShutdown();
  rex::thread::AndroidShutdown();
  rex::memory::AndroidShutdown();

  LOGI("=== NFSMW Cleanly Terminated ===");
}
