#!/usr/bin/env python3
"""
Instrumenta la cadena de audio entera.  (version 3)

    python tools/parche_xma.py            aplicar
    python tools/parche_xma.py --estado
    python tools/parche_xma.py --revertir

Toca tres ficheros del SDK:

    src/audio/xma_context.cpp        el descodificador
    src/audio/audio_system.cpp       el hilo que llama al juego
    src/audio/sdl/sdl_audio_driver.cpp   la salida a la tarjeta

Guarda un .original de cada uno la primera vez y es idempotente. Si detecta
una version anterior del parche, restaura desde el .original antes de aplicar
esta, para que los anclajes encajen sobre el codigo limpio.


LO QUE YA SABEMOS, Y POR QUE HACE FALTA UNA v3
==============================================

El cuelgue esta localizado: el hilo 0xD del juego gira entre
XMAGetOutputBufferWriteOffset y XMAGetOutputBufferReadOffset esperando audio
descodificado que no llega. La muerte del audio y el cuelgue al volver al menu
son EL MISMO FALLO, no dos.

La v2 ya midio lo importante: 6739 kicks, 7160 pases de Work, y 1232 salidas
tempranas TODAS por output_buffer_valid == 0. La rama de "no cabe" -que era mi
sospechosa- no se disparo ni una vez. Y luego el XMA se para en seco.

Pero la v2 dejo dos huecos, los dos mios:

  1. El log del estado de Work lo puse ANTES de PrepareOutputRingBuffer. Y
     resulta que PrepareOutputRingBuffer es justo quien recalcula
     remaining_subframe_blocks_in_output_buffer_ a partir de los offsets. O
     sea que el "hueco" que salia era el valor SOBRANTE del pase anterior.
     Salia 0 siempre y no significaba nada. En la v3 va despues.

  2. No se veia el otro lado de la cadena. Y ahi esta el resto de la historia.


LA CADENA COMPLETA, Y DONDE SE ROMPE
====================================

El audio de la 360 pasa por tres piezas, y cada una espera a la anterior:

    juego  ->  XmaContext::Work()  ->  frames_queued_  ->  SDLCallback
      ^                                                        |
      |                    semaforo, una suelta por frame       |
      +--------------------------------------------------------+

SDLCallback suelta el semaforo SOLO cuando consume un frame de verdad. Si la
cola se vacia, no suelta nada; entonces el WaitAny del AudioWorker se agota a
los 500 ms y NO llama al callback del juego; y si no se llama al juego, el
juego no entrega mas audio. Es un anillo, y con que se pare un eslabon se
paran los tres.

El log de la v2 encaja con eso al detalle: queued_count=8 a las 22:31:25,
nada mas despues, y "no frames queued (silence)" desde las 22:31:52. Los 8
frames se gastaron y no llego ninguno mas.

Lo que NO se puede saber con la v2 es si el AudioWorker siguio llamando al
juego despues del cuelgue, porque el SDK tiene esos dos contadores topados:

    if (diag_pump_count < 10)      en audio_system.cpp
    if (sdl_callback_count < 10)   en sdl_audio_driver.cpp

A las diez lineas se callan para siempre. Justo antes del fallo. Por eso la
v3 los cambia por un latido de una linea por segundo: no inunda el log y no
se calla nunca, que es exactamente lo que hace falta aqui.


QUE VA A CONTESTAR
==================

Con las tres piezas instrumentadas, la ultima linea de cada una antes del
silencio dice quien se paro primero:

  - si dejan de salir kicks     -> el juego dejo de pedir audio
  - si siguen los kicks pero Work se sale -> el descodificador se atasca
  - si el latido del worker sigue vivo pero con envios=0 congelado -> el
    semaforo no se suelta, y el eslabon roto es la salida
  - si el latido para del todo -> el propio hilo de audio esta bloqueado

Todo detras de log_noisy salvo el latido, que va a DEBUG porque es una linea
por segundo y es la que importa. LOG_DETALLADO.bat ya enciende las dos cosas.
"""

import argparse
import os
import pathlib
import shutil
import sys

MARCA = "PARCHE LOCAL - instrumentacion de audio v4"

