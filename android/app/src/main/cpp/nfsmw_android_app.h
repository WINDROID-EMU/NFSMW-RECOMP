#pragma once

#if __has_include("generated/default/nfsmw_init.h")
#include "generated/default/nfsmw_init.h"
#elif __has_include("app/generated/default/nfsmw_init.h")
#include "app/generated/default/nfsmw_init.h"
#else
#include <rex/ppc/function.h>
extern const rex::PPCImageInfo PPCImageConfig;
#endif

#include "src/nfsmw_app.h"
#include "audio/aaudio_driver.h"
#include "input/android_input_driver.h"
#include "vfs/android_storage.h"

#include <rex/system/kernel_state.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xmemory.h>

struct PPCContext;
extern "C" void rexcrt_memset(PPCContext& ctx, uint8_t* base);
extern "C" void rexcrt_memcpy(PPCContext& ctx, uint8_t* base);

namespace rex::android {

class NfsmwAndroidApp final : public NfsmwApp {
 public:
  using NfsmwApp::NfsmwApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<NfsmwAndroidApp>(new NfsmwAndroidApp(ctx, "nfsmw", PPCImageConfig));
  }

 protected:
  void OnPreSetup(rex::RuntimeConfig& config) override {
    // Override audio and input factories to use pure Android NDK backends (Zero SDL)
    config.audio_factory = REX_AUDIO_BACKEND(rex::audio::android::AndroidAAudioSystem);
    config.input_factory = REX_INPUT_BACKEND(rex::input::android::AndroidInputSystem::Create);
  }

  void OnPostSetup() override {
    NfsmwApp::OnPostSetup();

    // Explicitly enforce readback_resolve as none on mobile to avoid GPU pipeline stalls
    rex::cvar::SetFlagByName("readback_resolve", "fast");

    auto* kernel = rex::system::kernel_state();
    if (kernel) {
      // Fast-path: Replace emulated PowerPC memset / memcpy loops with native ARM64 NEON
      if (kernel->function_dispatcher()) {
        kernel->function_dispatcher()->SetFunction(0x826BE610, rexcrt_memset);
        kernel->function_dispatcher()->SetFunction(0x826BE1B0, rexcrt_memcpy);
      }
      if (kernel->memory()) {
        kernel->memory()->SetFunction(0x826BE610, rexcrt_memset);
        kernel->memory()->SetFunction(0x826BE1B0, rexcrt_memcpy);
      }
      __android_log_print(ANDROID_LOG_INFO, "NFS-FastPath",
                          "Hooked 0x826BE610 (memset) & 0x826BE1B0 (memcpy) -> host native ARM64 NEON");
    }
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    rex::vfs::android::AndroidStorage::ConfigureAppPaths(paths);
  }
};

}  // namespace rex::android
