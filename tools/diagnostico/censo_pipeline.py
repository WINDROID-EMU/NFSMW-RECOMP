#!/usr/bin/env python3
"""
Censo de lo que el juego le pide a la GPU: dibujos, objetivos y resolves.

    python tools/diagnostico/censo_pipeline.py            aplicar
    python tools/diagnostico/censo_pipeline.py --estado
    python tools/diagnostico/censo_pipeline.py --revertir

Toca tres ficheros del SDK:
    src/graphics/vulkan/command_processor.cpp    (dibujos y resumen)
    src/graphics/vulkan/render_target_cache.cpp  (resolves)
    src/graphics/command_processor.cpp           (buffers indirectos)
No guarda .original: aplica y deshace por sustitucion de texto exacta.


PARA QUE
========

Es la fase 1 de docs/pipeline-nativo.md. Antes de escribir un pipeline propio
hay que saber QUE pide el juego de verdad: cuantos dibujos, cuantas parejas de
shaders distintas, cuantos objetivos de render, y -lo que mas importa para la
fase 2- a que direcciones de memoria del invitado acaban yendo esos objetivos.
Sin eso, cualquier diseno es adivinar.

Se enciende con dos cvars:

  --nfsmw_censo=N          cuantos fotogramas censar (0 = nada)
  --nfsmw_censo_minimo=N   solo fotogramas con al menos N dibujos

El segundo es el que elige la escena SIN navegar por los menus, que con adb no
es fiable: se lanza el juego armado y el censo se queda callado hasta que el
juego llega a un fotograma bastante cargado. Por dibujos por fotograma: video
3-5, pantalla de titulo ~18, menu principal ~800, carrera >2500. Asi que

  --nfsmw_censo=1 --nfsmw_censo_minimo=400     el primer fotograma de menu
  --nfsmw_censo=1 --nfsmw_censo_minimo=1500    el primero de carrera, cuando
                                               entre el usuario

Escribe en el log de la app (NO en logcat, que en el movil de pruebas esta
capado):

  CENSO pase <n>: <dibujos> dibujos, pitch=... msaa=...x superficie=...
  CENSO dibujo <n>: prim=... vs=... ps=... superficie=... mezcla=...
  CENSO fotograma: <dibujos> dibujos, <n> parejas de shaders, <n> objetivos...
  CENSO   objetivo superficie=... color=... prof=...: <n> dibujos
  CENSO   resolve destino=... <w>x<h> color|prof fmt=...: <n> veces
  CENSO   shaders vs=... ps=...: <n> dibujos

Lo primero que hay que leer son los PASES: un tramo por cada vez que el juego
cambia de objetivo, con cuantos dibujos lleva cada uno. Eso es la forma del
fotograma (sombras, reflejos, escena, posprocesado) y es lo que dice como
montar el pipeline. Las lineas por dibujo son solo una muestra de los primeros
kCensoDetalleMaximo, porque se las come entero el primer pase.

Todo se guarda en memoria y solo se escribe si el fotograma cuenta: en carrera
hay miles de dibujos por fotograma, y escribirlos mientras se espera a la
escena buena ahogaria el log. Lo que disena el pipeline es el reparto de abajo:
si las parejas de shaders y las configuraciones de objetivo son pocas
-y en este juego deberian serlo-, el estado se puede cachear entero en vez de
montarlo dibujo a dibujo. Y la lista de resolves es el mapa "direccion de
invitado -> imagen" que la fase 2 necesita para tirar la EDRAM.

Cuesta lo suyo, asi que es para mirar, no para jugar.
"""

import argparse
import json
import os
import pathlib
import sys


CP = "src/graphics/vulkan/command_processor.cpp"
RTC = "src/graphics/vulkan/render_target_cache.cpp"
CPB = "src/graphics/command_processor.cpp"

# ---------------------------------------------------------------- dibujos ---

CP_INCLUDE_ANCLA = '''#include <string_view>
'''

CP_INCLUDE_NUEVO = '''#include <algorithm>  // CENSO (tools/diagnostico/censo_pipeline.py)
#include <map>        // CENSO
#include <set>        // CENSO
#include <string_view>
#include <utility>  // CENSO
#include <vector>   // CENSO
'''

CP_CVAR_ANCLA = '''namespace rex::graphics::vulkan {
'''

