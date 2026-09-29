#!/usr/bin/env python3
"""
Diagnostico: quien le quita el cerrojo global al procesador de comandos.

    python tools/diagnostico/parche_cerrojo.py            aplicar
    python tools/diagnostico/parche_cerrojo.py --estado
    python tools/diagnostico/parche_cerrojo.py --revertir

Toca un fichero del SDK:  src/graphics/shared_memory.cpp
Es para medir, no para dejarlo puesto.


POR QUE
=======

En el movil el menu principal va a 15 fps y parche_tiempos.py dice que el CP
no espera a la GPU. Mirando con simpleperf DONDE se duerme (evento
sched:sched_switch con pila), un tercio de las veces es aqui:

    VulkanCommandProcessor::IssueDraw
      SharedMemory::RequestRanges
        global_critical_region_.Acquire()      <- recursive_mutex::lock, futex

Ese cerrojo es UNO para todo el sistema: kernel, hilos, memoria, audio. Y
nadie lo retiene trabajando en CPU (simpleperf no ve a nadie mas ocupado), asi
que alguien lo tiene cogido mientras duerme o mientras el planificador lo
tiene parado.


QUE HACE
========

Cada vez que shared_memory.cpp coge el cerrojo global, primero lo intenta sin
esperar. Si esta ocupado, lee el tid del dueno -bionic lo guarda en el propio
pthread_mutex_t, a 4 bytes del principio- y mide cuanto se espera. Cada 5 s:

    cerrojo global: 412 esperas, 38.2 ms/s | Main XThread (F 30.1 ms (380), ...

Leer el dueno asi es un apano que depende de bionic. Por eso esto vive en
tools/diagnostico.
"""

import argparse
import os
import pathlib
import sys


FICHERO = "src/graphics/shared_memory.cpp"

LLAMADA_ORIGINAL = "global_critical_region_.Acquire();"
LLAMADA_NUEVA = "NfsmwCogerCerrojo(global_critical_region_);"

ANCLA_AYUDANTE = '''namespace rex::graphics {
'''

AYUDANTE = '''namespace rex::graphics {

// DIAGNOSTICO - cerrojo: ver tools/diagnostico/parche_cerrojo.py.
namespace {
struct NfsmwCerrojo {
  std::mutex m;
  std::map<int, std::pair<uint64_t, uint32_t>> por_dueno;  // tid -> (ns, esperas)
  uint64_t total_ns = 0;
  uint32_t esperas = 0;
  std::chrono::steady_clock::time_point desde = std::chrono::steady_clock::now();
};

NfsmwCerrojo& NfsmwEstadoCerrojo() {
  static NfsmwCerrojo c;
  return c;
}

std::string NfsmwNombreHilo(int tid) {
  char ruta[64];
  std::snprintf(ruta, sizeof(ruta), "/proc/self/task/%d/comm", tid);
  FILE* f = std::fopen(ruta, "r");
  if (!f) {
    return "?";
  }
  char nombre[64] = {};
  if (!std::fgets(nombre, sizeof(nombre), f)) {
    nombre[0] = 0;
  }
  std::fclose(f);
  std::string s(nombre);
  while (!s.empty() && s.back() == '\\n') {
    s.pop_back();
  }
  return s;
}

std::unique_lock<std::recursive_mutex> NfsmwCogerCerrojo(
    rex::thread::global_critical_region& region) {
  auto cerrojo = region.AcquireDeferred();
  if (cerrojo.try_lock()) {
    return cerrojo;
  }
  // bionic: pthread_mutex_internal_t { uint16 state; uint16 pad; int owner_tid; ... }
  const int dueno = reinterpret_cast<const std::atomic<int>*>(
                        reinterpret_cast<const char*>(cerrojo.mutex()->native_handle()) + 4)
                        ->load(std::memory_order_relaxed);
  const auto t0 = std::chrono::steady_clock::now();
  cerrojo.lock();
  const auto t1 = std::chrono::steady_clock::now();
  const uint64_t ns =
      uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

  NfsmwCerrojo& c = NfsmwEstadoCerrojo();
  std::lock_guard<std::mutex> guarda(c.m);
  auto& e = c.por_dueno[dueno];
  e.first += ns;
  ++e.second;
  c.total_ns += ns;
  ++c.esperas;
  const double segundos = std::chrono::duration<double>(t1 - c.desde).count();
  if (segundos >= 5.0) {
    std::vector<std::pair<int, std::pair<uint64_t, uint32_t>>> lista(c.por_dueno.begin(),
                                                                     c.por_dueno.end());
    std::sort(lista.begin(), lista.end(),
              [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
    std::string duenos;
    for (size_t i = 0; i < lista.size() && i < 6; ++i) {
      duenos += fmt::format(" | {}({}) {:.1f} ms/s ({})", NfsmwNombreHilo(lista[i].first),
                            lista[i].first, double(lista[i].second.first) / 1e6 / segundos,
                            lista[i].second.second);
    }
    REXGPU_INFO("cerrojo global: {} esperas en {:.1f} s, {:.1f} ms/s esperando{}", c.esperas,
                segundos, double(c.total_ns) / 1e6 / segundos, duenos);
    c.por_dueno.clear();
    c.total_ns = 0;
    c.esperas = 0;
    c.desde = t1;
  }
  return cerrojo;
}
}  // namespace
'''

ANCLA_INCLUDES = '''#include <utility>
'''

INCLUDES = '''#include <utility>
// DIAGNOSTICO - cerrojo
#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <fmt/format.h>
#include <rex/logging.h>
'''


def localizar_sdk():
    raiz = pathlib.Path(__file__).resolve().parent.parent.parent
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
    puesto = AYUDANTE in txt

    if args.estado:
        print(f"  cerrojo (diagnostico)      {'aplicado' if puesto else 'sin aplicar'}")
        return 0

    if args.revertir:
        if not puesto:
            print("[ok] cerrojo: no habia nada puesto")
            return 0
        txt = txt.replace(AYUDANTE, ANCLA_AYUDANTE).replace(INCLUDES, ANCLA_INCLUDES)
        txt = txt.replace(LLAMADA_NUEVA, LLAMADA_ORIGINAL)
        escribir(f, txt, eol)
        print("[ok] Quitado")
        return 0

    if puesto:
        print("[ok] cerrojo: ya estaba")
        return 0
    for ancla in (ANCLA_AYUDANTE, ANCLA_INCLUDES):
        if txt.count(ancla) != 1:
            sys.exit(f"[ERROR] Un anclaje no aparece exactamente una vez en {FICHERO}.\n"
                     "        No he tocado nada.")
    n = txt.count(LLAMADA_ORIGINAL)
    if n == 0:
        sys.exit(f"[ERROR] No hay ningun {LLAMADA_ORIGINAL} en {FICHERO}. No he tocado nada.")
    txt = txt.replace(ANCLA_INCLUDES, INCLUDES).replace(ANCLA_AYUDANTE, AYUDANTE)
    txt = txt.replace(LLAMADA_ORIGINAL, LLAMADA_NUEVA)
    escribir(f, txt, eol)
    print(f"[ok] Aplicado: {n} tomas del cerrojo global medidas en {FICHERO}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