# Marcas de versiones anteriores. Si aparece alguna, se restaura el fichero
# desde su .original antes de aplicar, porque los anclajes de abajo estan
# escritos contra el codigo LIMPIO del SDK y no encajarian sobre el parcheado.
MARCAS_VIEJAS = [
    "PARCHE LOCAL - instrumentacion de audio v3",
    "PARCHE LOCAL - el anillo de salida no se llena del todo",
    "PARCHE LOCAL - instrumentacion del XMA v2",
    "PARCHE LOCAL - instrumentacion de audio v2",
    "PARCHE LOCAL - el descodificador XMA no puede dormirse para siempre",
]

# ---------------------------------------------------------------------------
#  1) src/audio/xma_context.cpp   -  el descodificador
# ---------------------------------------------------------------------------

ENABLE_ANCLA = """void XmaContext::Enable() {
  std::lock_guard<std::mutex> lock(lock_);
  set_is_enabled(true);
}
"""

ENABLE_NUEVO = """void XmaContext::Enable() {
  std::lock_guard<std::mutex> lock(lock_);
  set_is_enabled(true);
  // PARCHE LOCAL - instrumentacion de audio v4
  //
  // Un "kick" del juego. Es lo unico que vuelve a encender un contexto:
  // Work() lo apaga tras un solo pase. Si estas lineas dejan de salir antes
  // del cuelgue, el juego dejo de pedir audio y el fallo no esta aqui.
  REXAPU_NOISY_DEBUG("XmaContext {}: kick (Enable)", id());
}
"""

WORK_ANCLA = """  if (!data.output_buffer_valid) {
    return true;
  }

  memory::RingBuffer output_rb = PrepareOutputRingBuffer(&data);
"""

WORK_NUEVO = """  if (!data.output_buffer_valid) {
    // PARCHE LOCAL - salida temprana 1 de 2. Devuelve "hecho" sin descodificar.
    //
    // OJO: esta rama sale SIN llamar a StoreContextMerged, o sea que no
    // escribe nada de vuelta en la memoria del juego. En la v2 fue la unica
    // que se disparo, 1232 veces.
    REXAPU_NOISY_DEBUG("XmaContext {}: se sale sin descodificar - salida no valida", id());
    return true;
  }

  memory::RingBuffer output_rb = PrepareOutputRingBuffer(&data);

  // PARCHE LOCAL - instrumentacion de audio v4
  //
  // Este log tiene que ir DESPUES de PrepareOutputRingBuffer y no antes. En
  // la v2 lo puse antes y el "hueco" que salia era el valor SOBRANTE de la
  // llamada anterior, no el de ahora: PrepareOutputRingBuffer es justo quien
  // recalcula remaining_subframe_blocks_in_output_buffer_ a partir de los
  // offsets. Salia 0 constantemente y no significaba nada.
  //
  // Los dos offsets son los mismos que el juego consulta en bucle mientras
  // esta colgado, asi que aqui se ve la otra mitad de esa conversacion.
  REXAPU_NOISY_DEBUG(
      "XmaContext {}: Work escritura={} lectura={} hueco={} sdc={} relleno={}", id(),
      uint32_t(data.output_buffer_write_offset), uint32_t(data.output_buffer_read_offset),
      remaining_subframe_blocks_in_output_buffer_, uint32_t(data.subframe_decode_count),
      uint32_t(data.output_buffer_padding));
"""

# La linea original de PrepareOutputRingBuffer se movio dentro del bloque de
# arriba, asi que hay que quitar la que quedaba suelta unas lineas mas abajo.
# Si no, se llamaria dos veces y la segunda pisaria los offsets del ring.
DUPLICADO_ANCLA = """  memory::RingBuffer output_rb = PrepareOutputRingBuffer(&data);

  // Consume-only context: no input, just drain remaining subframes.
"""

DUPLICADO_NUEVO = """  // Consume-only context: no input, just drain remaining subframes.
"""

HUECO_ANCLA = """  if (minimum_subframe_decode_count > remaining_subframe_blocks_in_output_buffer_) {
    StoreContextMerged(data, initial_data, context_ptr);
    return true;
  }
"""

HUECO_NUEVO = """  if (minimum_subframe_decode_count > remaining_subframe_blocks_in_output_buffer_) {
    // PARCHE LOCAL - salida temprana 2 de 2.
    //
    // Significa "no hay hueco en el buffer de salida para descodificar". Era
    // mi sospechosa en la v2 y NO se disparo ni una vez, asi que se queda
    // instrumentada solo para poder descartarla otra vez de un vistazo.
    REXAPU_NOISY_DEBUG(
        "XmaContext {}: se sale sin descodificar - no cabe (necesita {} bloques, hay {})", id(),
        minimum_subframe_decode_count, remaining_subframe_blocks_in_output_buffer_);
    StoreContextMerged(data, initial_data, context_ptr);
    return true;
  }
"""

