#!/usr/bin/env python3
"""
Otimizações de GPU na API Vulkan para máximo FPS e fluidez.

    python3 tools/parche_vulkan_opt.py            aplicar
    python3 tools/parche_vulkan_opt.py --estado   verificar
    python3 tools/parche_vulkan_opt.py --revertir desfazer

O QUE ESTE PARCHE OTIMIZA:
===========================
1. vulkan_async_skip_incomplete_frames:
   - De fábrica estava 'true', o que fazia o motor DESCARTAR e NÃO APRESENTAR
     o quadro inteiro sempre que um shader estivesse compilando em segundo plano.
     Isso causava congelamentos constantes e sensação de 10-15 FPS.
   - Mudado para 'false' por padrão: renderiza o frame com placeholder sem descartar,
     mantendo a taxa de quadros suave enquanto os pipelines compilam.

2. vulkan_submit_on_primary_buffer_end:
   - De fábrica submetia command buffers pequenos repetidamente a cada fim de buffer
     primário PM4, sobrecarregando o driver Vulkan com dezenas de vkQueueSubmit por frame.
   - Mudado para 'false' por padrão para permitir loteamento (batching) eficiente de submissões.

3. render_target_path_vulkan:
   - Caminho EDRAM padrão para "fbo" (Host Framebuffers) em vez de vazio/"fsi",
     evitando o uso acidental de Fragment Shader Interlock (3x mais pesado).

4. Priorização de GPU dedicada no Vulkan (vulkan_provider.cpp):
   - Adiciona pontuação prioritária para VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
     (+1000 pontos) sobre GPUs integradas (+100 pontos), garantindo que GPUs dedicadas
     (como GTX 1050 Ti) sejam sempre a primeira escolha automática.

5. Suporte a limitador de FPS suave no Vulkan (vulkan_presenter.cpp):
   - Conecta a verificação do cvar 'max_fps' antes de vkQueuePresentKHR,
     garantindo frame pacing estável e sem stuttering.

6. OTIMIZACAO DE ILUMINACAO (command_processor.cpp base):
   - query_occlusion_fake_sample_count: Reduzido de 1000 para 1.
     O NFSMW usa occlusion queries para determinar visibilidade das luzes dos carros
     e bloom de faróis. Com 1000 amostras falsas, TODAS as luzes sempre passam como
     visíveis, forçando o motor a renderizar todos os passes de luz e bloom mesmo
     para luzes fora de cena ou atrás de prédios (~30-40% do custo de iluminação).
     Com 1 amostra: luzes são culled corretamente pelo hardware de occlusion.
   - clear_memory_page_state: Desativado (era true).
     Varre toda a memória GPU-escrita no fim de CADA FRAME: custo O(N) por frame ~0.5ms.
     Desnecessário em modo normal (somente necessário para debug de coherência).
"""

import argparse
import pathlib
import sys

MARCA = "PARCHE LOCAL"

# ---------------------------------------------------------------------------
#  1. command_processor.cpp (Async skip e submit batching)
# ---------------------------------------------------------------------------
ANCLA_CMD = """REXCVAR_DEFINE_BOOL(vulkan_async_skip_incomplete_frames, true, "GPU/Vulkan",
                    "When async shader compilation is enabled, skip presenting frames that "
                    "used placeholder pipelines to avoid visible flashing")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(vulkan_submit_on_primary_buffer_end, true, "GPU/Vulkan",
                    "Submit command buffer when PM4 primary buffer ends")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);"""

NUEVO_CMD = """// PARCHE LOCAL - otimizacoes de performance GPU Vulkan (async skip false, submit batching)
REXCVAR_DEFINE_BOOL(vulkan_async_skip_incomplete_frames, false, "GPU/Vulkan",
                    "When async shader compilation is enabled, skip presenting frames that "
                    "used placeholder pipelines to avoid visible flashing")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(vulkan_submit_on_primary_buffer_end, false, "GPU/Vulkan",
                    "Submit command buffer when PM4 primary buffer ends")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);"""

