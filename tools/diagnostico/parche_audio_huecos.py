#!/usr/bin/env python3
"""
Cuantas veces por segundo el juego pide audio y no hay nada que darle.

    python tools/diagnostico/parche_audio_huecos.py            aplicar
    python tools/diagnostico/parche_audio_huecos.py --estado
    python tools/diagnostico/parche_audio_huecos.py --revertir

Toca un fichero del SDK:  src/kernel/xboxkrnl/xboxkrnl_audio_xma.cpp
Va DESPUES de tools/parche_anillo.py y tools/parche_desatasco.py, sobre ese
mismo fichero.

No guarda .original: aplica y deshace por sustitucion de texto exacta.


PARA QUE
========

Se mide el hueco DONDE SE OYE: en el lado del juego, no en el nuestro.

Lo que ya esta descartado con medidas:

  - El codec no esta sin optimizar (FFmpeg ARM64 con NEON de verdad).
  - El planificador no lo ahoga (espera por despertar de 0,02-0,09 ms).
  - La salida esta sana (los underruns de AudioFlinger no crecen jugando).
  - No va atado a los fps (subir de 20 a 35 no mejoro el audio).

Y del lado del descodificador se sabe que nunca va sobrado: "sin hueco de
salida" sale 0 siempre, o sea que el juego se lo come todo segun se produce.

Falta el dato que cierra el circulo: cuando el juego consulta el offset de
escritura -que es lo que hace sin parar mientras mezcla-, cuantas veces se
encuentra con CERO bloques disponibles. Eso es un hueco audible. El parche de
desatasco ya vigila esta misma funcion, pero solo salta si la cosa dura mas de
250 ms; aqui se cuentan los cortos, que son los que se oyen como cortes.

Cada segundo escribe:

  [huecos xma] N consultas, M secas (P%), K voces distintas

  - "secas" alto en carrera y bajo en menu -> el juego se queda sin audio
    porque el descodificador no llega. Caso nuestro.
  - "secas" parecido en los dos -> es normal del protocolo y el corte viene de
    otro sitio (mezcla del juego, volumenes, streaming).
"""

import argparse
import os
import pathlib
import sys


FICHERO = "src/kernel/xboxkrnl/xboxkrnl_audio_xma.cpp"

ANCLA = """u32 XMAGetOutputBufferWriteOffset_entry(mapped_void context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
"""

NUEVO = """u32 XMAGetOutputBufferWriteOffset_entry(mapped_void context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);

  // HUECOS XMA (tools/diagnostico/parche_audio_huecos.py): contar cuantas
  // veces el juego consulta esto y no hay ni un bloque que darle. Es el hueco
  // en el sitio donde se oye. Esta en un bucle muy caliente, asi que son
  // sumas atomicas relajadas y una linea por segundo, nada mas.
  {
    static std::atomic<uint32_t> consultas{0};
    static std::atomic<uint32_t> secas{0};
    static std::atomic<int64_t> desde{0};

    const uint32_t escritura = context.output_buffer_write_offset;
    const uint32_t lectura = context.output_buffer_read_offset;
    consultas.fetch_add(1, std::memory_order_relaxed);
    if (context.output_buffer_valid && escritura == lectura) {
      secas.fetch_add(1, std::memory_order_relaxed);
    }

    const int64_t ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
    int64_t antes = desde.load(std::memory_order_relaxed);
    if (antes == 0) {
      desde.compare_exchange_strong(antes, ahora);
    } else if (ahora - antes >= 1000 && desde.compare_exchange_strong(antes, ahora)) {
      const uint32_t n = consultas.exchange(0);
      const uint32_t s = secas.exchange(0);
      REXLOG_INFO("[huecos xma] {} consultas, {} secas ({}%)", n, s, n ? s * 100 / n : 0);
    }
  }
"""


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
    puesto = NUEVO in txt

    if args.estado:
        print(f"  audio_huecos               {'aplicado' if puesto else 'sin aplicar'}")
        return 0

    if args.revertir:
        if not puesto:
            print("[ok] audio_huecos: no habia nada puesto")
            return 0
        escribir(f, txt.replace(NUEVO, ANCLA), eol)
        print("[ok] Quitado")
        return 0

    if puesto:
        print("[ok] audio_huecos: ya estaba")
        return 0
    n = txt.count(ANCLA)
    if n != 1:
        sys.exit(f"[ERROR] El anclaje aparece {n} veces en {FICHERO}, esperaba 1.\n"
                 "        Aplica antes parche_anillo.py y parche_desatasco.py. No he tocado nada.")
    escribir(f, txt.replace(ANCLA, NUEVO, 1), eol)
    print("[ok] Aplicado: [huecos xma] una linea por segundo en el log")
    return 0


if __name__ == "__main__":
    sys.exit(main())
