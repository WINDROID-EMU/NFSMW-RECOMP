#include <android_native_app_glue.h>
#include <android/log.h>
#include <android/native_window.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/thread.h>
#include <rex/filesystem.h>
#include <rex/ui/windowed_app.h>
#include <sys/resource.h>

#include "ui/android_app_context.h"
#include "ui/android_window.h"
#include "audio/aaudio_driver.h"
#include "input/android_input_driver.h"
#include "vfs/android_storage.h"
#include "nfsmw_android_app.h"
#include "vulkan_driver_loader.h"

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

  // ── Turnip / Mesa custom Vulkan driver ────────────────────────────────────
  // Must run before ANY Vulkan call or SDK init. If libadrenotools.so is
  // bundled in the APK and the user placed libvulkan_freedreno.so inside
  // <internalDataPath>/turnip/, the stock driver is replaced transparently.
  // Falls back silently to the system driver if anything is missing.
  {
    bool turnip_loaded = nfsmw::android::TryLoadCustomVulkanDriver(state);
    if (turnip_loaded) {
      LOGI("[Vulkan] Turnip/Mesa driver active — shaderInt64 + BDA guaranteed");
    } else {
      LOGI("[Vulkan] Using stock system driver (Turnip not installed or not available)");
    }
  }

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

  // Prioritize main game thread on Android to ensure scheduling on performance cores (Cortex-X2/A710)
  setpriority(PRIO_PROCESS, 0, -8);

  // Initialize CVars before setting flags
  char* dummy_argv[] = { const_cast<char*>("nfsmw"), nullptr };
  rex::cvar::Init(1, dummy_argv);

  // Storage initialization
  const char* internal_path = state->activity->internalDataPath ? state->activity->internalDataPath : "";
  const char* external_path = state->activity->externalDataPath ? state->activity->externalDataPath : "";
  rex::vfs::android::AndroidStorage::Initialize(internal_path, external_path);

  // Optional user overrides (nfsmw.toml in storage directory) loaded before defaults
  if (external_path && external_path[0]) {
    rex::cvar::LoadConfig(std::filesystem::path(external_path) / "nfsmw.toml");
  }
  if (internal_path && internal_path[0]) {
    rex::cvar::LoadConfig(std::filesystem::path(internal_path) / "nfsmw.toml");
  }

  // Set default baseline flags for Android mobile platform only if not already specified by config
  auto SetDefaultFlag = [](std::string_view name, std::string_view val) {
    if (rex::cvar::GetFlagSource(name) == rex::cvar::Source::kDefault) {
      rex::cvar::SetFlagByName(name, val);
    }
  };

  SetDefaultFlag("gpu_backend", "vulkan");
  SetDefaultFlag("gpu_plugin", "xenos");
  SetDefaultFlag("render_target_path_d3d12", "rtv");
  SetDefaultFlag("render_target_path_vulkan", "fbo");
  SetDefaultFlag("readback_resolve", "fast");
  SetDefaultFlag("async_shader_compilation", "true");
  SetDefaultFlag("vulkan_async_skip_incomplete_frames", "false");
  SetDefaultFlag("vulkan_submit_on_primary_buffer_end", "false");
  SetDefaultFlag("vulkan_dynamic_rendering", "true");
  SetDefaultFlag("native_2x_msaa", "false");
  SetDefaultFlag("gamma_render_target_as_unorm16", "false");
  SetDefaultFlag("depth_transfer_not_equal_test", "false");
  SetDefaultFlag("clear_memory_page_state", "false");
  // Mobile devices have less unified memory bandwidth than desktop GPUs.
  // Android limits are kept below desktop defaults (24/384 MB), not above them.
  // Previous values (96/512 MB) were backwards and caused memory contention.
  SetDefaultFlag("texture_cache_memory_limit_render_to_texture", "16");
  SetDefaultFlag("texture_cache_memory_limit_soft", "256");
  SetDefaultFlag("vulkan_pipeline_creation_threads", "2");
  SetDefaultFlag("store_shaders", "true");
  SetDefaultFlag("vsync", "true");
  SetDefaultFlag("mnk_mode", "false");
  SetDefaultFlag("present_letterbox", "false");

  // 1280x720 Native Xbox 360 resolution fits within Adreno 650/730 GMEM on-chip tile memory
  SetDefaultFlag("video_mode_width", "1280");
  SetDefaultFlag("video_mode_height", "720");
  SetDefaultFlag("resolution_scale", "1");
  SetDefaultFlag("anisotropic_override", "1");

  // Adreno performance & logging optimizations
  SetDefaultFlag("query_occlusion_fake_sample_count", "1");
  SetDefaultFlag("primitive_processor_cache_min_indices", "-1");
  SetDefaultFlag("vulkan_validation_enabled", "false");
  SetDefaultFlag("vulkan_log_debug_messages", "false");
  SetDefaultFlag("gpu_allow_invalid_fetch_constants", "true");
  SetDefaultFlag("log_level", "info");
  SetDefaultFlag("log_file", "");
  SetDefaultFlag("log_noisy", "false");
  SetDefaultFlag("log_verbose", "false");
  SetDefaultFlag("protect_zero", "false");

  LOGI("Effective GPU plugin cvar: %s", rex::cvar::GetFlagByName("gpu_plugin").c_str());

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