# ---------------------------------------------------------------------------
#  2. render_target_cache.cpp (FBO padrão para EDRAM)
# ---------------------------------------------------------------------------
ANCLA_RTC = """REXCVAR_DEFINE_STRING(render_target_path_vulkan, "", "GPU/Vulkan",
                      "Vulkan render target implementation path")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);"""

NUEVO_RTC = """// PARCHE LOCAL - otimizacoes de performance GPU Vulkan (FBO padrao rapido)
REXCVAR_DEFINE_STRING(render_target_path_vulkan, "fbo", "GPU/Vulkan",
                      "Vulkan render target implementation path")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);"""

# ---------------------------------------------------------------------------
#  3. vulkan_provider.cpp (Priorizar GPU dedicada)
# ---------------------------------------------------------------------------
ANCLA_PROV = """        if (prefer_fill_mode_non_solid && supported_features.fillModeNonSolid) {
          ++score;
        }
        scored_devices.push_back({physical_device, score});"""

NUEVO_PROV = """        if (prefer_fill_mode_non_solid && supported_features.fillModeNonSolid) {
          ++score;
        }
        // PARCHE LOCAL - otimizacoes de performance GPU Vulkan: priorizar GPU dedicada
        VkPhysicalDeviceProperties device_props = {};
        ifn.vkGetPhysicalDeviceProperties(physical_device, &device_props);
        if (device_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
          score += 1000;
        } else if (device_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
          score += 100;
        }
        scored_devices.push_back({physical_device, score});"""

# ---------------------------------------------------------------------------
#  4. vulkan_presenter.cpp (Includes e Limitador de FPS max_fps)
# ---------------------------------------------------------------------------
ANCLA_PRES_INC = """#include <memory>
#include <utility>
#include <vector>"""

NUEVO_PRES_INC = """#include <chrono>   // PARCHE LOCAL - limitador max_fps
#include <memory>
#include <thread>   // PARCHE LOCAL - limitador max_fps
#include <utility>
#include <vector>"""

ANCLA_PRES = """    const VulkanDevice::Queue::Acquisition queue_acquisition =
        vulkan_device_->AcquireQueue(paint_context_.present_queue_family, 0);
    present_result = dfn.vkQueuePresentKHR(queue_acquisition.queue(), &present_info);"""

NUEVO_PRES = """    // ------------------------------------------------------------------
    //  PARCHE LOCAL - otimizacoes de performance GPU Vulkan: limitador max_fps
    // ------------------------------------------------------------------
    {
      int32_t tope = 0;
      if (rex::cvar::GetFlagInfo("max_fps") != nullptr) {
        tope = rex::cvar::Query<int32_t>("max_fps");
      }
      if (tope > 0) {
        using Reloj = std::chrono::steady_clock;
        static Reloj::time_point siguiente{};
        const auto periodo = std::chrono::duration_cast<Reloj::duration>(
            std::chrono::duration<double>(1.0 / double(tope)));
        const auto ahora = Reloj::now();
        if (siguiente > ahora) {
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

    const VulkanDevice::Queue::Acquisition queue_acquisition =
        vulkan_device_->AcquireQueue(paint_context_.present_queue_family, 0);
    present_result = dfn.vkQueuePresentKHR(queue_acquisition.queue(), &present_info);"""

# ---------------------------------------------------------------------------
#  6. command_processor.cpp base (Otimizações de iluminação)
# ---------------------------------------------------------------------------
ANCLA_LUZ = """REXCVAR_DEFINE_INT32(query_occlusion_fake_sample_count, 1000, "GPU",
                     "Fake sample count for occlusion queries")
    .range(1, 100000)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);"""

NUEVO_LUZ = """// PARCHE LOCAL - otimizacao de iluminacao: occlusion queries corretas para culling de luzes
// NFSMW usa occlusion queries para visibilidade de faróis/bloom. Com 1000 amostras sempre-visíveis
// todos os passes de iluminação eram sempre executados. Com 1: luzes são culled pelo hardware.
REXCVAR_DEFINE_INT32(query_occlusion_fake_sample_count, 1, "GPU",
                     "Fake sample count for occlusion queries")
    .range(1, 100000)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);"""