CP_CVAR_NUEVO = '''// CENSO (tools/diagnostico/censo_pipeline.py): fase 1 del pipeline nativo.
REXCVAR_DEFINE_INT32(nfsmw_censo, 0, "GPU",
                     "Censar N fotogramas de lo que el juego le pide a la GPU");
REXCVAR_DEFINE_INT32(nfsmw_censo_minimo, 200, "GPU",
                     "Censar solo fotogramas con al menos N dibujos: asi se elige la escena sin "
                     "navegar por los menus (video 3-5, titulo ~18, menu ~800, carrera >2500)");

namespace {

// Dibujos sueltos de los que se guarda el detalle. Se apuntan en memoria y solo
// se escriben si el fotograma acaba contando, que es lo que evita ahogar el log
// mientras se espera a la escena buena.
constexpr uint32_t kCensoDetalleMaximo = 64;
// Cambios de objetivo que se guardan. ESTO es lo que describe el fotograma: un
// pase por cada vez que el juego cambia de superficie, con cuantos dibujos lleva
// cada uno. Los primeros N dibujos no sirven, porque se los come el primer pase.
constexpr uint32_t kCensoPasesMaximo = 256;

struct NfsmwCensoDibujo {
  uint64_t vs = 0;
  uint64_t ps = 0;
  uint32_t primitiva = 0;
  uint32_t indices = 0;
  uint32_t superficie = 0;
  uint32_t color = 0;
  uint32_t profundidad = 0;
  uint32_t control_prof = 0;
  uint32_t control_color = 0;
  uint32_t mezcla = 0;
  uint32_t texturas = 0;
};

// Cuantos dibujos de la secuencia se guardan para buscarle el periodo. El pase
// grande de una carrera ronda los 4.000.
constexpr uint32_t kCensoSecuenciaMaxima = 16384;

// Lo que coloca cada dibujo en la pantalla. Es lo unico que cambia de una
// franja a la siguiente.
struct NfsmwCensoVentana {
  uint32_t offset = 0;
  uint32_t recorte_tl = 0;
  uint32_t recorte_br = 0;
  uint32_t pantalla_tl = 0;
};

// De cuantas vueltas de un mismo buffer indirecto se guarda la ventana.
constexpr uint32_t kCensoVueltasGuardadas = 4;

// Un buffer indirecto y las vueltas que le da el juego en un fotograma.
struct NfsmwCensoLlamada {
  uint32_t veces = 0;
  NfsmwCensoVentana vueltas[kCensoVueltasGuardadas];
};

// Cuantas llamadas seguidas se guardan, en orden. Con esto se ve la ESTRUCTURA:
// si el juego hace tres vueltas limpias a la misma lista o las mezcla.
constexpr uint32_t kCensoOrdenMaximo = 64;

struct NfsmwCensoOrden {
  uint32_t puntero = 0;
  uint32_t largo = 0;
  uint32_t offset = 0;
  uint32_t recorte_tl = 0;
  uint32_t recorte_br = 0;
};

// Un tramo de dibujos seguidos contra el mismo objetivo.
struct NfsmwCensoPase {
  uint32_t superficie = 0;
  uint32_t color = 0;
  uint32_t profundidad = 0;
  uint32_t dibujos = 0;
  uint32_t primer_dibujo = 0;  // indice en la secuencia
  uint64_t vs = 0;             // los del primer dibujo del pase, para reconocerlo
  uint64_t ps = 0;
};

struct NfsmwCensoObjetivo {
  uint32_t dibujos = 0;
  uint32_t profundidad = 0;
};

struct NfsmwCensoResolucion {
  uint32_t veces = 0;
  uint32_t destino = 0;
  uint32_t ancho = 0;
  uint32_t alto = 0;
  uint32_t base_color = 0;
  uint32_t base_prof = 0;
  uint32_t formato = 0;
  bool profundidad = false;
};

// Lo que se cuenta de un fotograma para el reparto final.
struct NfsmwCenso {
  uint32_t dibujos = 0;
  uint32_t fotogramas_restantes = 0;
  std::map<uint64_t, NfsmwCensoObjetivo> objetivos;            // superficie:color
  std::map<std::pair<uint64_t, uint64_t>, uint32_t> shaders;   // vs:ps -> dibujos
  std::map<uint64_t, NfsmwCensoResolucion> resolves;
  std::set<uint64_t> mezclas;
  std::set<uint32_t> texturas;
  std::vector<NfsmwCensoDibujo> detalle;
  std::vector<NfsmwCensoPase> pases;
  // Cuantas veces se repite cada dibujo identico. Ojo: la huella es shaders +
  // primitiva + numero de indices, asi que dos dibujos de verdad distintos
  // pueden compartirla. Sirve para ver lo repetitivo que es el fotograma, NO
  // para demostrar el troceado.
  std::map<uint64_t, uint32_t> repeticiones;
  // Eso lo demuestra la secuencia: si el juego pinta la escena tres veces, una
  // por franja, la lista de dibujos del pase grande son tres bloques identicos.
  std::vector<uint64_t> secuencia;
  // Y en paralelo, la ventana de cada dibujo. Entre una franja y otra el juego
  // manda la MISMA geometria y solo cambia esto, asi que aqui se ve que habria
  // que forzar para pintarlo todo de una pasada.
  std::vector<NfsmwCensoVentana> ventanas;
  // Llamadas a buffers indirectos, por puntero y largo. Si una sale tres veces
  // en el fotograma, el juego esta reenviando la misma lista de dibujos.
  std::map<uint64_t, NfsmwCensoLlamada> indirectos;
  std::vector<NfsmwCensoOrden> orden_indirectos;
};
NfsmwCenso nfsmw_censo_estado;

void NfsmwCensoOlvidar(NfsmwCenso& censo) {
  censo.dibujos = 0;
  censo.objetivos.clear();
  censo.shaders.clear();
  censo.resolves.clear();
  censo.mezclas.clear();
  censo.texturas.clear();
  censo.detalle.clear();
  censo.pases.clear();
  censo.repeticiones.clear();
  censo.secuencia.clear();
  censo.ventanas.clear();
  censo.indirectos.clear();
  censo.orden_indirectos.clear();
}

// En cuantos bloques iguales se parte la lista de dibujos de un pase. Si sale
// 3, el juego esta pintando eso tres veces -una por franja del troceado- y
// sobran dos tercios de sus dibujos.
uint32_t NfsmwCensoBloques(const NfsmwCenso& censo, const NfsmwCensoPase& pase) {
  if (pase.primer_dibujo + pase.dibujos > censo.secuencia.size() || pase.dibujos < 2) {
    return 1;
  }
  const uint64_t* lista = censo.secuencia.data() + pase.primer_dibujo;
  uint32_t bloques = 1;
  for (uint32_t n : {2u, 3u, 4u, 6u}) {
    if (pase.dibujos % n) {
      continue;
    }
    const uint32_t paso = pase.dibujos / n;
    bool igual = true;
    for (uint32_t i = paso; i < pase.dibujos && igual; ++i) {
      igual = lista[i] == lista[i % paso];
    }
    if (igual) {
      bloques = n;  // se queda con el mayor que cuadre
    }
  }
  return bloques;
}

void NfsmwCensoVolcar(const NfsmwCenso& censo) {
  // Lo primero, la forma del fotograma: que objetivo se pinta, en que orden y
  // con cuantos dibujos. Es lo que dice como montar el pipeline.
  for (size_t i = 0; i < censo.pases.size(); ++i) {
    const NfsmwCensoPase& pase = censo.pases[i];
    const uint32_t pitch = pase.superficie & 0x3FFFu;
    const uint32_t muestras = 1u << ((pase.superficie >> 16) & 3u);
    const uint32_t bloques = NfsmwCensoBloques(censo, pase);
    REXGPU_INFO(
        "CENSO pase {}: {} dibujos en {} bloques iguales ({} distintos), pitch={} msaa={}x "
        "superficie={:08X} color={:08X} prof={:08X} primer vs={:016X} ps={:016X}",
        i + 1, pase.dibujos, bloques, pase.dibujos / bloques, pitch, muestras, pase.superficie,
        pase.color, pase.profundidad, pase.vs, pase.ps);
    // Si el pase se repite, que cambia de una franja a otra. La geometria es la
    // misma; lo unico que se mueve es la ventana. Eso es lo que habria que
    // forzar a pantalla completa para pintarlo todo de una pasada.
    if (bloques > 1 && pase.primer_dibujo + pase.dibujos <= censo.ventanas.size()) {
      const uint32_t paso = pase.dibujos / bloques;
      for (uint32_t b = 0; b < bloques; ++b) {
        const NfsmwCensoVentana& ventana = censo.ventanas[pase.primer_dibujo + b * paso];
        REXGPU_INFO(
            "CENSO   franja {}/{}: offset={:08X} recorte {:08X}-{:08X} pantalla={:08X}", b + 1,
            bloques, ventana.offset, ventana.recorte_tl, ventana.recorte_br, ventana.pantalla_tl);
      }
    }
  }
  for (size_t i = 0; i < censo.detalle.size(); ++i) {
    const NfsmwCensoDibujo& apunte = censo.detalle[i];
    REXGPU_INFO(
        "CENSO dibujo {}: prim={} indices={} vs={:016X} ps={:016X} superficie={:08X} "
        "color={:08X} prof={:08X} ctrl_prof={:08X} ctrl_color={:08X} mezcla={:08X} "
        "texturas={:08X}",
        i + 1, apunte.primitiva, apunte.indices, apunte.vs, apunte.ps, apunte.superficie,
        apunte.color, apunte.profundidad, apunte.control_prof, apunte.control_color, apunte.mezcla,
        apunte.texturas);
  }
  REXGPU_INFO(
      "CENSO fotograma: {} dibujos, {} parejas de shaders, {} objetivos, {} estados de "
      "mezcla/profundidad, {} juegos de texturas, {} resolves",
      censo.dibujos, censo.shaders.size(), censo.objetivos.size(), censo.mezclas.size(),
      censo.texturas.size(), censo.resolves.size());
  for (const auto& entrada : censo.objetivos) {
    REXGPU_INFO("CENSO   objetivo superficie={:08X} color={:08X} prof={:08X}: {} dibujos",
                uint32_t(entrada.first >> 32), uint32_t(entrada.first),
                entrada.second.profundidad, entrada.second.dibujos);
  }
  // El mapa "direccion de invitado -> imagen" que necesita la fase 2.
  for (const auto& entrada : censo.resolves) {
    const NfsmwCensoResolucion& resolucion = entrada.second;
    REXGPU_INFO(
        "CENSO   resolve destino={:08X} {}x{} {} fmt={:X} desde color base {} prof base {}: "
        "{} veces",
        resolucion.destino, resolucion.ancho, resolucion.alto,
        resolucion.profundidad ? "prof" : "color", resolucion.formato, resolucion.base_color,
        resolucion.base_prof, resolucion.veces);
  }
  // Cuanto del fotograma es repeticion. Si la escena se pinta tres veces por el
  // troceado, aqui salen la mayoria de los dibujos en grupos de tres, y el
  // "x3 y mas" es lo que se ahorraria un objetivo de render nativo.
  {
    uint32_t sueltos = 0, dobles = 0, triples = 0, mas = 0;
    for (const auto& entrada : censo.repeticiones) {
      const uint32_t veces = entrada.second;
      if (veces == 1) {
        ++sueltos;
      } else if (veces == 2) {
        dobles += veces;
      } else if (veces == 3) {
        triples += veces;
      } else {
        mas += veces;
      }
    }
    REXGPU_INFO(
        "CENSO repeticiones: {} dibujos distintos de {}; una vez {}, dos veces {}, "
        "tres veces {}, mas de tres {}",
        censo.repeticiones.size(), censo.dibujos, sueltos, dobles, triples, mas);
  }
  // De donde salen las repeticiones: si el mismo buffer indirecto se ejecuta
  // varias veces, saltarselo ahi cuesta un "if".
  for (const auto& entrada : censo.indirectos) {
    const NfsmwCensoLlamada& llamada = entrada.second;
    if (llamada.veces <= 1) {
      continue;
    }
    REXGPU_INFO("CENSO   buffer indirecto {:08X} de {:X} bytes: ejecutado {} veces",
                uint32_t(entrada.first >> 32), uint32_t(entrada.first), llamada.veces);
    const uint32_t guardadas = std::min(llamada.veces, kCensoVueltasGuardadas);
    for (uint32_t v = 0; v < guardadas; ++v) {
      const NfsmwCensoVentana& ventana = llamada.vueltas[v];
      REXGPU_INFO("CENSO     vuelta {}: offset={:08X} recorte {:08X}-{:08X} pantalla={:08X}",
                  v + 1, ventana.offset, ventana.recorte_tl, ventana.recorte_br,
                  ventana.pantalla_tl);
    }
  }
  for (size_t i = 0; i < censo.orden_indirectos.size(); ++i) {
    const NfsmwCensoOrden& apunte = censo.orden_indirectos[i];
    REXGPU_INFO("CENSO   orden {}: {:08X} de {:X} offset={:08X} recorte {:08X}-{:08X}", i + 1,
                apunte.puntero, apunte.largo, apunte.offset, apunte.recorte_tl, apunte.recorte_br);
  }
  // Lo que sobra en todo el fotograma por pintar lo mismo varias veces.
  {
    uint32_t sobran = 0, en_bloques = 0;
    for (const NfsmwCensoPase& pase : censo.pases) {
      const uint32_t bloques = NfsmwCensoBloques(censo, pase);
      if (bloques > 1) {
        sobran += pase.dibujos - pase.dibujos / bloques;
        en_bloques += pase.dibujos;
      }
    }
    REXGPU_INFO(
        "CENSO troceado: {} dibujos de {} estan en pases que se repiten; sobrarian {} "
        "({}% del fotograma)",
        en_bloques, censo.dibujos, sobran, censo.dibujos ? sobran * 100 / censo.dibujos : 0);
  }
  // Las parejas de shaders mas usadas: si unas pocas se comen los dibujos,
  // cachear su pipeline entero es lo que mas devuelve.
  std::vector<std::pair<uint32_t, std::pair<uint64_t, uint64_t>>> orden;
  orden.reserve(censo.shaders.size());
  for (const auto& entrada : censo.shaders) {
    orden.emplace_back(entrada.second, entrada.first);
  }
  std::sort(orden.begin(), orden.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
  const size_t tope = std::min<size_t>(orden.size(), 10);
  for (size_t i = 0; i < tope; ++i) {
    REXGPU_INFO("CENSO   shaders vs={:016X} ps={:016X}: {} dibujos", orden[i].second.first,
                orden[i].second.second, orden[i].first);
  }
}

}  // namespace

// La llama command_processor.cpp en cada buffer indirecto.
extern "C" void NfsmwCensoIndirecto(uint32_t puntero, uint32_t largo, uint32_t offset,
                                    uint32_t recorte_tl, uint32_t recorte_br,
                                    uint32_t pantalla_tl) {
  NfsmwCenso& censo = nfsmw_censo_estado;
  if (!censo.fotogramas_restantes) {
    return;
  }
  NfsmwCensoLlamada& indirecto = censo.indirectos[(uint64_t(puntero) << 32) | largo];
  if (indirecto.veces < kCensoVueltasGuardadas) {
    NfsmwCensoVentana& ventana = indirecto.vueltas[indirecto.veces];
    ventana.offset = offset;
    ventana.recorte_tl = recorte_tl;
    ventana.recorte_br = recorte_br;
    ventana.pantalla_tl = pantalla_tl;
  }
  ++indirecto.veces;
  if (censo.orden_indirectos.size() < kCensoOrdenMaximo) {
    NfsmwCensoOrden apunte;
    apunte.puntero = puntero;
    apunte.largo = largo;
    apunte.offset = offset;
    apunte.recorte_tl = recorte_tl;
    apunte.recorte_br = recorte_br;
    censo.orden_indirectos.push_back(apunte);
  }
}

// La llama render_target_cache.cpp en cada resolve del juego: es el mapa
// "direccion de invitado -> imagen" que la fase 2 necesita.
extern "C" void NfsmwCensoResolve(uint32_t destino, uint32_t ancho, uint32_t alto,
                                  uint32_t base_color, uint32_t base_prof, uint32_t formato,
                                  uint32_t es_profundidad) {
  NfsmwCenso& censo = nfsmw_censo_estado;
  if (!censo.fotogramas_restantes) {
    return;
  }
  const uint64_t clave = (uint64_t(destino) << 32) ^ (uint64_t(ancho) << 16) ^ uint64_t(alto) ^
                         (es_profundidad ? (uint64_t(1) << 63) : uint64_t(0));
  NfsmwCensoResolucion& resolucion = censo.resolves[clave];
  ++resolucion.veces;
  resolucion.destino = destino;
  resolucion.ancho = ancho;
  resolucion.alto = alto;
  resolucion.base_color = base_color;
  resolucion.base_prof = base_prof;
  resolucion.formato = formato;
  resolucion.profundidad = es_profundidad != 0;
}

namespace rex::graphics::vulkan {

'''

