// NFSMW Recompiled - entrada nativa en Android
//
// SDLActivity (Java) carga librexruntime.so y libmain.so y llama a SDL_main
// desde su hilo nativo. Aqui se hace lo unico que no puede hacer el escritorio:
// darle al SDK el JavaVM, la actividad y la carpeta de las .so, y despues se
// sigue por el mismo camino que en Windows y Linux.
//
// Dos caminos:
//   --nfsmw_sonda   la sonda de Vulkan (sonda_vulkan.cpp). No necesita el juego.
//   lo demas        el juego, por windowed_app_main_sdl.cpp del SDK, igual que
//                   en escritorio. Solo existe si el APK se compilo con codigo
//                   generado (NFSMW_CON_JUEGO).

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_system.h>

#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <rex/filesystem.h>
#include <rex/main_android.h>
#include <rex/memory.h>
#include <rex/system.h>
#include <rex/thread.h>

#if NFSMW_CON_JUEGO
#include <algorithm>
#include <map>
#include <memory>

#include <SDL3/SDL_joystick.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context_sdl.h>

#include "afinidad.h"
#endif

int NfsmwSondaVulkan(int argc, char** argv);  // sonda_vulkan.cpp

namespace {

#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, "nfsmw", __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "nfsmw", __VA_ARGS__)

constexpr const char kArgSonda[] = "--nfsmw_sonda";

// ApplicationInfo.nativeLibraryDir sin pasar por JNI: es la carpeta donde el
// cargador ha encontrado esta misma libreria. Con useLegacyPackaging = true las
// .so se extraen ahi, y ahi busca el SDK el plugin de GPU y adrenotools sus
// hooks.
std::string CarpetaDeLasLibrerias() {
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(&CarpetaDeLasLibrerias), &info) && info.dli_fname) {
    std::string ruta(info.dli_fname);
    const size_t barra = ruta.find_last_of('/');
    if (barra != std::string::npos) {
      return ruta.substr(0, barra);
    }
  }
  return {};
}

bool PrepararContextoAndroid() {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  // Referencia local: hay que soltarla. SetAndroidApplicationContext se queda
  // con una global propia.
  auto actividad = static_cast<jobject>(SDL_GetAndroidActivity());
  if (!env || !actividad) {
    ALOGE("Sin JNIEnv o sin actividad: SDL no ha terminado de arrancar");
    return false;
  }
  JavaVM* vm = nullptr;
  if (env->GetJavaVM(&vm) != JNI_OK || !vm) {
    env->DeleteLocalRef(actividad);
    ALOGE("GetJavaVM ha fallado");
    return false;
  }
  const std::string libs = CarpetaDeLasLibrerias();
  rex::SetAndroidApplicationContext(vm, actividad, libs.c_str());
  env->DeleteLocalRef(actividad);

  // EL SDK NO LLAMA A ESTO SOLO. Declara un AndroidInitialize() por subsistema
  // y espera que lo haga la aplicacion. Sin el de memoria, el puntero a
  // ASharedMemory_create se queda sin resolver, CreateFileMappingHandle cae al
  // camino de /dev/ashmem -desactivado desde API 29- y el arranque muere con
  //     Unable to reserve the 4gb guest address space.
  // que fue exactamente lo que paso en el primer arranque en el movil.
  rex::memory::AndroidInitialize();
  rex::thread::AndroidInitialize();
  rex::filesystem::AndroidInitialize();
  if (!rex::InitializeAndroidSystemForApplicationContext()) {
    ALOGE("El SDK no acepta el contexto de Android");
    return false;
  }

  ALOGI("Contexto Android listo; librerias en %s", libs.c_str());
  return true;
}

void CerrarContextoAndroid() {
  rex::filesystem::AndroidShutdown();
  rex::thread::AndroidShutdown();
  rex::memory::AndroidShutdown();
  rex::ShutdownAndroidSystem();
}

