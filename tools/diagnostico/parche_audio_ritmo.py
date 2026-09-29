#!/usr/bin/env python3
"""
A que ritmo va el descodificador XMA, y quien llega tarde: el juego o nosotros.

    python tools/diagnostico/parche_audio_ritmo.py            aplicar
    python tools/diagnostico/parche_audio_ritmo.py --estado
    python tools/diagnostico/parche_audio_ritmo.py --revertir

Toca un fichero del SDK:  src/audio/xma_context.cpp
No guarda .original: aplica y deshace por sustitucion de texto exacta.


PARA QUE
========

El audio se oye entrecortado en carrera y ya hay tres cosas descartadas CON
MEDIDAS, no por intuicion:

  - El codec no esta sin optimizar: FFmpeg se compila para ARM64 con
    ARCH_AARCH64, HAVE_NEON y el ensamblador NEON (fft_neon, mdct_neon,
    float_dsp_neon), que es lo que consume WMA Pro.
  - El planificador no lo ahoga: la espera por despertar de los hilos de audio
    es de 0,021 ms en menu y menos de 0,089 ms en carrera a 20 fps.
  - La salida esta sana: los contadores de underrun de AudioFlinger no crecen
    mientras se juega.
  - Y subir los fps de 20 a 35 (nfsmw_una_pasada) NO arreglo el audio, asi que
    tampoco es que el juego actualice el sonido una vez por fotograma.

Si la salida nunca se queda vacia y aun asi suena cortado, los huecos estan
DENTRO de lo que el juego mezcla. Y para eso solo hay dos culpables:

  A) el juego no le da datos de entrada al descodificador a tiempo
  B) el descodificador no produce a tiempo aunque tenga datos

Este parche los separa. Cada segundo escribe:

  [ritmo xma] N decodificaciones, M veces sin hueco de salida,
              K veces sin entrada, en L llamadas

  - "sin hueco de salida" alto  -> el descodificador va SOBRADO: llena el
    buffer y se le dice que vuelva luego. El problema no es el.
  - "sin entrada" alto          -> el juego no alimenta. Caso A.
  - las dos bajas y pocas decodificaciones -> caso B.
"""

import argparse
import os
import pathlib
import sys


FICHERO = "src/audio/xma_context.cpp"

BLOQUES = [
    (
        '''#include <algorithm>
#include <cstring>
''',
        '''#include <algorithm>
#include <atomic>  // RITMO XMA (tools/diagnostico/parche_audio_ritmo.py)
#include <chrono>  // RITMO XMA
#include <cstring>
''',
    ),
    (
        '''bool XmaContext::Work() {
  if (!is_allocated() || !is_enabled()) {
    return false;
  }
''',
        '''namespace {
// RITMO XMA (tools/diagnostico/parche_audio_ritmo.py): quien llega tarde.
// Atomicos porque Work() lo llaman dos hilos: el trabajador del SDK y el hilo
// del juego cuando da el kick.
struct NfsmwRitmo {
  std::atomic<uint32_t> llamadas{0};
  std::atomic<uint32_t> decodificaciones{0};
  std::atomic<uint32_t> sin_hueco{0};
  std::atomic<uint32_t> sin_entrada{0};
  std::atomic<uint32_t> salida_vacia{0};
};
NfsmwRitmo nfsmw_ritmo;

void NfsmwRitmoContar() {
  using Reloj = std::chrono::steady_clock;
  static std::atomic<int64_t> desde{0};
  const int64_t ahora = Reloj::now().time_since_epoch().count();
  int64_t antes = desde.load(std::memory_order_relaxed);
  if (antes == 0) {
    desde.compare_exchange_strong(antes, ahora);
    return;
  }
  const auto pasado = std::chrono::nanoseconds(ahora - antes);
  if (pasado < std::chrono::seconds(1)) {
    return;
  }
  if (!desde.compare_exchange_strong(antes, ahora)) {
    return;
  }
  REXAPU_INFO(
      "[ritmo xma] {} decodificaciones, {} sin hueco de salida, {} sin entrada, "
      "{} SALIDA VACIA, en {} llamadas",
      nfsmw_ritmo.decodificaciones.exchange(0), nfsmw_ritmo.sin_hueco.exchange(0),
      nfsmw_ritmo.sin_entrada.exchange(0), nfsmw_ritmo.salida_vacia.exchange(0),
      nfsmw_ritmo.llamadas.exchange(0));
}
}  // namespace

bool XmaContext::Work() {
  if (!is_allocated() || !is_enabled()) {
    return false;
  }
  nfsmw_ritmo.llamadas.fetch_add(1, std::memory_order_relaxed);  // RITMO XMA
  NfsmwRitmoContar();                                            // RITMO XMA
''',
    ),
    (
        '''  if (minimum_subframe_decode_count > remaining_subframe_blocks_in_output_buffer_) {
    StoreContextMerged(data, initial_data, context_ptr);
    return true;
  }
''',
        '''  if (minimum_subframe_decode_count > remaining_subframe_blocks_in_output_buffer_) {
    // RITMO XMA: no cabe mas salida. El descodificador va por delante.
    nfsmw_ritmo.sin_hueco.fetch_add(1, std::memory_order_relaxed);
    StoreContextMerged(data, initial_data, context_ptr);
    return true;
  }
''',
    ),
    (
        '''  while (remaining_subframe_blocks_in_output_buffer_ >= minimum_subframe_decode_count) {
    Decode(&data);
    Consume(&output_rb, &data);
''',
        '''  while (remaining_subframe_blocks_in_output_buffer_ >= minimum_subframe_decode_count) {
    nfsmw_ritmo.decodificaciones.fetch_add(1, std::memory_order_relaxed);  // RITMO XMA
    Decode(&data);
    Consume(&output_rb, &data);
''',
    ),
    (
        '''  if (output_rb.empty()) {
    data.output_buffer_valid = 0;
  }
''',
        '''  if (output_rb.empty()) {
    // RITMO XMA: la salida se ha quedado SIN NADA. Esto es un hueco audible:
    // el juego va a leer y no hay muestras que leer.
    nfsmw_ritmo.salida_vacia.fetch_add(1, std::memory_order_relaxed);
    data.output_buffer_valid = 0;
  }
''',
    ),
    (
        '''    if ((!data.IsAnyInputBufferValid() || data.error_status == 4) &&
        current_frame_remaining_subframes_ == 0) {
      break;
    }
''',
        '''    if ((!data.IsAnyInputBufferValid() || data.error_status == 4) &&
        current_frame_remaining_subframes_ == 0) {
      // RITMO XMA: se acabo la entrada. El juego no ha alimentado a tiempo.
      nfsmw_ritmo.sin_entrada.fetch_add(1, std::memory_order_relaxed);
      break;
    }
''',
    ),
]