CP_DIBUJO_ANCLA = '''  // After all commands that may dispatch, copy or insert barriers, submit
'''

CP_DIBUJO_NUEVO = '''  // CENSO (tools/diagnostico/censo_pipeline.py): el estado real de este dibujo.
  if (nfsmw_censo_estado.fotogramas_restantes) {
    NfsmwCenso& censo = nfsmw_censo_estado;
    const uint64_t vs_hash = vertex_shader->ucode_data_hash();
    const uint64_t ps_hash = pixel_shader ? pixel_shader->ucode_data_hash() : 0;
    const uint32_t superficie = regs[XE_GPU_REG_RB_SURFACE_INFO];
    const uint32_t color = regs[XE_GPU_REG_RB_COLOR_INFO];
    const uint32_t profundidad = regs[XE_GPU_REG_RB_DEPTH_INFO];
    const uint32_t control_prof = regs[XE_GPU_REG_RB_DEPTHCONTROL];
    const uint32_t control_color = regs[XE_GPU_REG_RB_COLORCONTROL];
    const uint32_t mezcla = regs[XE_GPU_REG_RB_BLENDCONTROL0];
    ++censo.dibujos;
    ++censo.shaders[std::make_pair(vs_hash, ps_hash)];
    NfsmwCensoObjetivo& objetivo = censo.objetivos[(uint64_t(superficie) << 32) | color];
    ++objetivo.dibujos;
    objetivo.profundidad = profundidad;
    censo.mezclas.insert((uint64_t(mezcla) << 32) | control_prof);
    censo.texturas.insert(used_texture_mask);
    // Huella del dibujo: dos dibujos con los mismos shaders, la misma primitiva
    // y el mismo numero de indices son, a efectos de esto, el mismo.
    const uint64_t huella = vs_hash * 0x9E3779B97F4A7C15ull ^
                            (ps_hash + 0x165667B19E3779F9ull) ^ (uint64_t(index_count) << 32) ^
                            uint64_t(prim_type);
    ++censo.repeticiones[huella];
    if (censo.secuencia.size() < kCensoSecuenciaMaxima) {
      censo.secuencia.push_back(huella);
      NfsmwCensoVentana ventana;
      ventana.offset = regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET];
      ventana.recorte_tl = regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL];
      ventana.recorte_br = regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR];
      ventana.pantalla_tl = regs[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL];
      censo.ventanas.push_back(ventana);
    }
    // Un pase nuevo cada vez que cambia el objetivo. Pasado el tope se deja de
    // contar del todo, para no inflar el ultimo con los dibujos de los demas.
    const bool sigue_el_pase =
        !censo.pases.empty() && censo.pases.back().superficie == superficie &&
        censo.pases.back().color == color && censo.pases.back().profundidad == profundidad;
    if (!sigue_el_pase && censo.pases.size() < kCensoPasesMaximo) {
      NfsmwCensoPase pase;
      pase.superficie = superficie;
      pase.color = color;
      pase.profundidad = profundidad;
      pase.primer_dibujo = censo.dibujos - 1;
      pase.vs = vs_hash;
      pase.ps = ps_hash;
      censo.pases.push_back(pase);
      ++censo.pases.back().dibujos;
    } else if (sigue_el_pase) {
      ++censo.pases.back().dibujos;
    }
    if (censo.detalle.size() < kCensoDetalleMaximo) {
      NfsmwCensoDibujo apunte;
      apunte.vs = vs_hash;
      apunte.ps = ps_hash;
      apunte.primitiva = uint32_t(prim_type);
      apunte.indices = index_count;
      apunte.superficie = superficie;
      apunte.color = color;
      apunte.profundidad = profundidad;
      apunte.control_prof = control_prof;
      apunte.control_color = control_color;
      apunte.mezcla = mezcla;
      apunte.texturas = used_texture_mask;
      censo.detalle.push_back(apunte);
    }
  }

  // After all commands that may dispatch, copy or insert barriers, submit
'''