#if NFSMW_CON_JUEGO
// Lo mismo que RunWindowedApp de windowed_app_main_sdl.cpp, pero pidiendo la app
// por su nombre.
//
// En Android el SDK define XE_UI_WINDOWED_APPS_IN_LIBRARY: varias apps pueden
// convivir en una sola libreria, asi que REX_DEFINE_APP -en juego.cpp- las
// APUNTA EN UNA TABLA en vez de definir GetWindowedAppCreator(), que es lo que
// busca la entrada de escritorio. De ahi que esa no se compile aqui.
int EjecutarJuego(int argc, char* argv[]) {
  auto sobrantes = rex::cvar::Init(argc, argv);
  rex::cvar::ApplyEnvironment();
  rex::InitLoggingEarly();

  // El juego es de 2005 y dibuja 16:9. Sin esto, SDL crea la ventana con la
  // orientacion que tenga el movil en ese momento y el juego sale en vertical
  // con franjas negras arriba y abajo.
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");

  rex::ui::SDLWindowedAppContext contexto;
  if (!contexto.Initialize()) {
    ALOGE("No se pudo inicializar el contexto de SDL");
    return EXIT_FAILURE;
  }

  auto creador = rex::ui::WindowedApp::GetCreator("nfsmw");
  if (!creador) {
    ALOGE("La app 'nfsmw' no esta registrada: falta REX_DEFINE_APP en libmain.so");
    return EXIT_FAILURE;
  }
  std::unique_ptr<rex::ui::WindowedApp> app = creador(contexto);

  // Los argumentos sueltos (los que no son --ajuste) se reparten en el orden
  // que la app declara, igual que en escritorio.
  const auto& nombres = app->GetPositionalOptions();
  std::map<std::string, std::string> opciones;
  const size_t n = std::min(sobrantes.size(), nombres.size());
  for (size_t i = 0; i < n; ++i) {
    opciones[nombres[i]] = sobrantes[i];
  }
  app->SetParsedArguments(std::move(opciones));

  const bool iniciada = app->OnInitialize();

  // Hilos fijados a nucleos (afinidad.cpp), si el cvar thread_affinity lo pide.
  // DESPUES de OnInitialize: hasta ahi el log no escribe en el fichero (con
  // InitLoggingEarly solo va a logcat, que en el movil de pruebas esta capado),
  // y lo que fija el vigilante se perdia. Los hilos que ya existan los recoge
  // en su primera pasada.
  nfsmw::afinidad::VigilantePtr vigilante =
      iniciada ? nfsmw::afinidad::Arrancar() : nfsmw::afinidad::VigilantePtr();

  const int resultado = iniciada ? contexto.RunMainMessageLoop() : EXIT_FAILURE;
  // Parar el vigilante antes de desmontar la app, y con ella el log.
  vigilante.reset();
  app->InvokeOnDestroy();
  return resultado;
}
#endif  // NFSMW_CON_JUEGO

}  // namespace

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char* argv[]) {
  if (!PrepararContextoAndroid()) {
    return EXIT_FAILURE;
  }

  // --nfsmw_sonda no es un cvar del SDK: se quita antes de que cvar::Init lo
  // vea y proteste por un ajuste desconocido.
  bool sonda = false;
  std::vector<char*> args;
  args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    if (std::strcmp(argv[i], kArgSonda) == 0) {
      sonda = true;
    } else {
      args.push_back(argv[i]);
    }
  }
  const int n = static_cast<int>(args.size());

#if NFSMW_CON_JUEGO
  if (!sonda) {
    const int resultado = EjecutarJuego(n, args.data());
    CerrarContextoAndroid();
    return resultado;
  }
#else
  if (!sonda) {
    ALOGE("Este APK se compilo sin el codigo del juego: solo lleva la sonda de Vulkan");
  }
#endif
  const int resultado = NfsmwSondaVulkan(n, args.data());
  CerrarContextoAndroid();
  return resultado;
}

