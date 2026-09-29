// NFSMW Recompiled - la app en Android
//
// Lo mismo que app/src/main.cpp, pero incluyendo la cabecera generada por su
// nombre: en Android el codigo generado vive en app/generated-android/default,
// que CMakeLists.txt pone en la ruta de includes.

#include "nfsmw_init.h"

#include "nfsmw_app.h"

#include "audio/aaudio_driver.h"

class AndroidNfsmwApp : public NfsmwApp {
 public:
  using NfsmwApp::NfsmwApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<AndroidNfsmwApp>(new AndroidNfsmwApp(ctx, "nfsmw", PPCImageConfig));
  }

 protected:
  void OnPreSetup(rex::RuntimeConfig& config) override {
    // LLamar al padre si hace algo, aunque NfsmwApp::OnPreSetup no está.
    // Reemplazamos la factoria de audio por AAudio
    config.audio_factory = REX_AUDIO_BACKEND(rex::audio::android::AndroidAAudioSystem);
  }
};

REX_DEFINE_APP(nfsmw, AndroidNfsmwApp::Create)
