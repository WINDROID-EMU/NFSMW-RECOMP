// NFSMW Recompiled - lo que la app de Android le pide al MOTOR NATIVO
//
// Con el motor nativo (-Pnfsmw.motor=nativo) libmain.so es la app de
// nfsmw-android: su codigo generado, su renderizador y su SDK. La entrada
// (SDL_main) la pone ese SDK. Este fichero es lo unico nuestro que entra en
// libmain.so, con sonda_vulkan.cpp y afinidad.cpp: lo que con el motor de
// Xenos hace android_main.cpp.
//
//   1. El mando tactil (TouchControllerBridge.setState).
//   2. Los fps del rotulo (GameActivity.nativeFps / nativeMsPorFotograma).
//   3. Abrir la ISO por su URI content:// (GameActivity.openContentFd), para
//      no copiarla ni pedir acceso a todos los archivos.
//   4. La sonda de Vulkan (--nfsmw_sonda), antes de arrancar el juego.
//   5. Los hilos fijados a nucleos (cvar thread_affinity).
//   6. La salida de audio por nuestro driver AAudio (cvar nfsmw_audio_aaudio)
//      en vez de por el de SDL de su SDK.
//   7. Parar el juego y el audio mientras la app esta en segundo plano.

#include <jni.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_system.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/audio/audio_system.h>
#include <rex/runtime.h>

#include "../afinidad.h"
#include "../audio/aaudio_driver.h"

REXCVAR_DEFINE_BOOL(nfsmw_audio_aaudio, true, "Audio",
                    "Android: sacar el sonido por AAudio (el driver de NFSMW Recompiled) en vez de por "
                    "el de SDL del SDK")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

int NfsmwSondaVulkan(int argc, char** argv);  // ../sonda_vulkan.cpp

// El mando tactil del SDK del motor nativo (sdk/src/input/sdl/sdl_input_driver.cpp):
// el estado va directo al mando del juego, sin pasar por un mando virtual de SDL.
extern "C" void rex_sdl_set_touch_gamepad_state(uint16_t buttons, int16_t left_x, int16_t left_y,
                                                 int16_t right_x, int16_t right_y,
                                                 uint8_t left_trigger, uint8_t right_trigger);

// Fotogramas que ha presentado el juego (app/src/nfsmw_d3d_trace.cpp, en su gancho del Swap).
extern std::atomic<uint64_t> g_nfsmw_fotogramas_juego;

