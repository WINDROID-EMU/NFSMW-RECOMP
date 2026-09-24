# Boas Práticas de Outros Ports rexglue para Android — Brain Log

> Fontes: https://github.com/Buku313/Skate3-Mobile e
> https://github.com/SansNope/UnleashedRecomp-Android
> Dois ports Android de recompilações estáticas rexglue-based, cada um com problemas
> reais de hardware já diagnosticados e resolvidos. Objetivo: dar a um agente de IA
> trabalhando em qualquer port Android do usuário (NFSMW-RECOMP incluso) um atalho para
> problemas que outros já resolveram, em vez de redescobri-los do zero.
> Complementa `RunTime-brain-log.md` e `NFSMW-RECOMP-brain-log.md`.

## Skate3-Mobile (fork Android de Skate3Recomp)

- Diferença estrutural importante em relação ao NFSMW-RECOMP: o Skate3Recomp (upstream)
  já tem um renderer nativo próprio suportando D3D12 **e** Vulkan simultaneamente desde a
  v2.0.0 — não emula a GPU Xenos; reconstrói os desenhos observando por hooks as
  submissões de malha/textura/shader/constantes do jogo original e redesenha com shaders
  Vulkan nativos. O fork Android reaproveitou o backend Vulkan já existente no
  Linux/macOS, sem escrever um backend novo do zero.
- Requisitos mínimos de hardware explícitos: Android 13+, arm64-v8a, ARMv8.2 com FP16 e
  extensões dot-product obrigatórias — tratado como gate de compatibilidade explícito no
  launcher, não como suposição implícita.
- Suporte a páginas de memória de 4 KB **e** 16 KB do Android já é item concluído
  (relevante desde o Android 15, principalmente linha Pixel — recomps costumam depender
  pesadamente de `mmap` para reservar 4 GB de espaço de endereço virtual do guest com
  premissas de alinhamento herdadas do desktop; falhas aqui costumam ser silenciosas ou
  crash no boot, não um erro claro).
- Perfis de dispositivo **nomeados**, em vez de toggles soltos: dois perfis alternáveis no
  mesmo APK — "Performance" (resolução interna baixa, LOD reduzido, sem grama/efeitos
  caros) e "Quality" (resolução interna alta, LOD original, MSAA, sombras, SSAO, bloom,
  volumétricos) — com um único botão "Apply & Restart". Reduz a chance de o usuário
  montar uma combinação que não roda no aparelho dele, e é mais fácil de validar em QA
  (2–3 combinações fixas, não uma matriz combinatória de cvars individuais).