CP_SWAP_ANCLA = '''void VulkanCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                                       uint32_t frontbuffer_height) {
'''

CP_SWAP_NUEVO = '''void VulkanCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                                       uint32_t frontbuffer_height) {
  // CENSO (tools/diagnostico/censo_pipeline.py): un fotograma se acaba aqui.
  {
    NfsmwCenso& censo = nfsmw_censo_estado;
    if (!censo.fotogramas_restantes) {
      // El censo se arma cuando cambia el valor pedido, no al arrancar: asi
      // vale tanto desde la linea de comandos como cambiandolo en marcha.
      static int32_t pedidos = -1;
      const int32_t ahora = REXCVAR_GET(nfsmw_censo);
      if (ahora != pedidos) {
        pedidos = ahora;
        censo.fotogramas_restantes = uint32_t(std::max(0, ahora));
      }
    } else if (censo.dibujos >= uint32_t(std::max(0, REXCVAR_GET(nfsmw_censo_minimo)))) {
      // Solo cuentan los fotogramas con dibujos de sobra, que es como se elige
      // la escena sin navegar por los menus.
      NfsmwCensoVolcar(censo);
      --censo.fotogramas_restantes;
      NfsmwCensoOlvidar(censo);
    } else {
      // Fotograma flojo: ni se escribe ni se descuenta.
      NfsmwCensoOlvidar(censo);
    }
  }

'''