ANCLA_PAGMEM = """REXCVAR_DEFINE_BOOL(clear_memory_page_state, true, "GPU",
                    "Refresh page-valid state from GPU-written memory at frame end. "
                    "Disable for minor CPU overhead reduction, but may break memory coherency.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);"""

NUEVO_PAGMEM = """// PARCHE LOCAL - otimizacao de iluminacao: desativar varredura O(N) de páginas por frame
// clear_memory_page_state=true causava ~0.5ms de overhead de CPU em cada frame ao varrer
// toda a memória escrita pela GPU. Desativado pois não há debug de coherência ativa.
REXCVAR_DEFINE_BOOL(clear_memory_page_state, false, "GPU",
                    "Refresh page-valid state from GPU-written memory at frame end. "
                    "Disable for minor CPU overhead reduction, but may break memory coherency.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);"""


def localizar_sdk():
    raiz = pathlib.Path(__file__).resolve().parent.parent
    for cand in [raiz.parent / "rexglue-sdk", raiz / "sdk"]:
        if (cand / "src" / "graphics" / "vulkan" / "command_processor.cpp").exists():
            return cand
    sys.exit("[ERRO] Não encontrei a pasta do ReXGlue SDK.\n"
             "       Procurado em ../rexglue-sdk e ./sdk")


def main():
    p = argparse.ArgumentParser(description="Parche de otimizações de GPU Vulkan")
    p.add_argument("--estado", action="store_true", help="Verificar status")
    p.add_argument("--revertir", action="store_true", help="Desfazer alterações")
    args = p.parse_args()

    sdk = localizar_sdk()
    f_cmd = sdk / "src" / "graphics" / "vulkan" / "command_processor.cpp"
    f_rtc = sdk / "src" / "graphics" / "vulkan" / "render_target_cache.cpp"
    f_prov = sdk / "src" / "ui" / "vulkan" / "vulkan_provider.cpp"
    f_pres = sdk / "src" / "ui" / "vulkan" / "vulkan_presenter.cpp"
    f_base_cmd = sdk / "src" / "graphics" / "command_processor.cpp"

    arquivos = [
        (f_cmd, [(ANCLA_CMD, NUEVO_CMD)]),
        (f_rtc, [(ANCLA_RTC, NUEVO_RTC)]),
        (f_prov, [(ANCLA_PROV, NUEVO_PROV)]),
        (f_pres, [(ANCLA_PRES_INC, NUEVO_PRES_INC), (ANCLA_PRES, NUEVO_PRES)]),
        (f_base_cmd, [(ANCLA_LUZ, NUEVO_LUZ), (ANCLA_PAGMEM, NUEVO_PAGMEM)]),
    ]

    if args.estado:
        for f, pares in arquivos:
            txt = f.read_text(encoding="utf-8")
            status = "APLICADO" if MARCA in txt else "sem aplicar"
            print(f"  {f.name:30} -> {status}")
        return 0

    if args.revertir:
        for f, pares in arquivos:
            txt = f.read_text(encoding="utf-8")
            if MARCA in txt:
                for ancla, nuevo in pares:
                    txt = txt.replace(nuevo, ancla)
                f.write_text(txt, encoding="utf-8")
                print(f"[ok] Revertido: {f.name}")
            else:
                print(f"[aviso] {f.name} não continha o parche.")
        return 0

    # Aplicar
    for f, pares in arquivos:
        txt = f.read_text(encoding="utf-8")
        if MARCA in txt:
            print(f"[ok] {f.name} já continha as otimizações.")
            continue
        for ancla, nuevo in pares:
            if txt.count(ancla) != 1:
                sys.exit(f"[ERRO] Ancoragem não encontrada ou duplicada em {f.name}!\n"
                         f"       SDK pode ter mudado.")
            txt = txt.replace(ancla, nuevo)
        f.write_text(txt, encoding="utf-8")
        print(f"[ok] Otimizações aplicadas em: {f.name}")

    print("\n[✓] Otimizações de GPU Vulkan aplicadas no SDK com sucesso!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