NADA_ANCLA = """  data.output_buffer_write_offset = output_rb.write_offset() / kOutputBytesPerBlock;

  if (output_rb.empty()) {
    data.output_buffer_valid = 0;
  }
"""

NADA_NUEVO = """  data.output_buffer_write_offset = output_rb.write_offset() / kOutputBytesPerBlock;

  if (output_rb.empty()) {
    data.output_buffer_valid = 0;
  }

  // PARCHE LOCAL - instrumentacion de audio v4
  //
  // LA PREGUNTA DE ESTA VERSION. En el cuelgue, este contexto tenia sitio de
  // sobra en la salida -hueco=19 de 24- y aun asi la escritura no se movio ni
  // un bloque. Si hay sitio y no produce, es que no tiene NADA QUE
  // DESCODIFICAR: el bucle de decodificacion se rompe en cuanto
  // IsAnyInputBufferValid() da falso.
  //
  // Asi que aqui se registra cada pase que no produjo una sola muestra, con
  // el estado de los dos buffers de entrada. Si salen los dos a cero, el juego
  // dejo de darle datos y el fallo esta arriba, en quien alimenta; si salen a
  // uno, el problema es del descodificador y hay que mirar error_status.
  if (data.output_buffer_write_offset == initial_data.output_buffer_write_offset) {
    REXAPU_NOISY_DEBUG(
        "XmaContext {}: NO PRODUJO NADA - ent0={} ent1={} err={} hueco={} escritura={} lectura={}",
        id(), uint32_t(data.input_buffer_0_valid), uint32_t(data.input_buffer_1_valid),
        uint32_t(data.error_status), remaining_subframe_blocks_in_output_buffer_,
        uint32_t(data.output_buffer_write_offset), uint32_t(data.output_buffer_read_offset));
  }
"""

XMA_ANCLAS = [
    ("kick del juego", ENABLE_ANCLA, ENABLE_NUEVO),
    ("pase que no produjo nada", NADA_ANCLA, NADA_NUEVO),
    ("estado al entrar en Work", WORK_ANCLA, WORK_NUEVO),
    ("PrepareOutputRingBuffer duplicado", DUPLICADO_ANCLA, DUPLICADO_NUEVO),
    ("salida por falta de hueco", HUECO_ANCLA, HUECO_NUEVO),
]

# ---------------------------------------------------------------------------
#  2) src/audio/audio_system.cpp  -  el hilo que llama al juego
# ---------------------------------------------------------------------------

LATIDO_ANCLA = """  // Main run loop.
  uint32_t diag_pump_count = 0;
  while (worker_running_) {
"""

LATIDO_NUEVO = """  // Main run loop.
  uint32_t diag_pump_count = 0;

  // PARCHE LOCAL - instrumentacion de audio v4, latido del hilo de audio.
  //
  // Los contadores de diagnostico que ya traia el SDK -diag_pump_count y su
  // gemelo en sdl_audio_driver.cpp- se topan a diez lineas y luego se callan
  // PARA SIEMPRE. Con eso se ve el arranque y nada mas, y aqui lo que hace
  // falta es justo lo contrario: saber si este hilo sigue vivo DESPUES de que
  // el audio se muera, que es cuando el SDK ya lleva rato mudo.
  //
  // Una linea por segundo no inunda nada y no se calla nunca. Va a DEBUG y no
  // a NOISY a proposito: es la linea que importa.
  //
  //   esperas  vueltas del bucle
  //   exitos   veces que un semaforo se solto -o sea, que SDL consumio audio-
  //   plazos   veces que WaitAny se agoto a los 500 ms sin senal ninguna
  //   fallos   errores del WaitAny
  //   envios   llamadas al callback del juego que volvieron
  //
  // Como leerlo cuando se cuelgue:
  //   plazos sube y exitos congelado -> SDL no consume: la cola de frames se
  //     vacio y nadie la rellena. El eslabon roto esta antes, en el juego o
  //     en el descodificador.
  //   exitos sube pero envios congelado -> se entra a llamar al juego y no
  //     vuelve: el callback del guest se quedo dentro.
  //   el latido para del todo -> este mismo hilo esta bloqueado.
  uint64_t lat_esperas = 0;
  uint64_t lat_exitos = 0;
  uint64_t lat_plazos = 0;
  uint64_t lat_fallos = 0;
  uint64_t lat_envios = 0;
  auto lat_ultimo = std::chrono::steady_clock::now();
  auto latido = [&]() {
    const auto ahora = std::chrono::steady_clock::now();
    if (ahora - lat_ultimo < std::chrono::seconds(1)) {
      return;
    }
    lat_ultimo = ahora;
    REXAPU_DEBUG("AudioWorker latido: esperas={} exitos={} plazos={} fallos={} envios={}",
                 lat_esperas, lat_exitos, lat_plazos, lat_fallos, lat_envios);
  };

  while (worker_running_) {
"""