# --------------------------------------------------------------- resolves ---

RTC_DECL_ANCLA = '''#include <rex/ui/vulkan/util.h>
'''

RTC_DECL_NUEVO = '''#include <rex/ui/vulkan/util.h>

// CENSO (tools/diagnostico/censo_pipeline.py): definida en command_processor.cpp.
extern "C" void NfsmwCensoResolve(uint32_t destino, uint32_t ancho, uint32_t alto,
                                  uint32_t base_color, uint32_t base_prof, uint32_t formato,
                                  uint32_t es_profundidad);
'''

RTC_RESOLVE_ANCLA = '''  // Nothing to copy/clear.
  if (!resolve_info.coordinate_info.width_div_8 || !resolve_info.height_div_8) {
    return true;
  }
'''

RTC_RESOLVE_NUEVO = '''  // Nothing to copy/clear.
  if (!resolve_info.coordinate_info.width_div_8 || !resolve_info.height_div_8) {
    return true;
  }

  // CENSO (tools/diagnostico/censo_pipeline.py): que resuelve el juego y adonde.
  NfsmwCensoResolve(uint32_t(resolve_info.copy_dest_base),
                    uint32_t(resolve_info.coordinate_info.width_div_8) * 8,
                    resolve_info.height_div_8 * 8, uint32_t(resolve_info.color_original_base),
                    uint32_t(resolve_info.depth_original_base),
                    uint32_t(resolve_info.copy_dest_info.copy_dest_format),
                    resolve_info.IsCopyingDepth() ? 1u : 0u);
'''

