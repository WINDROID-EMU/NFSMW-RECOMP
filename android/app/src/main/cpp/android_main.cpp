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
        auto* driver = rex::input::android::AndroidInputDriver::GetActiveDriver();
        if (driver) {
          int32_t w = ANativeWindow_getWidth(app->window);
          int32_t h = ANativeWindow_getHeight(app->window);
          driver->SetScreenDimensions(w, h);
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
        auto* driver = rex::input::android::AndroidInputDriver::GetActiveDriver();
        if (driver) {
          int32_t w = ANativeWindow_getWidth(app->window);
          int32_t h = ANativeWindow_getHeight(app->window);
          driver->SetScreenDimensions(w, h);
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
  auto* driver = rex::input::android::AndroidInputDriver::GetActiveDriver();
  if (driver && driver->HandleInputEvent(event)) {
    return 1;
  }
  return 0;
}

}  // namespace

void android_main(struct android_app* state) {
  LOGI("=== Need for Speed: Most Wanted (Pure Android NDK) Starting ===");

  if (state && state->activity && state->activity->vm && state->activity->clazz) {
    rex::input::android::RegisterVirtualGamepadJNI(state->activity->vm, state->activity->clazz);
  }

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

  // Set default baseline flags for Android mobile platform (Adreno 650 TBR / GMEM optimized)
  rex::cvar::SetFlagByName("gpu_backend", "vulkan");
  rex::cvar::SetFlagByName("gpu_plugin", "xenos");
  rex::cvar::SetFlagByName("render_target_path_d3d12", "rtv");
  rex::cvar::SetFlagByName("render_target_path_vulkan", "fbo");
  rex::cvar::SetFlagByName("readback_resolve", "fast");
  rex::cvar::SetFlagByName("async_shader_compilation", "true");
  rex::cvar::SetFlagByName("vulkan_async_skip_incomplete_frames", "false");
  rex::cvar::SetFlagByName("vulkan_submit_on_primary_buffer_end", "false");
  rex::cvar::SetFlagByName("vulkan_dynamic_rendering", "true");
  rex::cvar::SetFlagByName("native_2x_msaa", "false");
  rex::cvar::SetFlagByName("gamma_render_target_as_unorm16", "false");
  rex::cvar::SetFlagByName("depth_transfer_not_equal_test", "false");
  rex::cvar::SetFlagByName("clear_memory_page_state", "false");
  rex::cvar::SetFlagByName("texture_cache_memory_limit_render_to_texture", "96");
  rex::cvar::SetFlagByName("texture_cache_memory_limit_soft", "512");
  rex::cvar::SetFlagByName("vulkan_pipeline_creation_threads", "2");
  rex::cvar::SetFlagByName("store_shaders", "true");
  rex::cvar::SetFlagByName("vsync", "true");
  rex::cvar::SetFlagByName("mnk_mode", "false");

  // 1280x720 Native Xbox 360 resolution fits within Adreno 650 8MB GMEM on-chip tile memory
  rex::cvar::SetFlagByName("video_mode_width", "1280");
  rex::cvar::SetFlagByName("video_mode_height", "720");
  rex::cvar::SetFlagByName("resolution_scale", "1");
  rex::cvar::SetFlagByName("anisotropic_override", "2");

  // Adreno performance & logging optimizations
  rex::cvar::SetFlagByName("vulkan_validation_enabled", "false");
  rex::cvar::SetFlagByName("vulkan_log_debug_messages", "false");
  rex::cvar::SetFlagByName("gpu_allow_invalid_fetch_constants", "true");
  rex::cvar::SetFlagByName("log_level", "info");
  rex::cvar::SetFlagByName("log_file", "");
  rex::cvar::SetFlagByName("log_noisy", "false");
  rex::cvar::SetFlagByName("log_verbose", "false");
  rex::cvar::SetFlagByName("protect_zero", "false");

  // Optional user overrides (nfsmw.toml in storage directory)
  if (external_path && external_path[0]) {
    rex::cvar::LoadConfig(std::filesystem::path(external_path) / "nfsmw.toml");
  }
  if (internal_path && internal_path[0]) {
    rex::cvar::LoadConfig(std::filesystem::path(internal_path) / "nfsmw.toml");
  }

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
