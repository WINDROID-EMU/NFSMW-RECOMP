#!/usr/bin/env python3
"""
Cuantas muestras de audio se estan RECORTANDO al bajar de 5.1 a estereo.

    python tools/diagnostico/parche_audio_clip.py            aplicar
    python tools/diagnostico/parche_audio_clip.py --estado
    python tools/diagnostico/parche_audio_clip.py --revertir

Toca un fichero del SDK:  include/rex/audio/conversion.h
No guarda .original: aplica y deshace por sustitucion de texto exacta.


PARA QUE
========

El audio se oye entrecortado, y TODO el transporte mide sano:

  - El codec no esta sin optimizar (FFmpeg ARM64 con NEON de verdad).
  - El planificador no lo ahoga (espera por despertar de 0,02-0,09 ms).
  - Los underruns de AudioFlinger no crecen jugando.
  - El descodificador XMA no se atasca ni se queda sin hueco de salida.
  - Cuando el juego consulta si tiene audio, 0 % de consultas secas.
  - Y la salida no entrega silencio por falta de datos: 0 % de rellenos.

O sea que no se pierde ni un fotograma de audio. Si no hay huecos pero suena
mal, lo siguiente a descartar no son cortes sino DISTORSION.

Y hay un sitio donde puede aparecer. En Android se fuerza la salida a estereo
(tools/parche_audio.py) y el 5.1 del juego se pliega con estos pesos:

    center = 0,707   surround = 0,707   lfe = 0   scale = 0,586

    salida = recortar((fl + fc*0,707 + bl*0,707) * 0,586, -1, 1)

Ese scale (1/(1+0,707)) normaliza para frontal+centro O frontal+surround, pero
no para los tres a la vez: con los tres a tope da 1,41, y se recorta a 1,0. Son
hasta +3 dB de margen que se comen recortando, o sea clipping duro. En carrera
-motor, musica y efectos a la vez- eso seria constante; en menus, casi nada.

Este parche cuenta las muestras recortadas, que es objetivo y no depende del
oido de nadie. Cada ~200 vueltas del callback (algo menos de un segundo):

  [clip audio] N de M muestras recortadas (P%), pico X

  - P notable en carrera y casi cero en menu -> es esto: hay que bajar el
    scale del plegado o meter un limitador, no tocar el descodificador.
  - P cero -> tampoco es distorsion del plegado, y hay que mirar lo que el
    juego mezcla antes de llegar aqui.
"""

import argparse
import os
import pathlib
import sys


FICHERO = "include/rex/audio/conversion.h"

ANCLA_INC = """#include <algorithm>
#include <cstdint>
"""

NUEVO_INC = """#include <algorithm>
#include <atomic>  // CLIP AUDIO (tools/diagnostico/parche_audio_clip.py)
#include <cstdint>

#include <rex/logging.h>  // CLIP AUDIO
"""

ANCLA = """inline void sequential_6_BE_to_interleaved_2_LE(float* output, const float* input,
                                                size_t ch_sample_count, const StereoFold& fold,
                                                float gain) {
  // Default 5.1 channel mapping is fl, fr, fc, lf, bl, br
  // https://docs.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-default-channel-mapping
  const float scale = fold.scale * gain;
  for (size_t sample = 0; sample < ch_sample_count; sample++) {
    const float fl = rex::byte_swap(input[0 * ch_sample_count + sample]);
    const float fr = rex::byte_swap(input[1 * ch_sample_count + sample]);
    const float fc = rex::byte_swap(input[2 * ch_sample_count + sample]);
    const float lf = rex::byte_swap(input[3 * ch_sample_count + sample]);
    const float bl = rex::byte_swap(input[4 * ch_sample_count + sample]);
    const float br = rex::byte_swap(input[5 * ch_sample_count + sample]);
    // Center and LFE land on both sides.
    const float mid = fc * fold.center + lf * fold.lfe;
    output[sample * 2] = std::clamp((fl + mid + bl * fold.surround) * scale, -1.0f, 1.0f);
    output[sample * 2 + 1] = std::clamp((fr + mid + br * fold.surround) * scale, -1.0f, 1.0f);
  }
}
"""

