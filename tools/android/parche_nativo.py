#!/usr/bin/env python3
"""
Motor nativo: lo que nuestra app de Android necesita del SDK de nfsmw-android.

    python tools/android/parche_nativo.py            aplicar
    python tools/android/parche_nativo.py --estado
    python tools/android/parche_nativo.py --revertir
    python tools/android/parche_nativo.py --arbol D:\\otra\\ruta\\nfsmw-android

Toca tres ficheros del SDK del motor nativo (..\\nfsmw-android\\sdk) y dos de su
app (..\\nfsmw-android\\app):

    sdk/include/rex/filesystem.h            SetAndroidContentOpener
    sdk/src/core/filesystem_posix.cpp       OpenAndroidContentFileDescriptor
    sdk/src/ui/windowed_app_main_sdl.cpp    la sonda de Vulkan, antes del juego
    app/src/nfsmw_app.h                     la salida de audio de la app (AAudio)
    app/src/nfsmw_nativo_sistema.cpp        parar el anillo en segundo plano

Los demas que necesita ese SDK son los mismos que el nuestro y se aplican tal
cual con NFSMW_SDK apuntando a el: parche_iso.py, parche_gamertag.py y
parche_turnip.py. tools/android/preparar_nativo.py los pone todos.


1. ABRIR UNA URI content://
===========================

La app no copia la ISO: la elige con el selector del sistema y se queda con su
URI content://. tools/parche_iso.py ya hace que el SDK monte una imagen de
disco y que, al mapearla, pida el descriptor con
OpenAndroidContentFileDescriptor. En el SDK de ReXGlue esa funcion existe, pero
en el port a Android de nfsmw-android esta vacia (devuelve ENOSYS): su app
importa el juego a una carpeta y no la necesita.

Abrir la URI es cosa de Java (ContentResolver), y el SDK no sabe de la
actividad. Asi que el SDK solo guarda un puntero a quien sabe abrirla, y la app
lo apunta al cargar libmain.so
(android/app/src/main/cpp/nativo/nativo_android.cpp), que llama a
GameActivity.openContentFd por JNI.


2. LA SONDA DE VULKAN
=====================

Con el motor de Xenos, SDL_main es nuestro (android_main.cpp) y mira si le
piden --nfsmw_sonda antes de arrancar el juego. Con el nativo SDL_main es de su
SDK (windowed_app_main_sdl.cpp). Se le anade la misma pregunta: si la app
define NfsmwSondaSiSePide (nativo_android.cpp, en la misma libmain.so), se la
llama primero, y si devuelve un resultado no se arranca el juego.

3. EL AUDIO POR AAUDIO
======================

Con el motor de Xenos, juego.cpp hereda de NfsmwApp y en OnPreSetup cambia la
factoria de audio de SDL por la de nuestro driver AAudio
(android/app/src/main/cpp/audio/aaudio_driver.cpp). Con el nativo la app es la
suya. En su OnPreSetup se llama a NfsmwAndroidAudio si la app la define
(nativo_android.cpp, en la misma libmain.so), que pone la de AAudio.


4. PARAR EL JUEGO EN SEGUNDO PLANO
==================================

Al minimizar, el sonido se para (el audio del SDK se pausa) pero el juego
seguia: su renderizador no espera a nadie para presentar. Suspender los hilos
del juego a la fuerza (XThread::Suspend, por senal) lo paraba, pero los dejaba
en cualquier punto: medido, tras volver el juego se quedaba parado 5-17 s, y un
hilo que el juego tenia a medio arrancar no volvia.

Asi que se para donde el juego ya sabe esperar: el hilo del anillo se detiene
en el siguiente cambio de fotograma (PM4_XE_SWAP), antes de presentarlo, y el
juego se queda esperando sitio en el anillo, como con una GPU lenta. La app
define NfsmwAndroidPausaEnSwap (nativo_android.cpp), que espera ahi mientras
la app esta en segundo plano.
"""

import argparse
import pathlib
import sys

RAIZ = pathlib.Path(__file__).resolve().parents[2]

CABECERA_ANCLA = '''bool IsAndroidContentUri(const std::string_view source);
int OpenAndroidContentFileDescriptor(const std::string_view uri, const char* mode);
'''

