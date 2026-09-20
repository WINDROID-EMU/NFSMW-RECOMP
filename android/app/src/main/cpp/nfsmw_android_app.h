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

  void OnConfigurePaths(rex::PathConfig& paths) override {
    rex::vfs::android::AndroidStorage::ConfigureAppPaths(paths);
  }
};

}  // namespace rex::android