# ------------------------------------------------------------- troceado ---

CPB_DECL_ANCLA = '''#include <rex/dbg.h>
'''

CPB_DECL_NUEVO = '''#include <rex/dbg.h>

// CENSO (tools/diagnostico/censo_pipeline.py): esta en vulkan/command_processor.cpp.
extern "C" void NfsmwCensoIndirecto(uint32_t puntero, uint32_t largo, uint32_t offset,
                                    uint32_t recorte_tl, uint32_t recorte_br,
                                    uint32_t pantalla_tl);
'''

CPB_IB_ANCLA = '''  list_length &= 0xFFFFF;
  ExecuteIndirectBuffer(GpuToCpu(list_ptr), list_length);
'''

CPB_IB_NUEVO = '''  list_length &= 0xFFFFF;
  // CENSO (tools/diagnostico/censo_pipeline.py): si el juego llama varias veces
  // al mismo buffer en un fotograma, ahi esta el troceado, y ese es el sitio
  // donde saltarselo saldria mas barato. La ventana de cada vuelta dice que
  // habria que forzar para que una sola pasada cubriera la pantalla entera.
  NfsmwCensoIndirecto(list_ptr, list_length,
                      register_file_->values[XE_GPU_REG_PA_SC_WINDOW_OFFSET],
                      register_file_->values[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL],
                      register_file_->values[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR],
                      register_file_->values[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL]);
  ExecuteIndirectBuffer(GpuToCpu(list_ptr), list_length);
'''