def localizar_sdk():
    raiz = pathlib.Path(__file__).resolve().parent.parent.parent
    # NFSMW_SDK apunta a otro arbol del SDK (el de Android, por ejemplo). Si
    # esta puesta se usa SOLO esa ruta: caer en silencio en el SDK de Windows
    # parchearia el arbol equivocado.
    otro = os.environ.get("NFSMW_SDK")
    candidatos = [pathlib.Path(otro)] if otro else [raiz.parent / "rexglue-sdk", raiz / "sdk"]
    for cand in candidatos:
        if (cand / FICHERO).exists():
            return cand
    sys.exit(f"[ERROR] No encuentro {FICHERO} del SDK.\n"
             "        Se busca en NFSMW_SDK, o en ..\\rexglue-sdk y .\\sdk")


def leer(f):
    with open(f, encoding="utf-8", newline="") as h:
        txt = h.read()
    eol = "\r\n" if "\r\n" in txt else "\n"
    return txt.replace("\r\n", "\n"), eol


def escribir(f, txt, eol):
    with open(f, "w", encoding="utf-8", newline="") as h:
        h.write(txt.replace("\n", eol))


def main():
    p = argparse.ArgumentParser(add_help=True)
    p.add_argument("--estado", action="store_true")
    p.add_argument("--revertir", action="store_true")
    args = p.parse_args()

    f = localizar_sdk() / FICHERO
    txt, eol = leer(f)
    puestos = sum(1 for _, nuevo in BLOQUES if nuevo in txt)

    if args.estado:
        estado = ("aplicado" if puestos == len(BLOQUES) else "sin aplicar" if puestos == 0
                  else f"A MEDIAS ({puestos}/{len(BLOQUES)})")
        print(f"  audio_ritmo                {estado}")
        return 0

    if args.revertir:
        if puestos == 0:
            print("[ok] audio_ritmo: no habia nada puesto")
            return 0
        if puestos != len(BLOQUES):
            sys.exit(f"[ERROR] Solo encuentro {puestos} de {len(BLOQUES)} bloques. No quito nada "
                     "a medias: mira el SDK a mano.")
        for ancla, nuevo in BLOQUES:
            txt = txt.replace(nuevo, ancla)
        escribir(f, txt, eol)
        print(f"[ok] Quitados {puestos} bloques")
        return 0

    if puestos == len(BLOQUES):
        print("[ok] audio_ritmo: ya estaba")
        return 0
    if puestos:
        sys.exit(f"[ERROR] audio_ritmo esta a medias ({puestos}/{len(BLOQUES)}). "
                 "Revierte primero con --revertir.")
    for ancla, _ in BLOQUES:
        n = txt.count(ancla)
        if n != 1:
            sys.exit(f"[ERROR] Un anclaje aparece {n} veces en {FICHERO}, esperaba 1:\n"
                     f"        {ancla.splitlines()[0].strip()}\n"
                     "        El SDK habra cambiado. No he tocado nada.")
    for ancla, nuevo in BLOQUES:
        txt = txt.replace(ancla, nuevo, 1)
    escribir(f, txt, eol)
    print("[ok] Aplicado: [ritmo xma] una linea por segundo en el log")
    return 0


if __name__ == "__main__":
    sys.exit(main())