ESPERA_ANCLA = """    if (result.first == rex::thread::WaitResult::kFailed) {
      REXAPU_WARN("AudioWorker: WaitAny failed");
      continue;
    }

    if (result.first == rex::thread::WaitResult::kTimeout) {
      if (diag_pump_count < 5) {
        REXAPU_NOISY_DEBUG("AudioWorker: WaitAny timed out (no semaphore signals)");
      }
    }
"""

ESPERA_NUEVO = """    // PARCHE LOCAL - instrumentacion de audio v4
    lat_esperas++;
    if (result.first == rex::thread::WaitResult::kFailed) {
      lat_fallos++;
      latido();
      REXAPU_WARN("AudioWorker: WaitAny failed");
      continue;
    }

    if (result.first == rex::thread::WaitResult::kTimeout) {
      lat_plazos++;
      if (diag_pump_count < 5) {
        REXAPU_NOISY_DEBUG("AudioWorker: WaitAny timed out (no semaphore signals)");
      }
    } else if (result.first == rex::thread::WaitResult::kSuccess) {
      lat_exitos++;
    }
    latido();
"""

ENVIO_ANCLA = """        function_dispatcher_->Execute(worker_thread_->thread_state(), client_callback, args,
                                      rex::countof(args));
"""

ENVIO_NUEVO = """        function_dispatcher_->Execute(worker_thread_->thread_state(), client_callback, args,
                                      rex::countof(args));
        // PARCHE LOCAL - se cuenta DESPUES de Execute a proposito: si el
        // callback del juego se queda dentro y no vuelve, este contador se
        // congela mientras "exitos" sigue subiendo, y eso lo dice todo.
        lat_envios++;
"""

SISTEMA_INC_ANCLA = """#include <rex/assert.h>
#include <rex/audio/audio_driver.h>
"""

SISTEMA_INC_NUEVO = """// PARCHE LOCAL - instrumentacion de audio v4. El fichero ya usaba
// std::chrono, pero le llegaba de rebote por otras cabeceras. El latido
// depende de el, asi que se pide explicitamente en vez de confiar en eso.
#include <chrono>

#include <rex/assert.h>
#include <rex/audio/audio_driver.h>
"""

SISTEMA_ANCLAS = [
    ("include de chrono", SISTEMA_INC_ANCLA, SISTEMA_INC_NUEVO),
    ("latido del worker", LATIDO_ANCLA, LATIDO_NUEVO),
    ("clasificacion de la espera", ESPERA_ANCLA, ESPERA_NUEVO),
    ("vuelta del callback del juego", ENVIO_ANCLA, ENVIO_NUEVO),
]

# ---------------------------------------------------------------------------
#  3) src/audio/sdl/sdl_audio_driver.cpp  -  la salida a la tarjeta
# ---------------------------------------------------------------------------

SILENCIO_ANCLA = """    static uint32_t sdl_callback_count = 0;
    std::unique_lock<std::mutex> guard(driver->frames_mutex_);
    if (driver->frames_queued_.empty()) {
      if (sdl_callback_count < 10) {
        REXAPU_DEBUG("SDLCallback: no frames queued (silence)");
        sdl_callback_count++;
      }
"""