namespace {

// ---------------------------------------------------------------------------
//  1. Mando tactil
// ---------------------------------------------------------------------------

// TouchControllerView manda los botones con el indice de SDL_GamepadButton;
// el SDK los quiere con la mascara de XInput.
constexpr uint16_t kBotonXInput[15] = {
    0x1000,  //  0 A
    0x2000,  //  1 B
    0x4000,  //  2 X
    0x8000,  //  3 Y
    0x0020,  //  4 BACK
    0x0000,  //  5 GUIDE: no existe en el mando del juego
    0x0010,  //  6 START
    0x0040,  //  7 stick izquierdo
    0x0080,  //  8 stick derecho
    0x0100,  //  9 LB
    0x0200,  // 10 RB
    0x0001,  // 11 cruceta arriba
    0x0002,  // 12 cruceta abajo
    0x0004,  // 13 cruceta izquierda
    0x0008,  // 14 cruceta derecha
};

int16_t Eje(float v) {
  return static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
}

uint8_t Gatillo(float v) {
  return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
}

// ---------------------------------------------------------------------------
//  2. Fps
// ---------------------------------------------------------------------------

float g_fps = -1.0f;

// Java pregunta cada medio segundo: los fotogramas presentados desde la vez
// anterior, entre el tiempo que ha pasado.
float MedirFps() {
  using reloj = std::chrono::steady_clock;
  static reloj::time_point antes;
  static uint64_t fotogramas_antes = 0;
  static bool hay_anterior = false;

  const auto ahora = reloj::now();
  const uint64_t fotogramas = g_nfsmw_fotogramas_juego.load(std::memory_order_relaxed);
  if (!hay_anterior) {
    hay_anterior = true;
    antes = ahora;
    fotogramas_antes = fotogramas;
    return g_fps;
  }
  const double segundos = std::chrono::duration<double>(ahora - antes).count();
  if (segundos < 0.25) {
    return g_fps;
  }
  g_fps = static_cast<float>(double(fotogramas - fotogramas_antes) / segundos);
  antes = ahora;
  fotogramas_antes = fotogramas;
  return g_fps;
}

// ---------------------------------------------------------------------------
//  3. URI content://
// ---------------------------------------------------------------------------

// Lo llama el SDK al mapear la imagen de disco (tools/parche_iso.py y
// tools/android/parche_nativo.py). Devuelve un descriptor que pasa a
// ser del SDK, o -1.
int AbrirContent(const char* uri, const char* modo) {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  auto actividad = static_cast<jobject>(SDL_GetAndroidActivity());
  if (!env || !actividad) {
    return -1;
  }
  int fd = -1;
  // FindClass desde un hilo nativo no ve las clases de la app: se llega a la
  // de la actividad por el objeto.
  jclass clase = env->GetObjectClass(actividad);
  jmethodID abrir = env->GetStaticMethodID(
      clase, "openContentFd",
      "(Ljava/lang/String;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;");
  if (abrir) {
    jstring juri = env->NewStringUTF(uri);
    jstring jmodo = env->NewStringUTF(modo ? modo : "r");
    jobject pfd = env->CallStaticObjectMethod(clase, abrir, juri, jmodo);
    if (!env->ExceptionCheck() && pfd) {
      jclass clase_pfd = env->GetObjectClass(pfd);
      jmethodID soltar = env->GetMethodID(clase_pfd, "detachFd", "()I");
      if (soltar) {
        fd = env->CallIntMethod(pfd, soltar);
      }
      env->DeleteLocalRef(clase_pfd);
    }
    if (pfd) {
      env->DeleteLocalRef(pfd);
    }
    env->DeleteLocalRef(juri);
    env->DeleteLocalRef(jmodo);
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    fd = -1;
  }
  env->DeleteLocalRef(clase);
  env->DeleteLocalRef(actividad);
  return fd;
}

// Al cargar libmain.so, antes de que arranque nada.
const struct ApuntarAbrirContent {
  ApuntarAbrirContent() { rex::filesystem::SetAndroidContentOpener(&AbrirContent); }
} g_apuntar_abrir_content;

}  // namespace

// ---------------------------------------------------------------------------
//  4. La sonda de Vulkan
// ---------------------------------------------------------------------------

// La llama SDL_main del SDK antes de arrancar el juego (parche_nativo.py). -1 =
// no se pide la sonda: que siga el juego.
extern "C" int NfsmwSondaSiSePide(int argc, char** argv) {
  constexpr const char kArgSonda[] = "--nfsmw_sonda";
  bool sonda = false;
  std::vector<char*> args;
  args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    // No es un cvar: se quita antes de que cvar::Init proteste.
    if (std::strcmp(argv[i], kArgSonda) == 0) {
      sonda = true;
    } else {
      args.push_back(argv[i]);
    }
  }
  if (!sonda) {
    return -1;
  }
  return NfsmwSondaVulkan(static_cast<int>(args.size()), args.data());
}

// ---------------------------------------------------------------------------
//  5. Hilos fijados a nucleos
// ---------------------------------------------------------------------------

namespace {
nfsmw::afinidad::VigilantePtr g_vigilante;
}  // namespace