NUEVO = """// CLIP AUDIO (tools/diagnostico/parche_audio_clip.py): contadores del plegado.
inline std::atomic<uint64_t> nfsmw_clip_muestras{0};
inline std::atomic<uint64_t> nfsmw_clip_recortadas{0};
inline std::atomic<uint32_t> nfsmw_clip_pico_milis{0};
inline std::atomic<uint32_t> nfsmw_clip_vueltas{0};

inline void sequential_6_BE_to_interleaved_2_LE(float* output, const float* input,
                                                size_t ch_sample_count, const StereoFold& fold,
                                                float gain) {
  // Default 5.1 channel mapping is fl, fr, fc, lf, bl, br
  // https://docs.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-default-channel-mapping
  const float scale = fold.scale * gain;
  uint64_t nfsmw_recortadas = 0;  // CLIP AUDIO
  float nfsmw_pico = 0.0f;        // CLIP AUDIO
  for (size_t sample = 0; sample < ch_sample_count; sample++) {
    const float fl = rex::byte_swap(input[0 * ch_sample_count + sample]);
    const float fr = rex::byte_swap(input[1 * ch_sample_count + sample]);
    const float fc = rex::byte_swap(input[2 * ch_sample_count + sample]);
    const float lf = rex::byte_swap(input[3 * ch_sample_count + sample]);
    const float bl = rex::byte_swap(input[4 * ch_sample_count + sample]);
    const float br = rex::byte_swap(input[5 * ch_sample_count + sample]);
    // Center and LFE land on both sides.
    const float mid = fc * fold.center + lf * fold.lfe;
    const float izq = (fl + mid + bl * fold.surround) * scale;
    const float der = (fr + mid + br * fold.surround) * scale;
    // CLIP AUDIO: mirar el valor ANTES de recortar.
    const float mayor = std::max(std::abs(izq), std::abs(der));
    if (mayor > 1.0f) {
      ++nfsmw_recortadas;
    }
    if (mayor > nfsmw_pico) {
      nfsmw_pico = mayor;
    }
    output[sample * 2] = std::clamp(izq, -1.0f, 1.0f);
    output[sample * 2 + 1] = std::clamp(der, -1.0f, 1.0f);
  }

  // CLIP AUDIO: una linea cada ~200 vueltas, que a 188 por segundo es algo mas
  // de un segundo. No se toca el reloj dentro del callback de audio.
  nfsmw_clip_muestras.fetch_add(ch_sample_count, std::memory_order_relaxed);
  nfsmw_clip_recortadas.fetch_add(nfsmw_recortadas, std::memory_order_relaxed);
  const uint32_t milis = static_cast<uint32_t>(nfsmw_pico * 1000.0f);
  uint32_t pico_visto = nfsmw_clip_pico_milis.load(std::memory_order_relaxed);
  while (milis > pico_visto &&
         !nfsmw_clip_pico_milis.compare_exchange_weak(pico_visto, milis,
                                                      std::memory_order_relaxed)) {
  }
  if (nfsmw_clip_vueltas.fetch_add(1, std::memory_order_relaxed) >= 200) {
    nfsmw_clip_vueltas.store(0, std::memory_order_relaxed);
    const uint64_t total = nfsmw_clip_muestras.exchange(0, std::memory_order_relaxed);
    const uint64_t rec = nfsmw_clip_recortadas.exchange(0, std::memory_order_relaxed);
    const uint32_t pico = nfsmw_clip_pico_milis.exchange(0, std::memory_order_relaxed);
    REXAPU_INFO("[clip audio] {} de {} muestras recortadas ({}%), pico {}.{:03}", rec, total,
                total ? rec * 100 / total : 0, pico / 1000, pico % 1000);
  }
}
"""

BLOQUES = [
    (ANCLA_INC, NUEVO_INC),
    (ANCLA, NUEVO),
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
        print(f"  audio_clip                 {estado}")
        return 0

    if args.revertir:
        if puestos == 0:
            print("[ok] audio_clip: no habia nada puesto")
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
        print("[ok] audio_clip: ya estaba")
        return 0
    if puestos:
        sys.exit(f"[ERROR] audio_clip esta a medias ({puestos}/{len(BLOQUES)}). "
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
    print("[ok] Aplicado: [clip audio] cuenta las muestras recortadas al plegar a estereo")
    return 0


if __name__ == "__main__":
    sys.exit(main())