CABECERA_NUEVO = '''bool IsAndroidContentUri(const std::string_view source);
int OpenAndroidContentFileDescriptor(const std::string_view uri, const char* mode);
// PARCHE LOCAL - URI content://
//
// Quien abre una URI content:// y devuelve su descriptor (o -1). Lo pone la
// app, que es la que tiene la actividad de Java; sin el, abrir una URI falla.
using AndroidContentOpener = int (*)(const char* uri, const char* mode);
void SetAndroidContentOpener(AndroidContentOpener opener);
'''

FUENTE_ANCLA = '''int OpenAndroidContentFileDescriptor(const std::string_view uri, const char* mode) {
  // SAF access requires a Java ContentResolver and an app-owned JNI bridge.
  // The Android app currently imports selected trees into private POSIX storage.
  (void)uri;
  (void)mode;
  errno = ENOSYS;
  return -1;
}
'''

FUENTE_NUEVO = '''// PARCHE LOCAL - URI content://
//
// Se apunta una vez, al cargar libmain.so y antes de que exista ningun hilo
// del juego: no hace falta que sea atomico.
static AndroidContentOpener g_android_content_opener = nullptr;

void SetAndroidContentOpener(AndroidContentOpener opener) {
  g_android_content_opener = opener;
}

int OpenAndroidContentFileDescriptor(const std::string_view uri, const char* mode) {
  if (!g_android_content_opener) {
    errno = ENOSYS;
    return -1;
  }
  const std::string copia(uri);
  return g_android_content_opener(copia.c_str(), mode);
}
'''

SONDA_ANCLA = '''#else

int main(int argc, char* argv[]) {
  return RunWindowedApp(argc, argv);
}
'''

SONDA_NUEVO = '''#else

#if REX_PLATFORM_ANDROID
// PARCHE LOCAL - sonda de Vulkan
//
// La app (nativo_android.cpp, en esta misma libreria) la define: si los
// argumentos piden la sonda, la ejecuta y devuelve su resultado; si no,
// devuelve -1 y se arranca el juego como siempre.
extern "C" int NfsmwSondaSiSePide(int argc, char** argv) __attribute__((weak));
#endif

int main(int argc, char* argv[]) {
#if REX_PLATFORM_ANDROID
  if (NfsmwSondaSiSePide) {
    const int sonda = NfsmwSondaSiSePide(argc, argv);
    if (sonda >= 0) {
      return sonda;
    }
  }
#endif
  return RunWindowedApp(argc, argv);
}
'''

AUDIO_ANCLA = '''  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (nfsmw::nativo::Activo()) {
      config.graphics = nfsmw::nativo::CrearSistemaGrafico();
    }
'''

AUDIO_NUEVO = '''  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (nfsmw::nativo::Activo()) {
      config.graphics = nfsmw::nativo::CrearSistemaGrafico();
    }
#if REX_PLATFORM_ANDROID
    // PARCHE LOCAL (NFSMW Recompiled) - la app de Android puede poner su propia
    // salida de audio (AAudio) en vez de la de SDL. Ver nativo_android.cpp.
    if (NfsmwAndroidAudio) {
      NfsmwAndroidAudio(config);
    }
#endif
'''

AUDIO_DECL_ANCLA = '''class NfsmwApp : public rex::ReXApp {
'''

AUDIO_DECL_NUEVO = '''#if REX_PLATFORM_ANDROID
// PARCHE LOCAL (NFSMW Recompiled): la define la app de Android, si quiere.
void NfsmwAndroidAudio(rex::RuntimeConfig& config) __attribute__((weak));
#endif

class NfsmwApp : public rex::ReXApp {
'''

PAUSA_DECL_ANCLA = '''#include "nfsmw_nativo_sistema.h"
'''

PAUSA_DECL_NUEVO = '''#include "nfsmw_nativo_sistema.h"

#if REX_PLATFORM_ANDROID
// PARCHE LOCAL (NFSMW Recompiled): la define la app de Android (nativo_android.cpp).
// Espera mientras la app esta en segundo plano.
extern "C" void NfsmwAndroidPausaEnSwap() __attribute__((weak));
#endif
'''

PAUSA_SWAP_ANCLA = '''        TrazaSwap();
        Presentar();
        AnotarJuegoPorDelante();  // Measurement only
'''