// Contador de fps en pantalla: GameActivity lo pide cada medio segundo.
//
// El valor lo publica el plugin de GPU (tools/parche_fps.py) en
// nfsmw_fps_invitado, con nombre C para poder encontrarlo con dlsym. El plugin
// lo carga el SDK con dlopen, asi que aqui se pide con RTLD_NOLOAD: si todavia
// no esta cargado, se devuelve -1 y la actividad pinta "--".
namespace {

// Un contador del plugin de GPU, buscado una sola vez. Devuelve nullptr
// mientras el plugin no este cargado.
std::atomic<float>* ContadorDelPlugin(const char* nombre, std::atomic<float>*& cache) {
  if (!cache) {
    void* plugin = dlopen("librexgpu-xenos.so", RTLD_NOW | RTLD_NOLOAD);
    if (!plugin) {
      return nullptr;
    }
    cache = static_cast<std::atomic<float>*>(dlsym(plugin, nombre));
  }
  return cache;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) jfloat
Java_io_github_nfsmwrecomp_GameActivity_nativeFps(JNIEnv*, jclass) {
  static std::atomic<float>* fps = nullptr;
  auto* v = ContadorDelPlugin("nfsmw_fps_invitado", fps);
  return v ? v->load(std::memory_order_relaxed) : -1.0f;
}

// Milisegundos por fotograma: la media del ultimo medio segundo.
extern "C" __attribute__((visibility("default"))) jfloat
Java_io_github_nfsmwrecomp_GameActivity_nativeMsPorFotograma(JNIEnv*, jclass) {
  static std::atomic<float>* ms = nullptr;
  auto* v = ContadorDelPlugin("nfsmw_ms_invitado", ms);
  return v ? v->load(std::memory_order_relaxed) : -1.0f;
}

#if NFSMW_CON_JUEGO
// Mando tactil (TouchControllerView): un mando virtual de SDL.
//
// Java llama aqui en CADA evento de toque, y al arrastrar un stick eso son 120-
// 240 veces por segundo. Cada SDL_SetJoystickVirtual* coge el cerrojo de los
// joysticks de SDL, el mismo que usa el hilo del juego para leer el mando. Asi
// que solo se manda lo que ha cambiado: al arrastrar un stick son dos ejes en
// vez de los 15 botones y 6 ejes de cada vez.
namespace {

constexpr int kBotones = 15;
constexpr int kEjes = 6;

// Lo ultimo que se le mando a SDL. Empieza como un mando virtual recien
// abierto -todo a cero-, asi que el primer envio solo manda lo que difiera: los
// gatillos sueltos (-32768), que en crudo arrancan a 0 y SDL leeria a medias.
SDL_Joystick* g_mando = nullptr;
int g_botones = 0;
Sint16 g_ejes[kEjes] = {};

Sint16 EjeAS16(float v) {
  v = std::clamp(v, -1.0f, 1.0f);
  return static_cast<Sint16>(v * 32767.0f);
}

Sint16 GatilloAS16(float v) {
  v = std::clamp(v, 0.0f, 1.0f);
  return static_cast<Sint16>((v * 65535.0f) - 32768.0f);
}

}  // namespace

extern "C" __attribute__((visibility("default"))) void JNICALL
Java_io_github_nfsmwrecomp_TouchControllerBridge_setState(
    JNIEnv*, jclass, jint buttons, jfloat left_x, jfloat left_y,
    jfloat right_x, jfloat right_y, jfloat left_trigger, jfloat right_trigger) {
  if (!g_mando) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = kEjes;
    desc.nbuttons = kBotones;
    desc.nhats = 0;
    const SDL_JoystickID jid = SDL_AttachVirtualJoystick(&desc);
    if (jid != 0) {
      g_mando = SDL_OpenJoystick(jid);
    }
    if (!g_mando) {
      return;
    }
  }

  // -1 es "mando oculto o desconectado": todo suelto. Antes se trataba como una
  // mascara mas, y con todos los bits a uno pulsaba los 15 botones a la vez.
  if (buttons < 0) {
    buttons = 0;
  }
  const int cambiados = buttons ^ g_botones;
  if (cambiados) {
    for (int i = 0; i < kBotones; ++i) {
      if (cambiados & (1 << i)) {
        SDL_SetJoystickVirtualButton(g_mando, i, (buttons & (1 << i)) != 0);
      }
    }
    g_botones = buttons;
  }

  const Sint16 ejes[kEjes] = {EjeAS16(left_x),  EjeAS16(left_y),  EjeAS16(right_x),
                              EjeAS16(right_y), GatilloAS16(left_trigger),
                              GatilloAS16(right_trigger)};
  for (int i = 0; i < kEjes; ++i) {
    if (ejes[i] != g_ejes[i]) {
      SDL_SetJoystickVirtualAxis(g_mando, i, ejes[i]);
      g_ejes[i] = ejes[i];
    }
  }
}
#endif