SILENCIO_NUEVO = """    static uint32_t sdl_callback_count = 0;
    std::unique_lock<std::mutex> guard(driver->frames_mutex_);
    if (driver->frames_queued_.empty()) {
      // PARCHE LOCAL - instrumentacion de audio v4
      //
      // El contador original se topa a diez lineas y luego se calla para
      // siempre, asi que el silencio del cuelgue no dejaba rastro: solo se
      // veian los diez primeros huecos del arranque, que son normales.
      //
      // Aqui va limitado por TIEMPO, una linea por segundo, y no se calla
      // nunca. Lleva la cuenta acumulada para distinguir un hueco suelto de
      // un silencio continuo.
      static std::atomic<uint64_t> sdl_silencios{0};
      static std::atomic<int64_t> sdl_ultimo_aviso{0};
      const uint64_t total_silencios = ++sdl_silencios;
      const int64_t ahora_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count();
      int64_t anterior = sdl_ultimo_aviso.load(std::memory_order_relaxed);
      if (ahora_ms - anterior >= 1000 &&
          sdl_ultimo_aviso.compare_exchange_strong(anterior, ahora_ms)) {
        REXAPU_DEBUG("SDLCallback: sin frames en cola (silencio), acumulado={}", total_silencios);
      }
      (void)sdl_callback_count;
"""

SDL_INC_ANCLA = """#include <algorithm>
#include <array>
#include <cstring>
"""

SDL_INC_NUEVO = """#include <algorithm>
#include <array>
#include <atomic>   // PARCHE LOCAL - instrumentacion de audio v4
#include <chrono>   // PARCHE LOCAL - instrumentacion de audio v4
#include <cstring>
"""

ENTREGA_ANCLA = """  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    frames_queued_.push(output_frame);
    PROFILE_BUFFER_QUEUE_DEPTH(static_cast<int64_t>(frames_queued_.size()));
  }
}
"""

ENTREGA_NUEVO = """  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    frames_queued_.push(output_frame);
    PROFILE_BUFFER_QUEUE_DEPTH(static_cast<int64_t>(frames_queued_.size()));

    // PARCHE LOCAL - instrumentacion de audio v4
    //
    // Esta es LA pregunta: despues del cuelgue, el juego sigue entregando
    // audio o no? El contador que traia el SDK aqui tambien se topaba a diez
    // -y los diez se gastan en el arranque-, asi que no se veia.
    //
    // Va dentro del cerrojo a proposito. La linea original del SDK lee
    // frames_queued_.size() FUERA de el, unas lineas mas arriba, y ahi el
    // valor puede estar cambiando bajo los pies. Aqui no.
    static std::atomic<uint64_t> sdl_entregas{0};
    static std::atomic<int64_t> sdl_ultima_entrega{0};
    const uint64_t total_entregas = ++sdl_entregas;
    const int64_t ahora_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
    int64_t anterior = sdl_ultima_entrega.load(std::memory_order_relaxed);
    if (ahora_ms - anterior >= 1000 &&
        sdl_ultima_entrega.compare_exchange_strong(anterior, ahora_ms)) {
      REXAPU_DEBUG("SDLAudioDriver: el juego entrego audio, acumulado={} en cola={}",
                   total_entregas, frames_queued_.size());
    }
  }
}
"""

SDL_ANCLAS = [
    ("includes de la salida", SDL_INC_ANCLA, SDL_INC_NUEVO),
    ("entrega de audio del juego", ENTREGA_ANCLA, ENTREGA_NUEVO),
    ("silencio de la salida", SILENCIO_ANCLA, SILENCIO_NUEVO),
]

# ---------------------------------------------------------------------------

def localizar_sdk():
    raiz = pathlib.Path(__file__).resolve().parent.parent
    # NFSMW_SDK apunta a otro arbol del SDK (el de Android, por ejemplo). Si
    # esta puesta se usa SOLO esa ruta: caer en silencio en el SDK de Windows
    # parchearia el arbol equivocado.
    otro = os.environ.get("NFSMW_SDK")
    candidatos = [pathlib.Path(otro)] if otro else [raiz.parent / "rexglue-sdk", raiz / "sdk"]
    for cand in candidatos:
        if (cand / "src" / "audio" / "xma_context.cpp").exists():
            return cand
    sys.exit("[ERROR] No encuentro src/audio/xma_context.cpp del SDK.\n"
             "        Se busca en ..\\rexglue-sdk y en .\\sdk")


def original_de(f):
    return f.with_suffix(f.suffix + ".original")


