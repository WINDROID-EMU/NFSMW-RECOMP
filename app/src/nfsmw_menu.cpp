// nfsmw - menu de ajustes ingame (estilo GoldenEye, abierto con ESC)
//
// Manejado por cvars del recomp. Hay dos grupos, y cada control avisa de cual
// es:
//   * "se aplica al instante"  -> el SDK o un parche del proyecto refresca el
//                                 valor en vivo (fullscreen, vsync, max_fps,
//                                 anisotropic_override, present_effect, game_speed, etc.).
//   * "se aplica al reiniciar" -> el SDK los marca como pendientes de reinicio
//                                 (lifecycle kRequiresRestart), asi que el
//                                 menu muestra el boton "Aplicar y reiniciar",
//                                 que guarda el toml y relanza el ejecutable.

#include "nfsmw_menu.h"

#include <rex/cvar.h>

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
//  Cvar del proyecto: contenido Black Edition.
//
//  El parche se aplica al cargar el XEX (OnPostLoadXexImage en nfsmw_app.h),
//  por eso este flag exige reinicio para activarse o desactivarse.
// ---------------------------------------------------------------------------
REXCVAR_DEFINE_BOOL(black_edition, true, "Contenido",
                    "Contenido Black Edition: coches de pago como descargables (requiere reinicio)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace {

// Paleta del menu: inspirada en la interfaz del juego (fondo oscuro, acento
// naranja).
constexpr ImU32 kBg = IM_COL32(22, 26, 32, 252);
constexpr ImU32 kBorde = IM_COL32(232, 161, 60, 255);
constexpr ImU32 kMarco = IM_COL32(64, 72, 84, 255);
constexpr ImU32 kTexto = IM_COL32(220, 226, 232, 255);
constexpr ImU32 kTextoAtenuado = IM_COL32(148, 156, 168, 255);
constexpr ImU32 kAcento = IM_COL32(232, 161, 60, 255);
constexpr ImU32 kTabSel = IM_COL32(58, 66, 78, 255);
constexpr ImU32 kFondoWidget = IM_COL32(38, 44, 52, 255);
constexpr ImU32 kVivo = IM_COL32(96, 200, 86, 255);
constexpr ImU32 kAviso = IM_COL32(232, 161, 60, 255);

const char* kTitulosPestana[] = {
    "VÍDEO",
    "GRÁFICOS",
    "ÁUDIO & INPUT",
    "JOGO",
    "SISTEMA",
    "DEBUG"
};
constexpr int kNumPestanas = 6;

// Acceso a cvars como strings.
bool CvarB(const char* nombre) { return rex::cvar::GetFlagByName(nombre) == "true"; }
void SetCvarB(const char* nombre, bool valor) {
  rex::cvar::SetFlagByName(nombre, valor ? "true" : "false");
}
std::string CvarS(const char* nombre) { return rex::cvar::GetFlagByName(nombre); }
void SetCvarS(const char* nombre, const std::string& valor) {
  rex::cvar::SetFlagByName(nombre, valor);
}
float CvarF(const char* nombre) { return static_cast<float>(std::atof(CvarS(nombre).c_str())); }
void SetCvarF(const char* nombre, float valor) {
  rex::cvar::SetFlagByName(nombre, std::to_string(valor));
}
int CvarI(const char* nombre) { return std::atoi(CvarS(nombre).c_str()); }
void SetCvarI(const char* nombre, int valor) {
  rex::cvar::SetFlagByName(nombre, std::to_string(valor));
}
bool ExisteCvar(const char* nombre) { return rex::cvar::GetFlagInfo(nombre) != nullptr; }

struct Opcion {
  const char* etiqueta;
  const char* valor;
};

// Fila nombre/valor para la pestana DEBUG. "-" cuando el cvar no existe o esta vacio.
void FilaDebug(const char* nombre, const std::string& valor) {
  const char* texto = valor.empty() ? "—" : valor.c_str();
  ImGui::Text("  %-28s %s", nombre, texto);
}

std::string JuntarLista(const std::vector<std::string>& items) {
  std::string lista;
  for (const auto& item : items) {
    if (!lista.empty()) lista += ", ";
    lista += item;
  }
  return lista;
}

// Combo con opciones fijas.
void ComboSimple(const char* etiqueta, const std::string& actual, const Opcion* opciones, int cuenta,
                 const char* texto_si_otro, const std::function<void(const char*)>& al_cambiar) {
  int idx = 0;
  for (int i = 0; i < cuenta; ++i) {
    if (opciones[i].valor == actual) {
      idx = i;
      break;
    }
  }
  const char* mostrar = opciones[idx].etiqueta;
  if (actual != opciones[idx].valor) {
    mostrar = (texto_si_otro && *texto_si_otro) ? texto_si_otro : actual.c_str();
  }
  ImGui::PushID(etiqueta);
  ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                      ImVec2(FLT_MAX, ImGui::GetFrameHeightWithSpacing() * 8.0f));
  if (ImGui::BeginCombo("##combo", mostrar)) {
    for (int i = 0; i < cuenta; ++i) {
      const bool sel = (i == idx);
      if (ImGui::Selectable(opciones[i].etiqueta, sel)) {
        al_cambiar(opciones[i].valor);
      }
      if (sel) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  ImGui::SameLine();
  ImGui::TextUnformatted(etiqueta);
}

}  // namespace

NfsmwMenuDialog::NfsmwMenuDialog(rex::ui::ImGuiDrawer* drawer, Callbacks callbacks)
    : rex::ui::ImGuiDialog(drawer), callbacks_(std::move(callbacks)) {}

NfsmwMenuDialog::~NfsmwMenuDialog() = default;

void NfsmwMenuDialog::RequestClose() { Close(); }

void NfsmwMenuDialog::Persistir() {
  if (callbacks_.persist_config) {
    callbacks_.persist_config();
  }
}

void NfsmwMenuDialog::OnClose() {
  if (callbacks_.on_closed) {
    callbacks_.on_closed();
  }
  if (quit_requested_ && callbacks_.request_quit) {
    callbacks_.request_quit();
  }
}

void NfsmwMenuDialog::OnDraw(ImGuiIO& io) {
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
    selected_tab_ = (selected_tab_ + kNumPestanas - 1) % kNumPestanas;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
    selected_tab_ = (selected_tab_ + 1) % kNumPestanas;
  }

  const ImVec2 pantalla = io.DisplaySize;
  const float ancho = std::floor(std::min(pantalla.x * 0.92f, 860.0f));
  const float alto = std::floor(std::min(pantalla.y * 0.90f, 650.0f));
  const ImVec2 origen((pantalla.x - ancho) * 0.5f, (pantalla.y - alto) * 0.5f);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNav;
  ImGui::SetNextWindowPos(origen);
  ImGui::SetNextWindowSize(ImVec2(ancho, alto));
  if (!ImGui::Begin("##nfsmw_menu", nullptr, flags)) {
    ImGui::End();
    ImGui::PopStyleVar(2);
    return;
  }

  const float pad = 18.0f;
  const float borde = 2.0f;
  const float cabecera = 56.0f;
  const float pie = 30.0f;
  const float carril = 175.0f;

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 x0 = origen;
  const ImVec2 x1(origen.x + ancho, origen.y + alto);

  // Fondo y marco.
  dl->AddRectFilled(x0, x1, kBg, 8.0f);
  dl->AddRect(x0, x1, kBorde, 8.0f, 0, borde);
  dl->AddRectFilled(ImVec2(x0.x + borde, x0.y + borde),
                    ImVec2(x1.x - borde, x0.y + cabecera), IM_COL32(26, 30, 37, 255));

  // Cabecera.
  dl->AddText(ImGui::GetFont(), 26.0f,
              ImVec2(x0.x + pad, x0.y + pad - 6.0f), kTexto, "CONFIGURAÇÕES");
  dl->AddText(ImGui::GetFont(), 26.0f,
              ImVec2(x1.x - pad - std::min(ancho * 0.42f, 360.0f), x0.y + pad - 6.0f), kAcento,
              "NFS MOST WANTED");
  dl->AddLine(ImVec2(x0.x + pad, x0.y + cabecera), ImVec2(x1.x - pad, x0.y + cabecera), kMarco,
              borde);

  // Pie: accesos rapidos.
  const float y_pie = x1.y - pie;
  dl->AddLine(ImVec2(x0.x + pad, y_pie), ImVec2(x1.x - pad, y_pie), kMarco, borde);
  dl->AddText(ImGui::GetFont(), 14.0f, ImVec2(x0.x + pad, y_pie + 6.0f), kTextoAtenuado,
              "Use as setas para alternar abas   |   ESC fecha este menu");

  // Carril de pestanas a la izquierda.
  const float y_carril = x0.y + cabecera;
  for (int i = 0; i < kNumPestanas; ++i) {
    const float y_i = y_carril + static_cast<float>(i) * 44.0f;
    const bool sel = (i == selected_tab_);
    if (sel) {
      dl->AddRectFilled(ImVec2(x0.x + borde, y_i), ImVec2(x0.x + carril, y_i + 38.0f), kTabSel);
      dl->AddRectFilled(ImVec2(x0.x + borde, y_i), ImVec2(x0.x + 6.0f, y_i + 38.0f), kAcento);
    }
    dl->AddText(ImGui::GetFont(), 18.0f,
                ImVec2(x0.x + 14.0f, y_i + 8.0f), sel ? kTexto : kTextoAtenuado,
                kTitulosPestana[i]);
    ImGui::SetCursorScreenPos(ImVec2(x0.x + borde, y_i));
    if (ImGui::InvisibleButton(
            (std::string("##pestana") + std::to_string(i)).c_str(),
            ImVec2(carril - borde, 38.0f))) {
      selected_tab_ = i;
    }
  }

  // Contenido, a la derecha del carril.
  ImGui::SetCursorScreenPos(ImVec2(x0.x + carril + pad, y_carril + 8.0f));
  const ImVec2 tam_contenido(ancho - carril - 2.0f * pad, alto - cabecera - pie - 16.0f);
  ImGui::BeginChild("##nfsmw_contenido", tam_contenido, false, ImGuiWindowFlags_NoBackground);

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, kFondoWidget);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(48, 56, 66, 255));
  ImGui::PushStyleColor(ImGuiCol_Text, kTexto);
  ImGui::PushStyleColor(ImGuiCol_TextDisabled, kTextoAtenuado);
  ImGui::PushStyleColor(ImGuiCol_CheckMark, kAcento);
  ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(48, 56, 66, 255));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(58, 66, 78, 255));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, kAcento);
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, kAcento);

  // =========================================================================
  //  TAB 0: VÍDEO
  // =========================================================================
  if (selected_tab_ == 0) {
    ImGui::TextUnformatted("TELA E TAXA DE QUADROS");
    ImGui::Spacing();

    // Pantalla completa
    bool completo = CvarB("fullscreen");
    if (ImGui::Checkbox("Tela cheia (Fullscreen)", &completo)) {
      SetCvarB("fullscreen", completo);
      Persistir();
    }
    MarcaVivo("se aplica ao instante");

    // V-Sync
    bool vsync = CvarB("vsync");
    if (ImGui::Checkbox("V-Sync (Sincronização Vertical)", &vsync)) {
      SetCvarB("vsync", vsync);
      Persistir();
    }
    MarcaVivo("se aplica ao instante");

    // Limite de FPS
    if (ExisteCvar("max_fps")) {
      static const Opcion kFps[] = {
          {"30 FPS", "30"}, {"60 FPS", "60"}, {"90 FPS", "90"},
          {"120 FPS", "120"}, {"144 FPS", "144"}, {"180 FPS", "180"},
          {"Sem limite (0)", "0"}};
      ComboSimple("Limite de FPS", CvarS("max_fps"), kFps, 7, nullptr,
                  [this](const char* v) {
                    SetCvarS("max_fps", v);
                    Persistir();
                  });
      MarcaVivo("se aplica ao instante");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("RESOLUÇÃO E MODO DE VÍDEO");
    ImGui::Spacing();

    static const Opcion kRes[] = {
        {"480p (854x480 - Modo Leve / Android)", "480p"},
        {"540p (960x540)", "540p"},
        {"720p (1280x720 - Padrão Xbox 360)", "720p"},
        {"900p (1600x900)", "900p"},
        {"1080p (1920x1080 - Full HD)", "1080p"},
        {"1440p (2560x1440 - Quad HD)", "1440p"},
        {"4K (3840x2160 - Ultra HD)", "4k"}};
    ComboSimple("Resolução da Janela / Vídeo", CvarS("resolution"), kRes, 7,
                "Personalizada", [this](const char* v) {
                  SetCvarS("resolution", v);
                  Persistir();
                });
    MarcaReinicio();

    if (ExisteCvar("video_mode_refresh_rate")) {
      static const Opcion kRefresh[] = {
          {"60 Hz", "60"}, {"75 Hz", "75"}, {"120 Hz", "120"},
          {"144 Hz", "144"}, {"165 Hz", "165"}, {"240 Hz", "240"}};
      ComboSimple("Taxa de Atualização do Monitor", CvarS("video_mode_refresh_rate"), kRefresh, 6,
                  nullptr, [this](const char* v) {
                    SetCvarS("video_mode_refresh_rate", v);
                    Persistir();
                  });
      MarcaReinicio();
    }

    // Resolucao interna (supersampling)
    static const Opcion kIRes[] = {
        {"1x - Nativo (Mais rápido, ideal para GPU integrada e Android)", "1"},
        {"2x - 1440p (4x pixels, imagem muito nítida)", "2"},
        {"3x - 4K (9x pixels, alta carga de GPU)", "3"},
        {"4x - 5K/8K (16x pixels, GPUs potentes)", "4"}};
    ComboSimple("Resolução Interna do Motor (Supersampling)", CvarS("resolution_scale"), kIRes, 4,
                nullptr, [this](const char* v) {
                  SetCvarS("resolution_scale", v);
                  Persistir();
                });
    MarcaReinicioConAviso(
        "Multiplica a resolução dos render targets internos do jogo antes de apresentar na tela.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("UPSCALING E FILTROS");
    ImGui::Spacing();

    if (ExisteCvar("present_effect")) {
      static const Opcion kPresent[] = {
          {"Bilinear (Padrão)", "bilinear"},
          {"CAS (AMD Contrast Adaptive Sharpening)", "cas"},
          {"FSR (AMD FidelityFX Super Resolution)", "fsr"}};
      ComboSimple("Filtro de Apresentação / Upscaler", CvarS("present_effect"), kPresent, 3, nullptr,
                  [this](const char* v) {
                    SetCvarS("present_effect", v);
                    Persistir();
                  });
      MarcaVivo("se aplica ao instante");

      if (ExisteCvar("present_cas_additional_sharpness")) {
        float nitidez = CvarF("present_cas_additional_sharpness");
        if (ImGui::SliderFloat("Nitidez CAS / FSR", &nitidez, 0.0f, 1.0f, "%.2f")) {
          SetCvarF("present_cas_additional_sharpness", nitidez);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          Persistir();
        }
        MarcaVivo("se aplica ao instante");
      }
    }

    if (ExisteCvar("anisotropic_override")) {
      static const Opcion kAniso[] = {
          {"Desativado (Bilinear)", "0"}, {"1x", "1"}, {"2x", "2"},
          {"4x", "3"}, {"8x", "4"}, {"16x (Máxima Nitidez de Texturas)", "5"}};
      ComboSimple("Filtragem Anisotrópica", CvarS("anisotropic_override"), kAniso, 6, nullptr,
                  [this](const char* v) {
                    SetCvarS("anisotropic_override", v);
                    Persistir();
                  });
      MarcaVivo("se aplica ao instante");
    }

    if (ExisteCvar("swap_post_effect")) {
      static const Opcion kAA[] = {
          {"Desativado", "none"}, {"FXAA", "fxaa"}, {"FXAA Extreme", "fxaa_extreme"}};
      ComboSimple("Anti-Aliasing", CvarS("swap_post_effect"), kAA, 3, nullptr, [this](const char* v) {
        SetCvarS("swap_post_effect", v);
        Persistir();
      });
      MarcaReinicio();
    }

  // =========================================================================
  //  TAB 1: GRÁFICOS
  // =========================================================================
  } else if (selected_tab_ == 1) {
    ImGui::TextUnformatted("BACKEND E API GRÁFICA");
    ImGui::Spacing();

    if (ExisteCvar("gpu_backend")) {
      static const Opcion kApi[] = {
          {"Vulkan (Recomendado para Linux e Android)", "vulkan"},
          {"Direct3D 12 (Nativo Windows)", "d3d12"},
          {"Plume (Novo Backend experimental)", "plume"}};
      ComboSimple("API Gráfica (Backend)", CvarS("gpu_backend"), kApi, 3, nullptr, [this](const char* v) {
        SetCvarS("gpu_backend", v);
        if (std::string(v) == "plume") {
          SetCvarS("gpu_plugin", "plume");
        } else {
          SetCvarS("gpu_plugin", "xenos");
        }
        Persistir();
      });
      MarcaReinicio();
    }

    if (ExisteCvar("vulkan_device")) {
      static const Opcion kDev[] = {
          {"Automático (GPU padrão do sistema)", "-1"},
          {"Dispositivo 0 (Normalmente GPU dedicada)", "0"},
          {"Dispositivo 1 (Normalmente GPU integrada)", "1"}};
      ComboSimple("Dispositivo Vulkan", CvarS("vulkan_device"), kDev, 3, nullptr, [this](const char* v) {
        SetCvarS("vulkan_device", v);
        Persistir();
      });
      MarcaReinicio();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("EMULAÇÃO DA EDRAM (XBOX 360)");
    ImGui::Spacing();

    // Seletor de EDRAM path
    const char* cvar_edram = ExisteCvar("render_target_path_vulkan") ? "render_target_path_vulkan"
                                                                     : "render_target_path_d3d12";
    if (ExisteCvar(cvar_edram)) {
      static const Opcion kEdram[] = {
          {"rtv - Host Render Targets (Rápido, dobro de FPS)", "rtv"},
          {"rov - Pixel Shader Interlock (Mais preciso, muito lento)", "rov"}};
      ComboSimple("Caminho de Render da EDRAM", CvarS(cvar_edram), kEdram, 2, nullptr,
                  [this, cvar_edram](const char* v) {
                    SetCvarS(cvar_edram, v);
                    // Sincroniza em ambos os backends se existirem
                    if (ExisteCvar("render_target_path_d3d12")) SetCvarS("render_target_path_d3d12", v);
                    if (ExisteCvar("render_target_path_vulkan")) SetCvarS("render_target_path_vulkan", v);
                    Persistir();
                  });
      MarcaReinicioConAviso(
          "O modo 'rtv' utiliza os render targets padrão da GPU (essencial para boa performance no Android e iGPUs). "
          "O modo 'rov' emula o hardware fixo via shader pixel interlock e é muito mais lento.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("OTIMIZAÇÕES DE SHADERS E PIPELINES");
    ImGui::Spacing();

    if (ExisteCvar("async_shader_compilation")) {
      bool async_shader = CvarB("async_shader_compilation");
      if (ImGui::Checkbox("Compilação assíncrona de shaders (Async Shaders)", &async_shader)) {
        SetCvarB("async_shader_compilation", async_shader);
        Persistir();
      }
      MarcaReinicioConAviso("Evita travamentos súbitos (stutters) durante o jogo ao compilar novos shaders.");
    }

    if (ExisteCvar("vulkan_async_skip_incomplete_frames")) {
      bool skip_inc = CvarB("vulkan_async_skip_incomplete_frames");
      if (ImGui::Checkbox("Pular quadros com shaders incompletos (Vulkan)", &skip_inc)) {
        SetCvarB("vulkan_async_skip_incomplete_frames", skip_inc);
        Persistir();
      }
      MarcaReinicioConAviso("Mantenha DESATIVADO para fluidez máxima e evitar quedas bruscas de FPS ao encontrar novos efeitos.");
    }

    if (ExisteCvar("vulkan_submit_on_primary_buffer_end")) {
      bool submit_pm4 = CvarB("vulkan_submit_on_primary_buffer_end");
      if (ImGui::Checkbox("Submeter comandos a cada buffer PM4 (Vulkan)", &submit_pm4)) {
        SetCvarB("vulkan_submit_on_primary_buffer_end", submit_pm4);
        Persistir();
      }
      MarcaReinicioConAviso("Mantenha DESATIVADO para ativar o batching (loteamento) de comandos e reduzir overhead de CPU/driver.");
    }

    if (ExisteCvar("vulkan_dynamic_rendering")) {
      bool dyn = CvarB("vulkan_dynamic_rendering");
      if (ImGui::Checkbox("Dynamic Rendering Vulkan 1.3 (VK_KHR_dynamic_rendering)", &dyn)) {
        SetCvarB("vulkan_dynamic_rendering", dyn);
        Persistir();
      }
      MarcaReinicioConAviso("Elimina a recriação custosa de Framebuffers e RenderPasses entre trocas de alvo de render.");
    }

    if (ExisteCvar("readback_resolve")) {
      std::string rb = CvarS("readback_resolve");
      bool readback = (rb != "none" && rb != "false" && !rb.empty());
      if (ImGui::Checkbox("Leitura de buffer de exposição (readback_resolve)", &readback)) {
        SetCvarS("readback_resolve", readback ? "fast" : "none");
        Persistir();
      }
      MarcaReinicioConAviso(
          "Efeitos de bloom e desfoque de movimento. 'none' economiza banda da GPU e elimina esperas de sincronização CPU-GPU.");
    }

    if (ExisteCvar("gpu_3d_to_2d_texture")) {
      bool conv = CvarB("gpu_3d_to_2d_texture");
      if (ImGui::Checkbox("Otimização de texturas 3D para 2D", &conv)) {
        SetCvarB("gpu_3d_to_2d_texture", conv);
        Persistir();
      }
      MarcaReinicio();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("ILUMINAÇÃO E RENDER TARGETS");
    ImGui::Spacing();

    if (ExisteCvar("native_2x_msaa")) {
      bool msaa = CvarB("native_2x_msaa");
      if (ImGui::Checkbox("MSAA 2x nativo na iluminação e geometria", &msaa)) {
        SetCvarB("native_2x_msaa", msaa);
        Persistir();
      }
      MarcaReinicioConAviso("Desative para dobrar a performance dos efeitos de iluminação e sombras (1 amostra por pixel).");
    }

    if (ExisteCvar("gamma_render_target_as_unorm16")) {
      bool gamma16 = CvarB("gamma_render_target_as_unorm16");
      if (ImGui::Checkbox("Render targets de iluminação em 64-bit UNORM16", &gamma16)) {
        SetCvarB("gamma_render_target_as_unorm16", gamma16);
        Persistir();
      }
      MarcaVivo("Desative para usar sRGB de 32-bit nativo acelerado por hardware, aliviando o peso de iluminação/bloom na GPU.");
    }

  // =========================================================================
  //  TAB 2: ÁUDIO & INPUT
  // =========================================================================
  } else if (selected_tab_ == 2) {
    ImGui::TextUnformatted("ÁUDIO");
    ImGui::Spacing();

    if (ExisteCvar("audio_mute")) {
      bool mute = CvarB("audio_mute");
      if (ImGui::Checkbox("Silenciar todo o áudio do jogo", &mute)) {
        SetCvarB("audio_mute", mute);
        Persistir();
      }
      MarcaVivo("se aplica ao instante");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("CONTROLES: TECLADO E MOUSE (MNK)");
    ImGui::Spacing();

    if (ExisteCvar("mnk_mode")) {
      bool mnk = CvarB("mnk_mode");
      if (ImGui::Checkbox("Ativar emulação de gamepad por Teclado e Mouse", &mnk)) {
        SetCvarB("mnk_mode", mnk);
        Persistir();
      }
      MarcaVivo("se aplica ao instante");
    }

    if (ExisteCvar("mnk_mouse")) {
      bool mouse = CvarB("mnk_mouse");
      if (ImGui::Checkbox("Controle de câmera pelo Mouse (analógico direito)", &mouse)) {
        SetCvarB("mnk_mouse", mouse);
        Persistir();
      }
      MarcaVivo("se aplica ao instante");
    }

    if (ExisteCvar("mnk_sensitivity")) {
      float sens = CvarF("mnk_sensitivity");
      if (ImGui::SliderFloat("Sensibilidade do Mouse", &sens, 0.1f, 5.0f, "%.2fx")) {
        SetCvarF("mnk_sensitivity", sens);
      }
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        Persistir();
      }
      MarcaVivo("se aplica ao instante");
    }

    ImGui::Spacing();
    ImGui::TextColored(ImColor(kAcento), "Controles Padrão no Teclado:");
    ImGui::BulletText("Acelerar: E ou W (Gatilho Direito)");
    ImGui::BulletText("Freio / Marcha Ré: Q ou S (Gatilho Esquerdo)");
    ImGui::BulletText("Direção (Esquerda/Direita): A / D");
    ImGui::BulletText("Freio de mão: Espaço (Botão A)");
    ImGui::BulletText("Nitro: L (Botão X)");
    ImGui::BulletText("Olhar para trás: 1 (Ombro Esquerdo)");
    ImGui::BulletText("Pausar / Menu do Jogo: Enter / X (Start)");
    ImGui::BulletText("Abrir/Fechar este Menu: Tecla ESC");

  // =========================================================================
  //  TAB 3: JOGO
  // =========================================================================
  } else if (selected_tab_ == 3) {
    ImGui::TextUnformatted("CONTEÚDO ADICIONAL");
    ImGui::Spacing();

    bool black = CvarB("black_edition");
    if (ImGui::Checkbox("Desbloquear Conteúdo Black Edition", &black)) {
      SetCvarB("black_edition", black);
      Persistir();
    }
    MarcaReinicioConAviso(
        "Desbloqueia os carros exclusivos da edição Black Edition no concessionário do modo carreira.");

    if (ExisteCvar("grant_user_privileges")) {
      bool gp = CvarB("grant_user_privileges");
      if (ImGui::Checkbox("Privilégios de Usuário Gold (Acesso a modos online)", &gp)) {
        SetCvarB("grant_user_privileges", gp);
        Persistir();
      }
      MarcaReinicio();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("PERFIL XBOX LIVE (SIMULADO)");
    ImGui::Spacing();

    if (!gamertag_sync_) {
      const auto actual = CvarS("user_profile_name");
      std::memset(gamertag_, 0, sizeof(gamertag_));
      const size_t copia = std::min<size_t>(actual.size(), sizeof(gamertag_) - 1);
      std::copy(actual.begin(), actual.begin() + copia, gamertag_);
      gamertag_sync_ = true;
    }
    ImGui::SetNextItemWidth(340.0f);
    if (ImGui::InputText("Gamertag do jogador", gamertag_, sizeof(gamertag_))) {
      rex::cvar::SetFlagByName("user_profile_name", gamertag_);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      Persistir();
    }
    MarcaVivo("o jogo lê o nome a cada carregamento de perfil");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("VELOCIDADE DA SIMULAÇÃO");
    ImGui::Spacing();

    if (ExisteCvar("game_speed")) {
      float velocidad = CvarF("game_speed");
      if (ImGui::SliderFloat("Velocidade do jogo", &velocidad, 20.0f, 200.0f, "%.0f%%")) {
        velocidad = std::clamp(velocidad, 20.0f, 200.0f);
        SetCvarF("game_speed", velocidad);
      }
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        Persistir();
      }
      MarcaVivo("se aplica ao instante (permite acelerar ou criar efeito de câmera lenta)");
    }

  // =========================================================================
  //  TAB 4: SISTEMA
  // =========================================================================
  } else if (selected_tab_ == 4) {
    ImGui::TextUnformatted("ESTADO E AÇÕES");
    ImGui::Spacing();

    const auto pendientes = rex::cvar::GetPendingRestartFlags();
    if (pendientes.empty()) {
      ImGui::TextColored(ImColor(kVivo), "Todas as opções estão sincronizadas (nenhum reinício pendente).");
    } else {
      ImGui::TextColored(ImColor(kAviso), "Opções que necessitam de reinício:");
      ImGui::TextWrapped("%s", JuntarLista(pendientes).c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("CONTINUAR JOGANDO", ImVec2(260.0f, 32.0f))) {
      Close();
    }
    ImGui::Spacing();

    if (ImGui::Button("SALVAR CONFIGURAÇÃO", ImVec2(260.0f, 32.0f))) {
      Persistir();
    }
    ImGui::Spacing();

    if (ImGui::Button("RESTAURAR PADRÕES", ImVec2(260.0f, 32.0f))) {
      rex::cvar::ResetAllToDefaults();
      Persistir();
    }
    ImGui::Spacing();

    if (BotonAplicar()) {
      Persistir();
      if (callbacks_.request_restart) {
        callbacks_.request_restart();
      }
    }
    ImGui::Spacing();

    if (ImGui::Button("SAIR DO JOGO", ImVec2(260.0f, 32.0f))) {
      quit_requested_ = true;
      Persistir();
      Close();
    }

  // =========================================================================
  //  TAB 5: DEBUG
  // =========================================================================
  } else {
    ImGui::TextUnformatted("TELEMETRIA E DESEMPENHO");
    ImGui::Spacing();

    if (callbacks_.sample_fps) {
      const auto stats = callbacks_.sample_fps();
      ImGui::TextColored(ImColor(kAcento), "  Taxa de Quadros (FPS): %.1f", stats.fps);
      ImGui::TextColored(ImColor(kAcento), "  Tempo de Quadro: %.2f ms", stats.frame_time_ms);
      ImGui::TextColored(
          ImColor(kTextoAtenuado),
          "  (A taxa é atualizada a cada segundo durante a execução)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("NÍVEL DE LOG E DIAGNÓSTICO");
    ImGui::Spacing();

    if (ExisteCvar("log_level")) {
      static const Opcion kLogs[] = {
          {"Info (Padrão)", "info"}, {"Debug (Detalhado)", "debug"},
          {"Trace (Máximo)", "trace"}, {"Warning (Apenas avisos)", "warn"},
          {"Error (Apenas erros)", "error"}};
      ComboSimple("Nível de Registro (Log)", CvarS("log_level"), kLogs, 5, nullptr,
                  [this](const char* v) {
                    SetCvarS("log_level", v);
                    Persistir();
                  });
      MarcaVivo("se aplica ao instante");
    }

    if (ExisteCvar("log_noisy")) {
      bool noisy = CvarB("log_noisy");
      if (ImGui::Checkbox("Ativar Log Ruidoso / Alta Frequência (log_noisy)", &noisy)) {
        SetCvarB("log_noisy", noisy);
        Persistir();
      }
      MarcaVivo("registra todo o tráfego do decodificador de áudio XMA e GPU");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("PARÂMETROS TÉCNICOS ATIVOS");
    ImGui::Spacing();
    FilaDebug("Backend gráfico", CvarS("gpu_backend"));
    FilaDebug("Plugin GPU", CvarS("gpu_plugin"));
    FilaDebug("Caminho EDRAM", CvarS(ExisteCvar("render_target_path_vulkan") ? "render_target_path_vulkan" : "render_target_path_d3d12"));
    FilaDebug("Filtro Apresentação", CvarS("present_effect"));
    FilaDebug("Anti-Aliasing", CvarS("swap_post_effect"));
    FilaDebug("Modo de Vídeo", CvarS("video_mode_width") + "x" + CvarS("video_mode_height"));
    FilaDebug("Escala Interna", CvarS("resolution_scale"));
    FilaDebug("Tela Cheia", CvarS("fullscreen"));
    FilaDebug("V-Sync", CvarS("vsync"));
    FilaDebug("Limite FPS", CvarS("max_fps"));
    FilaDebug("Velocidade do Jogo", CvarS("game_speed"));
    FilaDebug("Gamertag", CvarS("user_profile_name"));
    FilaDebug("Black Edition", CvarS("black_edition"));

    ImGui::Spacing();
    ImGui::TextColored(
        ImColor(kTextoAtenuado),
        "Pressione F3 para o painel de estatísticas avançadas do ReXGlue ou F4 para "
        "editar todos os cvars brutos da engine.");
  }

  // Se houver flags pendentes de reinicio e nao estivermos na aba SISTEMA, mostra um aviso
  if (selected_tab_ != 4) {
    const auto pend = rex::cvar::GetPendingRestartFlags();
    if (!pend.empty()) {
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      ImGui::TextColored(ImColor(kAviso),
                         "Existem alterações que exigem reiniciar o jogo para surtir efeito.");
      if (BotonAplicar()) {
        Persistir();
        if (callbacks_.request_restart) {
          callbacks_.request_restart();
        }
      }
    }
  }

  ImGui::PopStyleColor(9);
  ImGui::PopStyleVar(2);
  ImGui::EndChild();

  ImGui::End();
  ImGui::PopStyleVar(2);
}

// ---------------------------------------------------------------------------
//  Helpers de desenho
// ---------------------------------------------------------------------------
void NfsmwMenuDialog::MarcaVivo(const char* texto) {
  ImGui::TextColored(ImColor(kVivo), "%s", texto);
  ImGui::Spacing();
}

void NfsmwMenuDialog::MarcaReinicio(const char* texto) {
  ImGui::TextColored(ImColor(kAviso), "%s", (texto && *texto) ? texto : "(requer reinício)");
  ImGui::Spacing();
}

void NfsmwMenuDialog::MarcaReinicioConAviso(const char* texto) {
  ImGui::TextColored(ImColor(kAviso), "Requer reiniciar o jogo para surtir efeito.");
  ImGui::TextColored(ImColor(kTextoAtenuado), "%s", texto);
  ImGui::Spacing();
}

bool NfsmwMenuDialog::BotonAplicar() {
  return ImGui::Button("APLICAR E REINICIAR", ImVec2(260.0f, 32.0f));
}