// ---------------------------------------------------------------------------
//  7. Segundo plano
// ---------------------------------------------------------------------------
//
// Con el motor de Xenos, al minimizar se callaba el sonido y el juego se
// quedaba quieto. Con el nativo seguia corriendo: su renderizador no espera a
// nadie para presentar. Al irse a segundo plano:
//
//   - se pausa el sistema de audio del SDK (su hilo de audio y el XMA), y con
//     el, la salida AAudio;
//   - el hilo del anillo se para en el siguiente cambio de fotograma
//     (NfsmwAndroidPausaEnSwap, que llama su renderizador: parche_nativo.py), y
//     el juego se queda esperando sitio en el anillo, donde ya sabe esperar;
//   - el reloj del rescate de su servidor de audio (nfsmw_audio_servidor.cpp)
//     se para: mide "250 ms sin un fin de paquete", y con el audio en pausa y
//     el reloj de verdad entraria en rescate y liberaria paquetes que la voz
//     todavia tiene. Medido: cientos por pausa, y el sonido corrupto al volver.
//
// Suspender los hilos del juego a la fuerza (XThread::Suspend) se probo y se
// quito: los paraba en cualquier punto, y al volver el juego se quedaba parado
// de 5 a 17 s.

// El reloj del rescate (su nfsmw_audio_servidor.cpp). Es un puntero a funcion
// para poder cambiarlo en las pruebas.
namespace nfsmw::audio_servidor {
extern int64_t (*g_reloj_ms)();
}  // namespace nfsmw::audio_servidor

namespace {

std::mutex g_pausa_mutex;
std::condition_variable g_pausa_cv;
bool g_en_pausa = false;
bool g_audio_pausado = false;

// Su reloj, sin el tiempo pasado en segundo plano: parado durante la pausa.
int64_t (*g_reloj_real)() = nullptr;
std::atomic<int64_t> g_pausa_desde_ms{-1};
std::atomic<int64_t> g_pausado_ms{0};

int64_t RelojSinPausas() {
  const int64_t desde = g_pausa_desde_ms.load(std::memory_order_acquire);
  const int64_t ahora = desde >= 0 ? desde : g_reloj_real();
  return ahora - g_pausado_ms.load(std::memory_order_relaxed);
}

void PararReloj() {
  if (g_reloj_real && g_pausa_desde_ms.load(std::memory_order_relaxed) < 0) {
    g_pausa_desde_ms.store(g_reloj_real(), std::memory_order_release);
  }
}

void SeguirReloj() {
  const int64_t desde = g_pausa_desde_ms.load(std::memory_order_relaxed);
  if (g_reloj_real && desde >= 0) {
    g_pausado_ms.fetch_add(g_reloj_real() - desde, std::memory_order_relaxed);
    g_pausa_desde_ms.store(-1, std::memory_order_release);
  }
}

// Todos los sistemas de audio del SDK (el de SDL, el nuestro de AAudio)
// heredan de AudioSystem.
rex::audio::AudioSystem* SistemaDeAudio() {
  auto* runtime = rex::Runtime::instance();
  auto* audio = runtime ? runtime->audio_system() : nullptr;
  return audio ? static_cast<rex::audio::AudioSystem*>(audio) : nullptr;
}

void Pausar() {
  {
    std::lock_guard<std::mutex> lock(g_pausa_mutex);
    if (g_en_pausa) {
      return;
    }
    g_en_pausa = true;
  }
  PararReloj();
  if (auto* audio = SistemaDeAudio(); audio && !audio->is_paused()) {
    audio->Pause();
    g_audio_pausado = true;
  }
  REXLOG_INFO("[segundo plano] juego y audio en pausa");
}

void Reanudar() {
  {
    std::lock_guard<std::mutex> lock(g_pausa_mutex);
    if (!g_en_pausa) {
      return;
    }
    g_en_pausa = false;
  }
  g_pausa_cv.notify_all();
  if (g_audio_pausado) {
    if (auto* audio = SistemaDeAudio()) {
      audio->Resume();
    }
    g_audio_pausado = false;
  }
  SeguirReloj();
  REXLOG_INFO("[segundo plano] juego y audio en marcha");
}

// En el hilo de SDL, dentro de su aviso de ciclo de vida: WILL_ENTER_BACKGROUND
// llega antes de que Android quite la superficie, y WILL_ENTER_FOREGROUND es lo
// primero al volver.
bool SDLCALL EventosSegundoPlano(void*, SDL_Event* evento) {
  if (evento->type == SDL_EVENT_WILL_ENTER_BACKGROUND) {
    Pausar();
  } else if (evento->type == SDL_EVENT_WILL_ENTER_FOREGROUND) {
    Reanudar();
  }
  return true;
}

}  // namespace