PAUSA_SWAP_NUEVO = '''        TrazaSwap();
#if REX_PLATFORM_ANDROID
        // PARCHE LOCAL (NFSMW Recompiled): en segundo plano el anillo se para aqui, en el
        // cambio de fotograma, y el juego se queda esperando sitio en el anillo.
        if (NfsmwAndroidPausaEnSwap) {
          NfsmwAndroidPausaEnSwap();
        }
#endif
        Presentar();
        AnotarJuegoPorDelante();  // Measurement only
'''

BLOQUES = [
    ("sdk/include/rex/filesystem.h", "declarar SetAndroidContentOpener", CABECERA_ANCLA, CABECERA_NUEVO),
    ("sdk/src/core/filesystem_posix.cpp", "abrir la URI con lo que ponga la app", FUENTE_ANCLA, FUENTE_NUEVO),
    ("sdk/src/ui/windowed_app_main_sdl.cpp", "la sonda de Vulkan antes del juego", SONDA_ANCLA, SONDA_NUEVO),
    ("app/src/nfsmw_app.h", "declarar NfsmwAndroidAudio", AUDIO_DECL_ANCLA, AUDIO_DECL_NUEVO),
    ("app/src/nfsmw_app.h", "la salida de audio de la app", AUDIO_ANCLA, AUDIO_NUEVO),
    ("app/src/nfsmw_nativo_sistema.cpp", "declarar la pausa del anillo", PAUSA_DECL_ANCLA, PAUSA_DECL_NUEVO),
    ("app/src/nfsmw_nativo_sistema.cpp", "parar el anillo en el Swap", PAUSA_SWAP_ANCLA, PAUSA_SWAP_NUEVO),
]


def leer(f):
    with open(f, encoding="utf-8", newline="") as h:
        txt = h.read()
    eol = "\r\n" if "\r\n" in txt else "\n"
    return txt.replace("\r\n", "\n"), eol


def escribir(f, txt, eol):
    with open(f, "w", encoding="utf-8", newline="") as h:
        h.write(txt.replace("\n", eol))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--arbol", default=str(RAIZ.parent / "nfsmw-android"),
                   help="el arbol del motor nativo (con sdk/ y app/)")
    p.add_argument("--estado", action="store_true")
    p.add_argument("--revertir", action="store_true")
    args = p.parse_args()

    arbol = pathlib.Path(args.arbol)
    ficheros = {}
    for ruta, _, _, _ in BLOQUES:
        if ruta in ficheros:
            continue
        f = arbol / ruta
        if not f.exists():
            sys.exit(f"[ERROR] No encuentro {f}")
        ficheros[ruta] = [f, *leer(f)]

    if args.estado:
        puestos = sum(1 for r, _, _, nuevo in BLOQUES if nuevo in ficheros[r][1])
        print(f"  nativo                     {puestos} de {len(BLOQUES)} bloques aplicados")
        for ruta, nombre, _, nuevo in BLOQUES:
            print(f"      {'si' if nuevo in ficheros[ruta][1] else 'NO':>2}  {nombre}  ({ruta})")
        return 0

    if args.revertir:
        for ruta, _, ancla, nuevo in BLOQUES:
            ficheros[ruta][1] = ficheros[ruta][1].replace(nuevo, ancla)
        for f, txt, eol in ficheros.values():
            escribir(f, txt, eol)
        print("[ok] nativo: quitado")
        return 0

    faltan = [b for b in BLOQUES if b[3] not in ficheros[b[0]][1]]
    if not faltan:
        print(f"[ok] nativo: los {len(BLOQUES)} bloques ya estaban")
        return 0
    # Todos los anclajes antes de escribir nada.
    for ruta, nombre, ancla, _ in faltan:
        n = ficheros[ruta][1].count(ancla)
        if n != 1:
            sys.exit(f"[ERROR] El anclaje de '{nombre}' aparece {n} veces en {ruta}, esperaba 1. "
                     f"No he tocado nada.")
    for ruta, nombre, ancla, nuevo in faltan:
        ficheros[ruta][1] = ficheros[ruta][1].replace(ancla, nuevo)
        print(f"[ok] Aplicado: {nombre}")
    for f, txt, eol in ficheros.values():
        escribir(f, txt, eol)
    return 0


if __name__ == "__main__":
    sys.exit(main())
