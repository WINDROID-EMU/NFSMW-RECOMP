#!/usr/bin/env python3
"""
En que se van los 7,4 us que cuesta CADA dibujo.

    python tools/diagnostico/parche_reparto_dibujo.py            aplicar
    python tools/diagnostico/parche_reparto_dibujo.py --estado
    python tools/diagnostico/parche_reparto_dibujo.py --revertir

Toca un fichero del SDK:  src/graphics/vulkan/command_processor.cpp
Va DESPUES de tools/parche_tiempos.py, que es de donde sale NfsmwCrono y la
linea "tiempos por fotograma".

No guarda .original: aplica y deshace por sustitucion de texto exacta.


PARA QUE
========

En carrera los dibujos se comen 19-28 ms de los ~33 ms del fotograma, y cada
dibujo cuesta ~7,4 us. Eso es carisimo para lo que hay: el censo
(tools/diagnostico/censo_pipeline.py) dice que un fotograma de 4.570 dibujos
usa solo 31 parejas de shaders y 9 estados de mezcla. O sea que se esta
montando desde cero, dibujo a dibujo, algo que se repite 140 veces.

Ya se probaron tres ideas sueltas -cachear registros, vertices y constantes- y
las TRES salieron neutras (ver docs/android.md; los parches se retiraron). Adivinar no funciona: hay
que ver el reparto.

Este parche cronometra las fases de IssueDraw por separado y las suma a la
linea de tiempos que ya existe:

  reparto del dibujo: analisis A + primitivas B + texturas C + objetivos D
                      + pipeline E + enlaces F + resto G

  - analisis    AnalyzeShaderUcode de los dos shaders
  - primitivas  primitive_processor_->Process (indices y vertices)
  - texturas    texture_cache_->RequestTextures
  - objetivos   render_target_cache_->Update (EDRAM, transferencias)
  - pipeline    pipeline_cache_->ConfigurePipeline (traduccion y estado)
  - constantes    UpdateSystemConstantValues
  - descriptores  UpdateBindings
  - resto       lo que queda dentro de IssueDraw, incluida la orden de dibujo

La fase que se lleve la parte del leon es la que hay que atacar, y dice cual de
las fases de docs/pipeline-nativo.md vale la pena primero.
"""

import argparse
import os
import pathlib
import sys


FICHERO = "src/graphics/vulkan/command_processor.cpp"