# Por fichero, en orden de aplicacion.
BLOQUES = {
    CP: [
        (CP_INCLUDE_ANCLA, CP_INCLUDE_NUEVO),
        (CP_CVAR_ANCLA, CP_CVAR_NUEVO),
        (CP_DIBUJO_ANCLA, CP_DIBUJO_NUEVO),
        (CP_SWAP_ANCLA, CP_SWAP_NUEVO),
    ],
    RTC: [
        (RTC_DECL_ANCLA, RTC_DECL_NUEVO),
        (RTC_RESOLVE_ANCLA, RTC_RESOLVE_NUEVO),
    ],
    CPB: [
        (CPB_DECL_ANCLA, CPB_DECL_NUEVO),
        (CPB_IB_ANCLA, CPB_IB_NUEVO),
    ],
}

TOTAL = sum(len(b) for b in BLOQUES.values())


def localizar_sdk():
    raiz = pathlib.Path(__file__).resolve().parent.parent.parent
    # NFSMW_SDK apunta a otro arbol del SDK (el de Android, por ejemplo). Si
    # esta puesta se usa SOLO esa ruta: caer en silencio en el SDK de Windows
    # parchearia el arbol equivocado.
    otro = os.environ.get("NFSMW_SDK")
    candidatos = [pathlib.Path(otro)] if otro else [raiz.parent / "rexglue-sdk", raiz / "sdk"]
    for cand in candidatos:
        if (cand / CP).exists():
            return cand
    sys.exit(f"[ERROR] No encuentro {CP} del SDK.\n"
             "        Se busca en NFSMW_SDK, o en ..\\rexglue-sdk y .\\sdk")


