#!/usr/bin/env python3
"""
Que el juego no se cierre en silencio cuando la GPU no vale.

    python tools/parche_gpu_fallback.py            aplicar
    python tools/parche_gpu_fallback.py --estado
    python tools/parche_gpu_fallback.py --revertir

Toca un solo fichero del SDK:  src/ui/d3d12/d3d12_provider.cpp
Guarda un .original la primera vez y es idempotente.


QUE PASABA
==========

La eleccion de adaptador ya era por capacidades, no por lista de modelos: se
recorren los adaptadores con EnumAdapters1 y se coge el primero que sepa crear
un dispositivo D3D12 a feature level 11_0. Eso esta bien y no se toca.

Lo que estaba mal eran las dos salidas de emergencia:

1. NO SE USABA EL FALLBACK QUE YA EXISTIA.
   El cvar d3d12_adapter admite -2, que significa "usa WARP" -el rasterizador
   por software de Microsoft-. Pero con el valor por defecto (-1) el bucle
   descarta explicitamente los adaptadores marcados como software:

       if (!(adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) break;

   O sea que si no habia ninguna GPU fisica valida, no se probaba WARP: se
   fallaba directamente. El fallback estaba ahi, pero habia que saber que
   existia y escribirlo a mano en la linea de comandos.

2. EL ERROR NO LO VEIA NADIE.
   Un REXLOG_ERROR y return false. La ventana no llega a abrirse, asi que
   desde fuera el juego "no hace nada": doble clic y ni un parpadeo. Para
   quien recibe la carpeta y no sabe que hay un log, eso es indistinguible de
   un ejecutable roto.


QUE HACE EL PARCHE
==================

  - Convierte el bucle en una funcion a la que se le dice si acepta
    adaptadores software. Misma logica, mismo orden, mismos criterios.

  - Primera pasada: solo GPU fisica, exactamente como antes.

  - Si no hay ninguna Y el usuario no pidio un adaptador concreto, SEGUNDA
    pasada aceptando software. Si WARP esta disponible, el juego arranca.
    Ira lentisimo -es un rasterizador por CPU-, y se avisa de ello en el log,
    pero arranca y se ve, que es infinitamente mejor que cerrarse.

  - Si tampoco hay WARP, se muestra un cuadro de dialogo de Windows que
    explica que hace falta y que hacer. El log sigue teniendo el mismo mensaje
    de siempre, para no romper nada que lo lea.

El cuadro de dialogo se llama por LoadLibrary/GetProcAddress en vez de
enlazar user32.lib. Es una linea mas de codigo y a cambio el parche no toca
la configuracion de enlazado del SDK, que es justo el tipo de cambio que
luego rompe una build ajena.

NO cambia el criterio de seleccion cuando SI hay GPU. Un equipo que hoy
funciona se comporta exactamente igual: la segunda pasada solo se ejecuta si
la primera se quedo sin candidatos.
"""

import argparse
import os
import pathlib
import shutil
import sys

MARCA = "PARCHE LOCAL - fallback de GPU y error visible"

ANCLA = """  // Choose the adapter.
  uint32_t adapter_index = 0;
  IDXGIAdapter1* adapter = nullptr;
  while (dxgi_factory->EnumAdapters1(adapter_index, &adapter) == S_OK) {
    DXGI_ADAPTER_DESC1 adapter_desc;
    if (SUCCEEDED(adapter->GetDesc1(&adapter_desc))) {
      if (SUCCEEDED(pfn_d3d12_create_device_(adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device),
                                             nullptr))) {
        if (REXCVAR_GET(d3d12_adapter) >= 0) {
          if (adapter_index == REXCVAR_GET(d3d12_adapter)) {
            break;
          }
        } else if (REXCVAR_GET(d3d12_adapter) == -2) {
          if (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            break;
          }
        } else {
          if (!(adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            break;
          }
        }
      }
    }
    adapter->Release();
    adapter = nullptr;
    ++adapter_index;
  }
  if (adapter == nullptr) {
    REXLOG_ERROR(
        "Failed to get an adapter supporting Direct3D 12 with the feature "
        "level of at least 11_0");
    dxgi_factory->Release();
    return false;
  }
"""

