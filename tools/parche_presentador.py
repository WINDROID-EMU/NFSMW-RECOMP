#!/usr/bin/env python3
"""
Hace que VSYNC y el LIMITE DE FPS existan de verdad.

    python tools/parche_presentador.py            aplicar
    python tools/parche_presentador.py --estado
    python tools/parche_presentador.py --revertir

Toca un solo fichero del SDK:  src/ui/d3d12/d3d12_presenter.cpp
Guarda un .original la primera vez y es idempotente.


POR QUE HACIA FALTA ESTO
========================

VSYNC
-----
El cvar "vsync" existe, pero NO es vsync. Se lee en un unico sitio de todo el
SDK, en command_processor.cpp, dentro de ExecutePacketType3_WAIT_REG_MEM:

    if (!REXCVAR_GET(vsync)) {
      // User wants it fast and dangerous.
      rex::thread::MaybeYield();
    } else {
      rex::thread::Sleep(std::chrono::milliseconds(wait / 0x100));
    }

O sea: decide si el procesador de comandos DUERME cuando el flujo de comandos
del juego pide esperar, o si se queda girando. Es un "corre a lo loco", no una
sincronizacion con la pantalla.

La sincronizacion de verdad esta en el presentador de D3D12, y estaba clavada:

    swap_chain->Present(0, DXGI_PRESENT_RESTART | ...);

Ese primer 0 es el SyncInterval. Con 0 se presenta siempre en cuanto se puede,
pase lo que pase con el cvar. El comentario del SDK explica por que se eligio
asi -el monitor puede ir a 144 Hz, que no es multiplo de los 30 o 60 del
guest-, pero el efecto es que la casilla de vsync no hacia nada visible.

El parche pasa SyncInterval 1 cuando vsync esta activado.

  DETALLE QUE IMPORTA: con SyncInterval distinto de 0, DXGI RECHAZA la bandera
  ALLOW_TEARING y devuelve DXGI_ERROR_INVALID_CALL. Son excluyentes. Y
  DXGI_PRESENT_RESTART descarta fotogramas encolados, que es justo lo contrario
  de lo que se quiere con vsync. Por eso con vsync activado no se pasa ninguna
  de las dos, y sin vsync se deja todo exactamente como estaba.

  El cvar se lee por NOMBRE, con rex::cvar::Query<bool>("vsync"), no con
  REXCVAR_GET. Es a proposito: "vsync" se define en el plugin de GPU
  (rexgpu-xenos.dll) y el presentador vive en rexruntime.dll. Enlazar contra un
  simbolo del plugin no funcionaria; el registro de cvars, en cambio, es comun
  y la busqueda por nombre lo atraviesa sin problema. Se comprueba antes con
  GetFlagInfo por si el plugin no estuviera cargado.

LIMITE DE FPS
-------------
No existia ninguno. Se busco en todas las cabeceras y en los simbolos de los
DLL compilados: solo hay "vsync". Asi que se anade un cvar nuevo, max_fps,
definido aqui mismo en el presentador.

  0 = sin limite (el comportamiento de siempre).

Duerme hasta que toque el siguiente fotograma. No duerme del todo: deja el
ultimo tramo girando, porque Sleep en Windows tiene una granularidad de entre
1 y 15 ms y sin ese remate el limite se queda corto y con tirones.
"""

import argparse
import os
import pathlib
import shutil
import sys

MARCA = "PARCHE LOCAL - vsync real y limitador de fps"

# ---------------------------------------------------------------------------
#  El sitio exacto, copiado tal cual del fuente del SDK.
# ---------------------------------------------------------------------------
ANCLA = """  HRESULT present_result = paint_context_.swap_chain->Present(
      0, DXGI_PRESENT_RESTART |
             (paint_context_.swap_chain_allows_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0));
"""

NUEVO = """  // ------------------------------------------------------------------
  //  PARCHE LOCAL - vsync real y limitador de fps
  //
  //  Aqui antes habia un Present(0, ...) con el SyncInterval clavado a 0,
  //  asi que la sincronizacion con la pantalla no ocurria nunca por mucho
  //  que se activara el cvar "vsync" -que en realidad solo decide si el
  //  procesador de comandos duerme en las esperas del guest-.
  // ------------------------------------------------------------------

  // Limitador. max_fps = 0 deja el comportamiento original.
  {
    const int32_t tope = REXCVAR_GET(max_fps);
    if (tope > 0) {
      using Reloj = std::chrono::steady_clock;
      // Estatica de funcion: PaintAndPresentImpl corre siempre en el hilo de
      // pintado, asi que no hace falta sincronizar nada.
      static Reloj::time_point siguiente{};
      const auto periodo = std::chrono::duration_cast<Reloj::duration>(
          std::chrono::duration<double>(1.0 / double(tope)));
      const auto ahora = Reloj::now();
      if (siguiente > ahora) {
        // Dormir casi todo y rematar girando: Sleep tiene una granularidad
        // de 1 a 15 ms y sin el remate el limite se queda corto.
        const auto margen = std::chrono::milliseconds(2);
        if (siguiente - ahora > margen) {
          std::this_thread::sleep_for((siguiente - ahora) - margen);
        }
        while (Reloj::now() < siguiente) {
          std::this_thread::yield();
        }
      }
      siguiente = std::max(Reloj::now(), siguiente) + periodo;
    }
  }

  // Vsync. Se busca por nombre porque el cvar lo define el plugin de GPU, que
  // es otro DLL: enlazar contra su simbolo no funcionaria, pero el registro de
  // cvars es comun.
  bool con_vsync = false;
  if (rex::cvar::GetFlagInfo("vsync") != nullptr) {
    con_vsync = rex::cvar::Query<bool>("vsync");
  }

  UINT sync_interval = 0;
  UINT present_flags = 0;
  if (con_vsync) {
    sync_interval = 1;
    // Ni ALLOW_TEARING ni RESTART: la primera es incompatible con
    // SyncInterval != 0 (DXGI devuelve DXGI_ERROR_INVALID_CALL) y la segunda
    // descarta los fotogramas encolados, que es lo contrario de lo que se
    // busca al sincronizar.
  } else {
    present_flags = DXGI_PRESENT_RESTART |
                    (paint_context_.swap_chain_allows_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
  }

  HRESULT present_result = paint_context_.swap_chain->Present(sync_interval, present_flags);
"""

