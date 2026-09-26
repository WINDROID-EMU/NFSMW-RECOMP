# Prompt: Migrar o backend gráfico do RunTime (ReXGlue) de emulação Xenos em runtime para recompilação estática de shaders (XenosRecomp) + plume

Cole este prompt inteiro numa ferramenta de codificação com agente (Claude Code, por exemplo) rodando dentro do repositório `RunTime`, com o repositório `NFSMW-RECOMP` acessível como referência do jogo alvo.

---

## Contexto

Estou trabalhando em dois repositórios:

- `RunTime` (https://github.com/WINDROID-EMU/RunTime.git) — o SDK ReXGlue. Recompila estaticamente o código PowerPC do Xbox 360 em C++ (isso já funciona, é AOT, baseado no mesmo modelo do XenonRecomp/UnleashedRecomp — ver `src/codegen/` e `include/rex/ppc/`). O problema é a camada de **GPU**: ela é herdada da Xenia e **emula o Xenos em runtime** — interpreta o command buffer PM4, decodifica registradores da GPU e traduz shaders on-the-fly. Isso está em `src/graphics/` (principalmente `command_processor.cpp`, `xenos.cpp`, `graphics_system.cpp`, `primitive_processor.cpp`, `packet_disassembler.cpp`, `shared_memory.cpp`) e `include/rex/graphics/` (`xenos.h`, `registers.h`, `register_table.inc`, `command_processor.h`, `primitive_processor.h`).
- `NFSMW-RECOMP` (https://github.com/WINDROID-EMU/NFSMW-RECOMP.git) — o jogo alvo (recompilação nativa de Need for Speed: Most Wanted 2005, Xbox 360), construído sobre o RunTime/ReXGlue SDK.

Quero portar a abordagem usada no projeto **UnleashedRecomp** (o port recompilado de Sonic Unleashed, de hedge-dev/Skyth/Sajid/Darío) para este projeto. A diferença central:

- UnleashedRecomp **não emula a GPU Xenos em runtime**. Os shaders do jogo são extraídos uma vez, offline, e convertidos de binário Xenos para HLSL pelo **XenosRecomp** (https://github.com/hedge-dev/XenosRecomp), depois compilados para SPIR-V/DXIL via DXC. O código do jogo recompilado chama diretamente uma camada de backend gráfico fina construída sobre **plume** (https://github.com/renderbag/plume — RHI de baixo nível sobre Vulkan/D3D12/Metal, criado por Darío para o RT64).
- Isso elimina inteiramente o parsing de registradores/pacotes Xenos em tempo real, que é o que está consumindo GPU/CPU de forma desnecessária no RunTime hoje.

## Objetivo

Migrar o backend gráfico do RunTime do modelo "emulação Xenos em runtime" para o modelo "recompilação estática de shaders + RHI fino (plume)", usando o NFSMW-RECOMP como primeiro jogo de validação. Ao final, `command_processor.cpp`/`primitive_processor.cpp`/`packet_disassembler.cpp` (o caminho de interpretação de registradores e pacotes Xenos) devem sair do caminho crítico de execução.

## Repositórios de referência (ler antes de propor mudanças)

- https://github.com/hedge-dev/UnleashedRecomp — projeto de referência completo (arquitetura do backend gráfico, como plume é usado, como os shaders recompilados são carregados e ligados aos draw calls do jogo)
- https://github.com/hedge-dev/XenonRecomp — recompilador estático de PPC (o RunTime já segue esse modelo pro lado de CPU; útil só como referência de convenções, ex. `config.cpp` do RunTime já cita o toml config dele)
- https://github.com/hedge-dev/XenosRecomp — conversor de shaders binários Xenos para HLSL (ferramenta offline, não faz parte do runtime)
- https://github.com/renderbag/plume — o RHI a ser adotado

## Plano de execução (em fases — não pule fases, valide cada uma antes de seguir)

### Fase 0 — Levantamento
1. Ler `src/graphics/command_processor.cpp`, `graphics_system.cpp`, `xenos.cpp` e os headers correspondentes no RunTime, e mapear exatamente: (a) onde o command buffer é lido, (b) onde os shaders são traduzidos hoje, (c) como o `Shader*` ativo é resolvido e passado pro draw, (d) todos os pontos de entrada que o código do jogo recompilado (NFSMW-RECOMP) chama nessa camada.
2. Ler a arquitetura de backend gráfico da UnleashedRecomp (diretório equivalente a `src/gpu/` ou `render_backend/` no repo dela) e documentar como plume é inicializado, como shaders pré-compilados são carregados em runtime, e como os draw calls do jogo recompilado chegam até o plume sem passar por parsing de registradores.
3. Produzir um documento curto (`docs/migracao-plume.md`) com o mapeamento de equivalência: função/arquivo do RunTime hoje → função/arquivo equivalente na UnleashedRecomp → o que precisa ser criado do zero para o RunTime.

### Fase 1 — Infraestrutura
1. Adicionar `plume` como submódulo em `thirdparty/plume`, seguindo o padrão dos submódulos já existentes em `.gitmodules`.
2. Adicionar `plume` ao `CMakeLists.txt` raiz e ao `src/graphics/CMakeLists.txt`, sem ainda remover nada da Xenos existente (as duas stacks convivem por enquanto).
3. Buildar o RunTime com plume linkado, apenas para confirmar que a dependência compila no target Android ARM64 (`build-android-arm64` já existe como workflow de CI — verificar `.github/workflows/`).

### Fase 2 — Ferramenta XenosRecomp offline
1. Trazer o XenosRecomp como ferramenta de build (não como dependência de runtime) — ou como submódulo em `tools/` ou como repositório irmão, seguindo o que a UnleashedRecomp faz.
2. No NFSMW-RECOMP, extrair os shaders do `default.xex` do jogo (usar/adaptar `EXTRAER_XEX.bat` e as ferramentas já presentes em `tools/`) e localizar os containers de shader Xenos (formato descrito no README do XenosRecomp — atenção à variante de container usada pelo NFSMW, pode não ser idêntica à do Sonic Unleashed).
3. Rodar o XenosRecomp sobre os shaders extraídos, gerar HLSL, compilar para SPIR-V via DXC, e conferir que a geração não quebra silenciosamente em nenhum shader (logar falhas por shader).

### Fase 3 — Novo backend gráfico
1. Criar um novo diretório, ex. `src/graphics/plume_backend/`, com uma implementação de `GraphicsSystem`/`CommandProcessor`-equivalente que:
   - Inicializa plume (dispositivo, swapchain, etc.)
   - Carrega os binários SPIR-V pré-compilados dos shaders do NFSMW gerados na Fase 2
   - Expõe pontos de entrada que recebem draw calls diretamente do código do jogo recompilado, sem interpretar registradores Xenos
2. Para cada shader/efeito usado pelo NFSMW, mapear manualmente: vertex declaration, constant buffers, samplers/texturas, e estado de pipeline (blend, depth, rasterizer) — comparando visualmente contra o comportamento atual (via Xenos emulada) como referência de corretude. Isso é trabalho por-jogo, não automatizável; ir efeito por efeito (comece pelos shaders de menu/UI antes dos de mundo aberto/trânsito, que são mais complexos).
3. Manter um flag de build (`RUNTIME_GFX_BACKEND=xenos|plume`) para poder comparar as duas implementações lado a lado durante a migração.

### Fase 4 — Corte da emulação antiga
1. Só depois que o backend plume cobrir visualmente o essencial do NFSMW (menus, HUD, mundo aberto, replay), remover do caminho de execução: `command_processor.cpp`, `primitive_processor.cpp`, `packet_disassembler.cpp` e o register file do Xenos (`register_file.cpp`, `registers.cpp`, `register_table.inc`), mantendo-os no repositório apenas se outros jogos ainda dependerem deles, ou removendo de vez se o RunTime for assumir plume como único backend.
2. Atualizar `docs/` do RunTime e do NFSMW-RECOMP para refletir a nova arquitetura.

## Critérios de aceitação por fase

- Fase 1: build Android ARM64 passa com plume linkado, sem regressão no build existente.
- Fase 2: XenosRecomp processa 100% dos shaders extraídos do NFSMW sem erro fatal (falhas pontuais são aceitáveis e devem ser listadas, não escondidas).
- Fase 3: o jogo roda via backend plume até o primeiro menu principal com paridade visual aceitável comparado ao backend Xenos atual.
- Fase 4: medir FPS/uso de GPU antes/depois em um mesmo trecho de gameplay (free roam) para confirmar o ganho de performance que motivou a migração.

## Restrições

- Não modificar comportamento do lado de CPU (PPC→C++) — isso já está correto e não faz parte deste trabalho.
- Não remover a stack Xenos antiga antes de ter uma alternativa funcional validada (Fase 4 só depois da Fase 3 confirmada).
- Documentar toda decisão de mapeamento shader-a-shader que não seja óbvia, porque isso não é reaproveitável automaticamente para outros jogos que rodem sobre o RunTime.
- Qualquer coisa ambígua sobre o formato de container de shader do NFSMW (pode diferir do formato assumido pelo XenosRecomp, que foi desenhado em cima do Sonic Unleashed) deve ser investigada e documentada antes de generalizar código.

---

Comece pela Fase 0. Não escreva código ainda — primeiro me entregue o documento de mapeamento (`docs/migracao-plume.md`) e um resumo do que vai ser necessário adaptar no XenosRecomp para o formato de shader do NFSMW, se for diferente do Sonic Unleashed.