BLOQUES = [
    (
        '''static std::atomic<uint64_t> nfsmw_cp_dibujo_ns{0};
''',
        '''static std::atomic<uint64_t> nfsmw_cp_dibujo_ns{0};
// REPARTO (tools/diagnostico/parche_reparto_dibujo.py): las fases de IssueDraw.
static std::atomic<uint64_t> nfsmw_fase_analisis_ns{0};
static std::atomic<uint64_t> nfsmw_fase_primitivas_ns{0};
static std::atomic<uint64_t> nfsmw_fase_texturas_ns{0};
static std::atomic<uint64_t> nfsmw_fase_objetivos_ns{0};
static std::atomic<uint64_t> nfsmw_fase_pipeline_ns{0};
static std::atomic<uint64_t> nfsmw_fase_constantes_ns{0};
static std::atomic<uint64_t> nfsmw_fase_descriptores_ns{0};
static std::atomic<uint64_t> nfsmw_fase_dinamico_ns{0};
static std::atomic<uint64_t> nfsmw_fase_barreras_ns{0};
''',
    ),
    (
        '''  pipeline_cache_->AnalyzeShaderUcode(*vertex_shader);
''',
        '''  {  // REPARTO
    NfsmwCrono nfsmw_c(nfsmw_fase_analisis_ns);
    pipeline_cache_->AnalyzeShaderUcode(*vertex_shader);
  }
''',
    ),
    (
        '''        pipeline_cache_->AnalyzeShaderUcode(*pixel_shader);
''',
        '''        {  // REPARTO
          NfsmwCrono nfsmw_c(nfsmw_fase_analisis_ns);
          pipeline_cache_->AnalyzeShaderUcode(*pixel_shader);
        }
''',
    ),
    (
        '''    if (!primitive_processor_->Process(primitive_processing_result)) {
''',
        '''    bool nfsmw_prim_ok;
    {  // REPARTO
      NfsmwCrono nfsmw_c(nfsmw_fase_primitivas_ns);
      nfsmw_prim_ok = primitive_processor_->Process(primitive_processing_result);
    }
    if (!nfsmw_prim_ok) {
''',
    ),
    (
        '''  texture_cache_->RequestTextures(used_texture_mask);
''',
        '''  {  // REPARTO
    NfsmwCrono nfsmw_c(nfsmw_fase_texturas_ns);
    texture_cache_->RequestTextures(used_texture_mask);
  }
''',
    ),
    (
        '''  UpdateSystemConstantValues(primitive_polygonal, primitive_processing_result,
''',
        '''  {  // REPARTO: solo las constantes
    NfsmwCrono nfsmw_c(nfsmw_fase_constantes_ns);
    UpdateSystemConstantValues(primitive_polygonal, primitive_processing_result,
                               shader_32bit_index_dma, 0, viewport_info, used_texture_mask,
                               normalized_depth_control, normalized_color_mask);
  }
  const bool nfsmw_constantes_hechas = true;
  (void)nfsmw_constantes_hechas;
  if (false)
    UpdateSystemConstantValues(primitive_polygonal, primitive_processing_result,
''',
    ),
    (
        '''  if (!render_target_cache_->Update(is_rasterization_done, normalized_depth_control,
                                    normalized_color_mask, *vertex_shader)) {
''',
        '''  bool nfsmw_obj_ok;
  {  // REPARTO
    NfsmwCrono nfsmw_c(nfsmw_fase_objetivos_ns);
    nfsmw_obj_ok = render_target_cache_->Update(is_rasterization_done, normalized_depth_control,
                                                normalized_color_mask, *vertex_shader);
  }
  if (!nfsmw_obj_ok) {
''',
    ),
    (
        '''  if (!pipeline_cache_->ConfigurePipeline(vertex_shader_translation, pixel_shader_translation,
                                          primitive_processing_result, normalized_depth_control,
                                          normalized_color_mask,
                                          render_target_cache_->last_update_render_pass_key(),
                                          pipeline, pipeline_layout_provider, &pipeline_handle)) {
''',
        '''  bool nfsmw_pipe_ok;
  {  // REPARTO
    NfsmwCrono nfsmw_c(nfsmw_fase_pipeline_ns);
    nfsmw_pipe_ok = pipeline_cache_->ConfigurePipeline(
        vertex_shader_translation, pixel_shader_translation, primitive_processing_result,
        normalized_depth_control, normalized_color_mask,
        render_target_cache_->last_update_render_pass_key(), pipeline, pipeline_layout_provider,
        &pipeline_handle);
  }
  if (!nfsmw_pipe_ok) {
''',
    ),
    (
        '''  if (!UpdateBindings(vertex_shader, pixel_shader)) {
    return draw_fail("update_bindings");
  }
''',
        '''  bool nfsmw_bind_ok;
  {  // REPARTO: solo los descriptores
    NfsmwCrono nfsmw_c(nfsmw_fase_descriptores_ns);
    nfsmw_bind_ok = UpdateBindings(vertex_shader, pixel_shader);
  }
  if (!nfsmw_bind_ok) {
    return draw_fail("update_bindings");
  }
''',
    ),
    (
        '''  UpdateDynamicState(viewport_info, primitive_polygonal, normalized_depth_control);
''',
        '''  {  // REPARTO: estado dinamico (viewport, scissor, sesgo, mezcla, stencil)
    NfsmwCrono nfsmw_c(nfsmw_fase_dinamico_ns);
    UpdateDynamicState(viewport_info, primitive_polygonal, normalized_depth_control);
  }
''',
    ),
    (
        '''  SubmitBarriersAndEnterRenderTargetCacheRenderPass(
      render_target_cache_->last_update_render_pass(),
      render_target_cache_->last_update_framebuffer());
''',
        '''  {  // REPARTO: barreras y entrada al pase de render
    NfsmwCrono nfsmw_c(nfsmw_fase_barreras_ns);
    SubmitBarriersAndEnterRenderTargetCacheRenderPass(
        render_target_cache_->last_update_render_pass(),
        render_target_cache_->last_update_framebuffer());
  }
''',
    ),
    (
        '''      const double n_dibujos = por(nfsmw_cp_dibujos);
''',
        '''      const double n_dibujos = por(nfsmw_cp_dibujos);
      // REPARTO (tools/diagnostico/parche_reparto_dibujo.py)
      const double f_analisis = ms(nfsmw_fase_analisis_ns);
      const double f_primitivas = ms(nfsmw_fase_primitivas_ns);
      const double f_texturas = ms(nfsmw_fase_texturas_ns);
      const double f_objetivos = ms(nfsmw_fase_objetivos_ns);
      const double f_pipeline = ms(nfsmw_fase_pipeline_ns);
      const double f_constantes = ms(nfsmw_fase_constantes_ns);
      const double f_descriptores = ms(nfsmw_fase_descriptores_ns);
      const double f_dinamico = ms(nfsmw_fase_dinamico_ns);
      const double f_barreras = ms(nfsmw_fase_barreras_ns);
      REXGPU_INFO(
          "reparto del dibujo: {:.1f} ms = analisis {:.1f} + primitivas {:.1f} + texturas "
          "{:.1f} + objetivos {:.1f} + pipeline {:.1f} + constantes {:.1f} + descriptores "
          "{:.1f} + dinamico {:.1f} + barreras {:.1f} + resto {:.1f}",
          dibujos, f_analisis, f_primitivas, f_texturas, f_objetivos, f_pipeline, f_constantes,
          f_descriptores, f_dinamico, f_barreras,
          dibujos - f_analisis - f_primitivas - f_texturas - f_objetivos - f_pipeline -
              f_constantes - f_descriptores - f_dinamico - f_barreras);
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
        print(f"  reparto_dibujo             {estado}")
        return 0

    if args.revertir:
        if puestos == 0:
            print("[ok] reparto_dibujo: no habia nada puesto")
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
        print("[ok] reparto_dibujo: ya estaba")
        return 0
    if puestos:
        sys.exit(f"[ERROR] reparto_dibujo esta a medias ({puestos}/{len(BLOQUES)}). "
                 "Revierte primero con --revertir.")
    for ancla, _ in BLOQUES:
        n = txt.count(ancla)
        if n != 1:
            sys.exit(f"[ERROR] Un anclaje aparece {n} veces en {FICHERO}, esperaba 1:\n"
                     f"        {ancla.splitlines()[0].strip()}\n"
                     "        Aplica antes parche_tiempos.py. No he tocado nada.")
    for ancla, nuevo in BLOQUES:
        txt = txt.replace(ancla, nuevo, 1)
    escribir(f, txt, eol)
    print("[ok] Aplicado: linea 'reparto del dibujo' junto a la de tiempos")
    return 0


if __name__ == "__main__":
    sys.exit(main())