NUEVO = """  // ============ PARCHE LOCAL - fallback de GPU y error visible ============
  //
  // La busqueda es la de siempre -recorrer adaptadores y quedarse con el
  // primero que cree un dispositivo D3D12 a feature level 11_0-, solo que
  // ahora esta en una funcion para poder repetirla.
  //
  // Se le pasa si acepta adaptadores marcados como software. Con el valor por
  // defecto del cvar (-1) la primera pasada los rechaza, igual que antes; si
  // esa pasada se queda sin candidatos se repite aceptandolos, y ahi es donde
  // aparece WARP. Antes ese segundo intento no existia: el fallback estaba
  // implementado pero solo se alcanzaba escribiendo d3d12_adapter=-2 a mano.
  auto buscar_adaptador = [&](bool aceptar_software) -> IDXGIAdapter1* {
    uint32_t indice = 0;
    IDXGIAdapter1* candidato = nullptr;
    while (dxgi_factory->EnumAdapters1(indice, &candidato) == S_OK) {
      DXGI_ADAPTER_DESC1 desc;
      if (SUCCEEDED(candidato->GetDesc1(&desc))) {
        if (SUCCEEDED(pfn_d3d12_create_device_(candidato, D3D_FEATURE_LEVEL_11_0,
                                               _uuidof(ID3D12Device), nullptr))) {
          const bool es_software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
          if (REXCVAR_GET(d3d12_adapter) >= 0) {
            // El usuario pidio un indice concreto: se respeta tal cual.
            if (indice == uint32_t(REXCVAR_GET(d3d12_adapter))) {
              return candidato;
            }
          } else if (REXCVAR_GET(d3d12_adapter) == -2) {
            // El usuario pidio WARP explicitamente.
            if (es_software) {
              return candidato;
            }
          } else if (aceptar_software || !es_software) {
            return candidato;
          }
        }
      }
      candidato->Release();
      candidato = nullptr;
      ++indice;
    }
    return nullptr;
  };

  IDXGIAdapter1* adapter = buscar_adaptador(false);

  bool usando_software = false;
  if (adapter == nullptr && REXCVAR_GET(d3d12_adapter) == -1) {
    // Ninguna GPU fisica sirve. Antes se acababa aqui.
    REXLOG_WARN(
        "No hay ninguna GPU fisica con Direct3D 12 feature level 11_0. "
        "Probando el rasterizador por software (WARP).");
    adapter = buscar_adaptador(true);
    if (adapter != nullptr) {
      usando_software = true;
      REXLOG_WARN(
          "Usando WARP: el render lo hace la CPU. Va a ir MUY lento -unos "
          "pocos fotogramas por segundo- pero el juego arranca y se ve. "
          "Actualiza el driver de la GPU para volver a la aceleracion por "
          "hardware.");
    }
  }

  if (adapter == nullptr) {
    // Este mensaje se deja PALABRA POR PALABRA como estaba: hay scripts y
    // logs viejos que lo buscan por texto.
    REXLOG_ERROR(
        "Failed to get an adapter supporting Direct3D 12 with the feature "
        "level of at least 11_0");

    // Y ademas se dice en pantalla, porque a estas alturas la ventana todavia
    // no existe: sin esto el ejecutable simplemente no hace nada al abrirlo, y
    // quien lo recibe no tiene forma de distinguirlo de un archivo roto.
    //
    // Se carga user32 a mano en vez de enlazarla para no tocar la
    // configuracion de enlazado del SDK.
    if (HMODULE user32 = LoadLibraryA("user32.dll")) {
      using PFN_MessageBoxW = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);
      auto message_box =
          reinterpret_cast<PFN_MessageBoxW>(GetProcAddress(user32, "MessageBoxW"));
      if (message_box) {
        message_box(nullptr,
                    L"No se ha encontrado ninguna tarjeta grafica compatible.\\n"
                    L"\\n"
                    L"Hace falta Direct3D 12 con feature level 11_0. Eso lo\\n"
                    L"cumple practicamente cualquier GPU de 2012 en adelante,\\n"
                    L"asi que lo mas probable es que el problema sea el driver.\\n"
                    L"\\n"
                    L"Que probar, por orden:\\n"
                    L"  1. Actualizar el driver de la tarjeta grafica.\\n"
                    L"  2. Comprobar que Windows esta al dia.\\n"
                    L"  3. Si es un portatil con dos graficas, forzar que el\\n"
                    L"     juego use la dedicada.\\n"
                    L"\\n"
                    L"Hay mas detalle en la carpeta logs, junto al ejecutable.",
                    L"Need for Speed: Most Wanted", 0x00000010 /* MB_ICONERROR */);
      }
      FreeLibrary(user32);
    }

    dxgi_factory->Release();
    return false;
  }

  if (usando_software) {
    REXGPU_INFO("Adaptador elegido: WARP (rasterizador por software)");
  }
  // ====================== fin del parche local ============================
"""


def localizar_sdk():
    raiz = pathlib.Path(__file__).resolve().parent.parent
    # NFSMW_SDK apunta a otro arbol del SDK (el de Android, por ejemplo). Si
    # esta puesta se usa SOLO esa ruta: caer en silencio en el SDK de Windows
    # parchearia el arbol equivocado.
    otro = os.environ.get("NFSMW_SDK")
    candidatos = [pathlib.Path(otro)] if otro else [raiz.parent / "rexglue-sdk", raiz / "sdk"]
    for cand in candidatos:
        f = cand / "src" / "ui" / "d3d12" / "d3d12_provider.cpp"
        if f.exists():
            return f
    sys.exit("[ERROR] No encuentro src/ui/d3d12/d3d12_provider.cpp del SDK.\n"
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

    n = txt.count(ANCLA)
    if n != 1:
        sys.exit(f"[ERROR] El anclaje 'eleccion de adaptador' aparece {n} veces,\n"
                 f"        esperaba 1. El SDK habra cambiado. No he tocado nada.")

    # OJO: este fichero comparte el .original con parche_presentador.py? No.
    # Ese toca d3d12_presenter.cpp, este d3d12_provider.cpp. Son distintos.
    if not original.exists():
        shutil.copy2(f, original)
        print(f"[ok] Copia de seguridad: {original.name}")

    f.write_text(txt.replace(ANCLA, NUEVO), encoding="utf-8")
    print("[ok] Parche aplicado.")
    print()
    print("  Sin GPU valida -> se intenta WARP antes de rendirse")
    print("  Sin WARP        -> cuadro de dialogo explicando que pasa")
    print()
    print("  HAY QUE RECOMPILAR EL SDK para que sirva de algo:")
    print("    cmake --build out/build/win-amd64 --config Release --target install")
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
