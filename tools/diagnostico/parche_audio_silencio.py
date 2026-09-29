#!/usr/bin/env python3
"""
Cuantas veces por segundo la salida de audio se rellena con SILENCIO.

    python tools/diagnostico/parche_audio_silencio.py            aplicar
    python tools/diagnostico/parche_audio_silencio.py --estado
    python tools/diagnostico/parche_audio_silencio.py --revertir

Toca un fichero del SDK:  src/audio/sdl/sdl_audio_driver.cpp
Va DESPUES de tools/parche_audio.py, sobre ese mismo fichero.

No guarda .original: aplica y deshace por sustitucion de texto exacta.


PARA QUE
========

El audio se oye entrecortado y todo lo de arriba del camino mide SANO:

  - El codec no esta sin optimizar (FFmpeg ARM64 con NEON de verdad).
  - El planificador no lo ahoga (espera por despertar de 0,02-0,09 ms).
  - Los underruns de AudioFlinger no crecen mientras se juega.
  - El descodificador XMA no se atasca ni se queda sin hueco de salida.
  - Y cuando el JUEGO consulta si tiene audio, nunca se lo encuentra vacio:
    0 % de consultas secas, igual en menu que en carrera.

La pieza que faltaba esta en el callback de SDL:

    if (driver->frames_queued_.empty()) {
      std::memset(data, 0, len);     // <- silencio
      ...
    }

Cuando la cola de fotogramas de audio se queda vacia, se entrega SILENCIO. Y
eso solo se apunta en el log las 10 primeras veces en toda la partida. Por eso
AudioFlinger no ve ni un underrun -siempre le damos datos, solo que mudos- y
por eso el corte no aparecia en ninguna de las medidas anteriores.

Este parche cuenta las dos cosas por segundo:

  [silencio audio] N rellenos con silencio de M vueltas (P%)

  - P alto  -> el juego no entrega fotogramas de audio al ritmo que el
    dispositivo los consume. El corte es este, y hay que ir a por el ritmo de
    entrega (cola mas profunda, o entregar antes).
  - P cero  -> el hueco no esta aqui tampoco y hay que mirar el contenido que
    entrega el juego, no el transporte.
"""

import argparse
import os
import pathlib
import sys


FICHERO = "src/audio/sdl/sdl_audio_driver.cpp"

ANCLA = """    if (driver->frames_queued_.empty()) {
      if (sdl_callback_count < 10) {
        REXAPU_DEBUG("SDLCallback: no frames queued (silence)");
        sdl_callback_count++;
      }
      std::memset(data, 0, len);
"""

NUEVO = """    if (driver->frames_queued_.empty()) {
      if (sdl_callback_count < 10) {
        REXAPU_DEBUG("SDLCallback: no frames queued (silence)");
        sdl_callback_count++;
      }
      // SILENCIO AUDIO (tools/diagnostico/parche_audio_silencio.py): esto es
      // el corte. Aqui se entrega silencio porque el juego no ha dejado
      // fotograma, y el dispositivo lo consume tan contento: por eso no
      // aparece como underrun en ningun sitio.
      NfsmwSilencioContar(true);
      std::memset(data, 0, len);
"""

ANCLA_CONTADOR = """void SDLAudioDriver::SDLCallback(void* userdata, SDL_AudioStream* stream, int additional_amount,
"""

NUEVO_CONTADOR = """namespace {
// SILENCIO AUDIO (tools/diagnostico/parche_audio_silencio.py).
void NfsmwSilencioContar(bool mudo) {
  static std::atomic<uint32_t> vueltas{0};
  static std::atomic<uint32_t> mudas{0};
  static std::atomic<int64_t> desde{0};

  vueltas.fetch_add(1, std::memory_order_relaxed);
  if (mudo) {
    mudas.fetch_add(1, std::memory_order_relaxed);
  }

  const int64_t ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
  int64_t antes = desde.load(std::memory_order_relaxed);
  if (antes == 0) {
    desde.compare_exchange_strong(antes, ahora);
    return;
  }
  if (ahora - antes < 1000 || !desde.compare_exchange_strong(antes, ahora)) {
    return;
  }
  const uint32_t n = vueltas.exchange(0);
  const uint32_t m = mudas.exchange(0);
  REXAPU_INFO("[silencio audio] {} rellenos con silencio de {} vueltas ({}%)", m, n,
              n ? m * 100 / n : 0);
}
}  // namespace

void SDLAudioDriver::SDLCallback(void* userdata, SDL_AudioStream* stream, int additional_amount,
"""

ANCLA_BUENA = """      driver->frames_unused_.push(buffer);

      auto ret = driver->semaphore_->Release(1, nullptr);
"""

NUEVO_BUENA = """      driver->frames_unused_.push(buffer);
      NfsmwSilencioContar(false);  // SILENCIO AUDIO: esta vuelta si llevaba sonido

      auto ret = driver->semaphore_->Release(1, nullptr);
"""

ANCLA_INC = """#include <rex/logging.h>
"""

NUEVO_INC = """#include <atomic>  // SILENCIO AUDIO (tools/diagnostico/parche_audio_silencio.py)
#include <chrono>  // SILENCIO AUDIO

#include <rex/logging.h>
"""

BLOQUES = [
    (ANCLA_INC, NUEVO_INC),
    (ANCLA_CONTADOR, NUEVO_CONTADOR),
    (ANCLA, NUEVO),
    (ANCLA_BUENA, NUEVO_BUENA),
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
        print(f"  audio_silencio             {estado}")
        return 0

    if args.revertir:
        if puestos == 0:
            print("[ok] audio_silencio: no habia nada puesto")
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
        print("[ok] audio_silencio: ya estaba")
        return 0
    if puestos:
        sys.exit(f"[ERROR] audio_silencio esta a medias ({puestos}/{len(BLOQUES)}). "
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
    print("[ok] Aplicado: [silencio audio] una linea por segundo en el log")
    return 0


if __name__ == "__main__":
    sys.exit(main())