def leer(f):
    with open(f, encoding="utf-8", newline="") as h:
        txt = h.read()
    eol = "\r\n" if "\r\n" in txt else "\n"
    return txt.replace("\r\n", "\n"), eol


def escribir(f, txt, eol):
    with open(f, "w", encoding="utf-8", newline="") as h:
        h.write(txt.replace("\n", eol))


# Lo que se dejo puesto la ultima vez, con el texto EXACTO. Se revierte por
# aqui, no por los bloques de arriba: si se editan los bloques con el parche
# puesto -que pasa cada vez que se afina el censo-, el texto de arriba ya no es
# el que esta en el SDK, y revertir por el dejaba media copia dentro y decia
# que todo bien. La siguiente aplicacion metia otra copia y el SDK no compilaba.
RECIBO = ".censo_pipeline_aplicado.json"


def leer_recibo(sdk):
    f = sdk / RECIBO
    if not f.exists():
        return None
    with open(f, encoding="utf-8") as h:
        return {rel: [tuple(par) for par in pares] for rel, pares in json.load(h).items()}


def main():
    p = argparse.ArgumentParser(add_help=True)
    p.add_argument("--estado", action="store_true")
    p.add_argument("--revertir", action="store_true")
    args = p.parse_args()

    sdk = localizar_sdk()
    recibo = leer_recibo(sdk)
    # Para mirar y para quitar manda el recibo; para poner, los bloques de arriba.
    vigentes = recibo if recibo else BLOQUES
    total_vigente = sum(len(b) for b in vigentes.values())

    ficheros = {}
    puestos = 0
    for rel in BLOQUES:
        txt, eol = leer(sdk / rel)
        ficheros[rel] = [txt, eol]
    for rel, bloques in vigentes.items():
        puestos += sum(1 for _, nuevo in bloques if nuevo in ficheros[rel][0])

    if args.estado:
        estado = ("aplicado" if puestos == total_vigente else "sin aplicar" if puestos == 0
                  else f"A MEDIAS ({puestos}/{total_vigente})")
        if recibo and puestos:
            estado += " (segun recibo)"
        print(f"  censo_pipeline             {estado}")
        return 0

    if args.revertir:
        if puestos == 0:
            print("[ok] censo_pipeline: no habia nada puesto")
            (sdk / RECIBO).unlink(missing_ok=True)
            return 0
        if puestos != total_vigente:
            sys.exit(f"[ERROR] Solo encuentro {puestos} de {total_vigente} bloques puestos.\n"
                     "        No quito nada a medias: mira el SDK a mano.")
        for rel, bloques in vigentes.items():
            for ancla, nuevo in bloques:
                ficheros[rel][0] = ficheros[rel][0].replace(nuevo, ancla)
            escribir(sdk / rel, *ficheros[rel])
        (sdk / RECIBO).unlink(missing_ok=True)
        print(f"[ok] Quitados {puestos} bloques")
        return 0

    if puestos == total_vigente and recibo:
        print("[ok] censo_pipeline: ya estaba")
        return 0
    if puestos:
        sys.exit(f"[ERROR] censo_pipeline esta a medias ({puestos}/{total_vigente}). "
                 "Revierte primero con --revertir.")
    for rel, bloques in BLOQUES.items():
        for ancla, _ in bloques:
            n = ficheros[rel][0].count(ancla)
            if n != 1:
                sys.exit(f"[ERROR] Un anclaje aparece {n} veces en {rel}, esperaba 1:\n"
                         f"        {ancla.splitlines()[0].strip()}\n"
                         "        El SDK habra cambiado. No he tocado nada.")
    for rel, bloques in BLOQUES.items():
        for ancla, nuevo in bloques:
            ficheros[rel][0] = ficheros[rel][0].replace(ancla, nuevo, 1)
        escribir(sdk / rel, *ficheros[rel])
    with open(sdk / RECIBO, "w", encoding="utf-8") as h:
        json.dump({rel: [list(par) for par in bloques] for rel, bloques in BLOQUES.items()}, h)
    print("[ok] Aplicado: censo con los cvars nfsmw_censo y nfsmw_censo_minimo")
    return 0


if __name__ == "__main__":
    sys.exit(main())