- Importação de driver Vulkan sem root via **libadrenotools**
  (https://github.com/bylaws/libadrenotools), carregando pacotes ADPKG/ExynosTools de
  dentro da pasta interna privada do app, com o driver do sistema sempre como fallback
  permanente.
- Usa **SDL3** (não SDL2) para janela/controle/áudio no Android, com uma ponte XInput
  nativa que funde o overlay de toque com o jogador um.
- Instalador embutido no app baixa a atualização oficial exigida de uma fonte controlada
  pelo próprio projeto, verifica tamanho e SHA-256 dos pacotes, e só então gera os
  arquivos derivados necessários para rodar — com fallback de seleção manual de arquivo
  se o download automático falhar.
- Auto-atualização do APK fora da Play Store: o launcher consulta um manifesto hospedado
  no próprio repositório, compara código de versão, baixa e verifica o novo APK via
  SHA-256, entrega ao instalador de pacotes do Android sem tocar na pasta do jogo já
  instalada.
- Compatibilidade retroativa de caminho de armazenamento: instalações antigas de
  testadores continuam sendo detectadas e funcionando depois que o app migrou o padrão
  de novas instalações para armazenamento sandboxed.
- Scripts de build locais para não-programadores (arquivo de duplo clique em Mac Apple
  Silicon) como complemento ao CI — reduz a barreira de entrada para quem só quer gerar
  um APK personalizado sem mexer em terminal.

## UnleashedRecomp-Android (fork Android de UnleashedRecomp, exclusivo Adreno)

O mais rico em achados técnicos concretos de hardware dos dois — vale revisar antes de
investigar qualquer bug de GPU/driver Vulkan em Android.

### O problema central: driver Adreno de fábrica não é suficiente

- O renderer do jogo depende de `VK_KHR_buffer_device_address` e aritmética de endereço
  de 64 bits em shaders (capacidade SPIR-V `Int64`/`shaderInt64`).
- O driver Adreno de fábrica em vários SoCs recentes só reporta Vulkan 1.1 e
  `shaderInt64 = false` — quase todo pipeline gráfico falha ao compilar
  (`VK_ERROR_UNKNOWN`, assert interno do compilador de shader da Adreno).
- **Solução usada**: carregar o driver open-source Mesa Turnip (Vulkan 1.4,
  `shaderInt64` completo) em tempo de execução via libadrenotools — a mesma técnica
  usada por emuladores Android. Um aparelho cujo driver de fábrica já seja Vulkan 1.3+
  com `shaderInt64` pode em tese dispensar o Turnip; mesmo assim vale usá-lo para manter
  um único caminho de código em vez de dois.

### Achados de bugs por família de GPU (de investigação em hardware real, não suposição)

- **Corrupção transitória tipo "cintilação"** (texturas/modelos se desfazendo por ~1
  frame) isolada por bissecção bit a bit das flags de debug do driver: apenas uma flag
  de sincronização do command-processor (não um cache flush) era necessária e
  suficiente — por isso nenhuma barreira da API Vulkan sozinha resolvia (o problema
  vivia no meio do render pass, entre draws). Confirmado como específico do driver
  aberto/família de GPU (uma GPU desktop de outro fabricante rodava o mesmo stream de
  comandos sem problema). Corrigido "assando" essa flag de sincronização por draw,
  incondicionalmente, direto no driver.
- **Bug diferente numa geração de GPU mais nova, disparado pelo MSAA**: com MSAA ligado
  há corrupção; com MSAA desligado, numa build idêntica, tudo limpo. Uma pista inicial
  (flush de cache de cor por draw) custava ~40% de FPS e só mascarava o sintoma. **Lição
  aplicável a qualquer investigação de corrupção visual em GPU integrada: testar
  primeiro se desligar MSAA já resolve, antes de aplicar um fix caro de sincronização**
  — frequentemente o bug real está no caminho de resolve/tile do MSAA do driver, não em
  falta de barreira.
- **Chip ausente da tabela de dispositivos do driver open-source**: resolvido
  "emprestando" a entrada de outro chip da mesma família de GPU para o id do chip
  faltante — funcionou de primeira nesse caso.
- **Chips sem suporte nenhum no driver upstream**: o driver cai para um perfil de GPU
  errado (geometria de memória interna, contagem de unidades de cache, registradores
  específicos), causando artefatos fortes. Resolvido combinando entradas de dispositivo
  de um driver comunitário derivado de traces de comando do driver proprietário real,
  junto com o fix de sincronização acima.
- **Lição geral mais importante de toda essa seção**: bugs de GPU integrada em Android
  raramente têm uma causa única "óbvia" — o mesmo sintoma visual (corrupção, artefato,
  tela preta) pode ter causas diferentes em famílias de GPU diferentes, mesmo dentro do
  mesmo fabricante. **Bissecção sistemática de flags de debug do driver, uma por vez, em
  hardware real, foi o método que funcionou** — não adivinhação de causa a partir da
  aparência do artefato.
- Bugs assim, quando reproduzíveis numa build limpa do driver upstream (sem patches do
  próprio fork), valem a pena ser reportados ao mantenedor do driver — confirma que não
  é um problema introduzido pelo port.

### Áudio

- Reescrito para um modelo "produtor cronometrado / consumidor trivial": uma thread
  produtora roda o guest uma vez por fatia de tempo decorrido (corrige um déficit
  permanente de fila que causava estalos em dispositivos que consomem áudio em rajadas
  grandes), com uma pequena folga de buffer, um watchdog de stream morto e correção de
  deriva.
- No Android, usar modo de performance "normal" em vez de "baixa latência" no backend de
  áudio evita um caminho de baixo nível frágil em alguns aparelhos, e um buffer de
  dispositivo maior ajuda.
- **Invariante de design a preservar em qualquer port**: o relógio de áudio do motor
  nunca deve depender da "vivacidade" do stream da plataforma, e código do guest nunca
  deve rodar na própria thread de áudio da plataforma.
- Detecção automática de "hardware fraco" não deve forçar sozinha configurações caras ou
  arriscadas (o projeto tinha uma detecção que forçava MSAA no Android por padrão — hoje
  sabidamente causa parte da corrupção documentada acima; foi desativada).

### Diagnóstico em campo (sem acesso físico ao aparelho do usuário)

- Log sempre ativo, espelhando toda linha de log mais a saída de erro padrão (onde o
  driver Vulkan imprime mensagens de erro de GPU) para um arquivo, sem buffer.
- **Watchdog de travamento**: uma thread em segundo plano despeja o estado de cada thread
  do processo no log se os frames pararem de avançar por mais de alguns segundos —
  permite distinguir um travamento do driver/GPU (thread de render presa numa espera de
  hardware) de um deadlock do lado do guest (thread presa numa espera interna), só
  olhando o log que o próprio usuário remoto manda.
- Fluxo de captura de trace da API Vulkan desligado por padrão, ativável só por um
  arquivo-marcador que o usuário cria manualmente — útil para mandar evidência
  reproduzível a quem mantém o driver, sem exigir acesso via cabo/terminal do usuário
  final.

### Controles de toque em tela

- Overlay translúcido desenhado com a própria camada de UI do jogo, reaproveitando os
  ícones de botão já existentes no atlas de controle do jogo, para bater com os prompts
  in-game.
- Multi-toque funcional (mover + pular + usar item ao mesmo tempo, por exemplo).
- Regra de visibilidade que vale para qualquer port com suporte a gamepad físico e
  virtual ao mesmo tempo: o overlay aparece por padrão, **some no instante em que um
  controle físico manda qualquer input**, e volta a aparecer no próximo toque na tela —
  nunca atrapalha quem está usando um controle físico, e volta na hora quando a pessoa
  larga o controle.
- Um dedo que pousa sobre o analógico/botão captura aquele controle até soltar; nunca
  deixar o mesmo toque mover a câmera **e** acionar um botão ao mesmo tempo (separação de
  zona de toque).

### Armadilhas de build (Windows, cross-compile para Android)

- Usar um caminho de checkout **sem espaços** — ferramentas de dependência do CMake/MSVC
  falham de forma enganosa com espaços no próprio caminho da ferramenta.
- Scripts que dependem de variáveis de ambiente configuradas pelo instalador do Visual
  Studio podem falhar silenciosamente em algumas instalações — mais seguro configurar
  PATH/INCLUDE/LIB explicitamente nos próprios scripts de build.
  NDK recente (com alinhamento de 16 KB por padrão) é importante para Android 15+.
- Bibliotecas carregadas dinamicamente por nome (não linkadas estaticamente) não são
  puxadas automaticamente pelo CMake para dentro do pacote final — precisam ser copiadas
  manualmente para a pasta de libs nativas do APK antes de empacotar, toda vez que o
  código nativo muda.
- Copiar arquivos do jogo para a pasta de dados do app via linha de comando direta (fora
  do fluxo normal do Android) pode falhar silenciosamente por permissão — a via
  confiável é o gerenciador de arquivos/transferência MTP do próprio Android.

## Para o agente de IA: como aplicar isso ao NFSMW-RECOMP (e a outros ports do usuário)

1. O bug já documentado de "Vulkan renderiza em preto no Intel" no NFSMW-RECOMP (ver
   `NFSMW-RECOMP-brain-log.md`) tem o mesmo formato dos bugs de GPU acima: carrega sem
   erro, mas não desenha nada visível. Vale aplicar o mesmo método — bissecção de flags
   de debug do driver Vulkan uma a uma — em vez de tentar adivinhar a causa pela
   aparência do problema.
2. Ao levar o port Android para produção, testar explicitamente com MSAA desligado antes
   de investigar qualquer corrupção visual em GPU integrada — é a variável mais barata de
   descartar primeiro.
3. Não confiar apenas no driver Vulkan de fábrica do aparelho: mapear desde cedo se o
   SoC-alvo reporta as extensões/capacidades que o pipeline gráfico do jogo realmente
   usa, e ter um plano B de driver alternativo (Turnip via libadrenotools é o padrão de
   fato nesses dois projetos) antes de gastar tempo depurando "por que o shader não
   compila".
4. Qualquer diagnóstico remoto (sem o aparelho do usuário em mãos) precisa de, no
   mínimo: informação do dispositivo no topo do log, e um watchdog de travamento que
   despeje o estado das threads — sem isso, um "travou no meu celular" do usuário final
   é praticamente inacionável.
5. Perfis de dispositivo nomeados (2–3 combinações fixas) em vez de expor cada opção
   gráfica individualmente reduzem a superfície de suporte — vale considerar para o port
   Android do NFSMW-RECOMP assim que a base estiver rodando de forma estável.
6. Se/quando o NFSMW-RECOMP ganhar controles em tela, aplicar a mesma regra de
   visibilidade (esconde com controle físico, volta ao tocar a tela) e a mesma separação
   de zona de toque (dedo na câmera nunca aciona botão).
7. Este arquivo é referência cruzada de `RunTime-brain-log.md` e
   `NFSMW-RECOMP-brain-log.md` — juntos, os três cobrem: o SDK base, o projeto específico
   do usuário, e o que outros ports Android do mesmo tipo de SDK já resolveram por
   tentativa e erro em hardware real.