# El cvar nuevo y las cabeceras que necesita el codigo de arriba.
ANCLA_CVAR = """REXCVAR_DEFINE_BOOL(d3d12_allow_variable_refresh_rate_and_tearing, true, "UI/D3D12",
                    "Allow variable refresh rate and tearing");
"""

NUEVO_CVAR = """REXCVAR_DEFINE_BOOL(d3d12_allow_variable_refresh_rate_and_tearing, true, "UI/D3D12",
                    "Allow variable refresh rate and tearing");

// PARCHE LOCAL - vsync real y limitador de fps
// El SDK no traia ningun limitador: solo estaba "vsync", y ese ni siquiera
// tocaba el SyncInterval del Present. Este es nuevo.
REXCVAR_DEFINE_INT32(max_fps, 0, "UI/Present",
                     "Limite de fotogramas por segundo (0 = sin limite)")
    .range(0, 1000);
"""

ANCLA_INC = """#include <algorithm>
#include <climits>
#include <cmath>
#include <memory>
#include <utility>
"""

NUEVO_INC = """#include <algorithm>
#include <chrono>   // PARCHE LOCAL - limitador de fps
#include <climits>
#include <cmath>
#include <memory>
#include <thread>   // PARCHE LOCAL - limitador de fps
#include <utility>
"""


def localizar_sdk():
    raiz = pathlib.Path(__file__).resolve().parent.parent
    # NFSMW_SDK apunta a otro arbol del SDK (el de Android, por ejemplo). Si
    # esta puesta se usa SOLO esa ruta: caer en silencio en el SDK de Windows
    # parchearia el arbol equivocado.
    otro = os.environ.get("NFSMW_SDK")
    candidatos = [pathlib.Path(otro)] if otro else [raiz.parent / "rexglue-sdk", raiz / "sdk"]
    for cand in candidatos:
        f = cand / "src" / "ui" / "d3d12" / "d3d12_presenter.cpp"
        if f.exists():
            return f
    sys.exit("[ERROR] No encuentro src/ui/d3d12/d3d12_presenter.cpp del SDK.\n"
             "        Se busca en ..\\rexglue-sdk y en .\\sdk")


def main():
    p = argparse.ArgumentParser(add_help=True)
    p.add_argument("--estado", action="store_true")
    p.add_argument("--revertir", action="store_true")
    args = p.parse_args()

    f = localizar_sdk()
    original = f.with_suffix(".cpp.original")
    txt = f.read_text(encoding="utf-8")
    puesto = MARCA in txt

    if args.estado:
        print(f"  {f}")
        print("  Parche:", "APLICADO" if puesto else "sin aplicar")
        return 0

    if args.revertir:
        if original.exists():
            shutil.copy2(original, f)
            print("[ok] Restaurado desde .original")
        else:
            print("[aviso] No hay .original que restaurar.")
        return 0

    if puesto:
        print("[ok] Ya estaba aplicado. No toco nada.")
        return 0

    # Comprobar los tres anclajes ANTES de escribir nada. Si el SDK cambia de
    # version y alguno no cuadra, mejor no dejar el fichero a medias.
    for nombre, ancla in [("includes", ANCLA_INC),
                          ("definicion de cvars", ANCLA_CVAR),
                          ("llamada a Present", ANCLA)]:
        n = txt.count(ancla)
        if n != 1:
            sys.exit(f"[ERROR] El anclaje '{nombre}' aparece {n} veces, esperaba 1.\n"
                     f"        El SDK habra cambiado. No he tocado nada.")

    if not original.exists():
        shutil.copy2(f, original)
        print(f"[ok] Copia de seguridad: {original.name}")

    txt = txt.replace(ANCLA_INC, NUEVO_INC)
    txt = txt.replace(ANCLA_CVAR, NUEVO_CVAR)
    txt = txt.replace(ANCLA, NUEVO)
    f.write_text(txt, encoding="utf-8")

    print("[ok] Parche aplicado.")
    print()
    print("  vsync    ahora pasa SyncInterval 1 al Present")
    print("  max_fps  cvar nuevo, 0 = sin limite")
    print()
    print("  HAY QUE RECOMPILAR EL SDK para que sirva de algo:")
    print("    cmake --build out/build/win-amd64 --config Release --target install")
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