def restaurar_si_hay_version_vieja(f):
    """Deja el fichero como estaba en el SDK si lleva un parche anterior.

    Sin esto, los anclajes -escritos contra el codigo limpio- no encajarian
    sobre un fichero ya parcheado, y el script abortaria diciendo que el SDK
    ha cambiado, que seria una pista falsa.
    """
    if not f.exists():
        return
    txt = f.read_text(encoding="utf-8")
    if not any(m in txt for m in MARCAS_VIEJAS):
        return
    orig = original_de(f)
    if not orig.exists():
        sys.exit(f"[ERROR] {f.name} tiene un parche anterior pero no hay\n"
                 f"        {orig.name} para deshacerlo. Restauralo desde el\n"
                 f"        repositorio del SDK y vuelve a intentarlo.")
    shutil.copy2(orig, f)
    print(f"[ok] {f.name}: quitada la version anterior del parche")


def aplicar(f, anclas):
    txt = f.read_text(encoding="utf-8")

    if MARCA in txt:
        print(f"[ok] {f.name}: ya estaba al dia, no lo toco")
        return

    for nombre, ancla, _ in anclas:
        n = txt.count(ancla)
        if n != 1:
            sys.exit(f"[ERROR] En {f.name}, el anclaje '{nombre}' aparece {n} veces,\n"
                     f"        esperaba 1. El SDK habra cambiado. No he tocado nada.")

    orig = original_de(f)
    if not orig.exists():
        shutil.copy2(f, orig)
        print(f"[ok] Copia de seguridad: {orig.name}")

    for _, ancla, nuevo in anclas:
        txt = txt.replace(ancla, nuevo)
    f.write_text(txt, encoding="utf-8")
    print(f"[ok] Parcheado {f.name}")


def main():
    p = argparse.ArgumentParser(add_help=True)
    p.add_argument("--estado", action="store_true")
    p.add_argument("--revertir", action="store_true")
    args = p.parse_args()

    sdk = localizar_sdk()
    audio = sdk / "src" / "audio"
    trabajo = [
        (audio / "xma_context.cpp", XMA_ANCLAS),
        (audio / "audio_system.cpp", SISTEMA_ANCLAS),
        (audio / "sdl" / "sdl_audio_driver.cpp", SDL_ANCLAS),
    ]
    # La v1 tocaba xma_decoder.cpp -le ponia un plazo de 4 ms al Wait del
    # worker-. Se demostro inutil: cada contexto se apaga solo tras un pase,
    # asi que barrer mas a menudo encuentra los 320 apagados. Y en un equipo
    # de dos nucleos era CPU gastada de balde. Se deshace si sigue puesta.
    f_dec = audio / "xma_decoder.cpp"

    if args.estado:
        for f, _ in trabajo:
            if not f.exists():
                print(f"  {f.name:24s} NO EXISTE")
                continue
            t = f.read_text(encoding="utf-8")
            if MARCA in t:
                estado = "v3 aplicado"
            elif any(m in t for m in MARCAS_VIEJAS):
                estado = "version ANTERIOR puesta (se cambiara al aplicar)"
            else:
                estado = "sin aplicar"
            print(f"  {f.name:24s} {estado}")
        if f_dec.exists() and any(m in f_dec.read_text(encoding="utf-8") for m in MARCAS_VIEJAS):
            print(f"  {f_dec.name:24s} v1 todavia puesta (se quitara al aplicar)")
        return 0

    if args.revertir:
        for f in [t[0] for t in trabajo] + [f_dec]:
            orig = original_de(f)
            if orig.exists():
                shutil.copy2(orig, f)
                print(f"[ok] Restaurado {f.name} desde {orig.name}")
        print()
        print("  HAY QUE RECOMPILAR EL SDK para que sirva de algo.")
        return 0

    restaurar_si_hay_version_vieja(f_dec)
    for f, anclas in trabajo:
        if not f.exists():
            sys.exit(f"[ERROR] No encuentro {f}")
        restaurar_si_hay_version_vieja(f)
        aplicar(f, anclas)

    print()
    print("  Se veran las tres piezas de la cadena de audio:")
    print("    - los kicks del juego y por que Work() no descodifica")
    print("    - un latido por segundo del hilo de audio, que NO se calla")
    print("    - si el juego sigue entregando audio, y el silencio de la salida")
    print()
    print("  Hace falta lanzar con --log_noisy=true (lo hace LOG_DETALLADO.bat).")
    print()
    print("  HAY QUE RECOMPILAR EL SDK para que sirva de algo:")
    print("    cmake --build out/build/win-amd64 --config Release --target install")
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