// La llama el hilo del anillo de su renderizador en cada cambio de fotograma,
// antes de presentarlo (parche_nativo.py). En marcha no cuesta mas que mirar un
// bool con el cerrojo; en pausa espera hasta que la app vuelve.
extern "C" void NfsmwAndroidPausaEnSwap() {
  std::unique_lock<std::mutex> lock(g_pausa_mutex);
  if (g_en_pausa) {
    REXLOG_INFO("[segundo plano] el anillo se para en el cambio de fotograma");
    g_pausa_cv.wait(lock, [] { return !g_en_pausa; });
  }
}

// Su nfsmw_app.h la llama en OnPostInitLogging, cuando ya estan leidos los
// cvars y el log escribe en el fichero, y antes de que el juego cree sus hilos.
// En su app es android_rendimiento.cpp, que deja los hilos fuera de los nucleos
// lentos; aqui hace lo nuestro:
//   - el vigilante de afinidad (afinidad.cpp), que con "auto" pone el hilo del
//     anillo y el principal del juego en los nucleos prime;
//   - parar el juego y el audio en segundo plano.
// Los dos viven lo que el proceso, que GameActivity mata al salir.
extern "C" void nfsmw_android_nucleos_grandes_aplicar() {
  if (!g_vigilante) {
    g_vigilante = nfsmw::afinidad::Arrancar();
  }
  static bool vigilando_segundo_plano = false;
  if (!vigilando_segundo_plano) {
    vigilando_segundo_plano = SDL_AddEventWatch(EventosSegundoPlano, nullptr);
    // Antes de que arranque el juego, y con el, el hilo servidor de audio.
    if (!g_reloj_real && nfsmw::audio_servidor::g_reloj_ms) {
      g_reloj_real = nfsmw::audio_servidor::g_reloj_ms;
      nfsmw::audio_servidor::g_reloj_ms = &RelojSinPausas;
    }
  }
}

// ---------------------------------------------------------------------------
//  6. Audio por AAudio
// ---------------------------------------------------------------------------

// La llama su NfsmwApp::OnPreSetup (parche_nativo.py), con la factoria de
// audio por defecto (SDL) ya puesta. Con el motor de Xenos lo hace juego.cpp.
void NfsmwAndroidAudio(rex::RuntimeConfig& config) {
  if (REXCVAR_GET(nfsmw_audio_aaudio)) {
    config.audio_factory = REX_AUDIO_BACKEND(rex::audio::android::AndroidAAudioSystem);
  }
}

extern "C" JNIEXPORT void JNICALL Java_io_github_nfsmwrecomp_TouchControllerBridge_setState(
    JNIEnv*, jclass, jint buttons, jfloat left_x, jfloat left_y, jfloat right_x, jfloat right_y,
    jfloat left_trigger, jfloat right_trigger) {
  // -1 es "mando oculto o desconectado": todo suelto.
  if (buttons < 0) {
    buttons = 0;
  }
  uint16_t mascara = 0;
  for (int i = 0; i < 15; ++i) {
    if (buttons & (1 << i)) {
      mascara |= kBotonXInput[i];
    }
  }
  // Los sticks llegan como en SDL, con la Y positiva hacia abajo; en el mando
  // de la Xbox 360 es al reves.
  rex_sdl_set_touch_gamepad_state(mascara, Eje(left_x), Eje(-left_y), Eje(right_x), Eje(-right_y),
                                  Gatillo(left_trigger), Gatillo(right_trigger));
}

extern "C" JNIEXPORT jfloat JNICALL Java_io_github_nfsmwrecomp_GameActivity_nativeFps(JNIEnv*,
                                                                                    jclass) {
  return MedirFps();
}

extern "C" JNIEXPORT jfloat JNICALL
Java_io_github_nfsmwrecomp_GameActivity_nativeMsPorFotograma(JNIEnv*, jclass) {
  return g_fps > 0.0f ? 1000.0f / g_fps : -1.0f;
}
