#!/usr/bin/env bash
# ==============================================================================
#  NFSMW Recompiled - Linux Vulkan Launcher & Debug Script
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="${SCRIPT_DIR}/app/build"
EXE="${BIN_DIR}/nfsmw"

if [ ! -f "$EXE" ]; then
    echo "[-] Erro: Executável não encontrado em: $EXE"
    echo "    Execute a compilação primeiro."
    exit 1
fi

# Garantir links de assets e bibliotecas no diretório do executável
if [ ! -e "${BIN_DIR}/game_root" ]; then
    ln -sfn "${SCRIPT_DIR}/game_root" "${BIN_DIR}/game_root"
fi
if [ ! -f "${BIN_DIR}/librexruntime.so" ]; then
    cp -P "${SCRIPT_DIR}/../rexglue-sdk/out/install/linux-amd64/lib/librexruntime.so" "${BIN_DIR}/"
fi
if [ ! -f "${BIN_DIR}/nfsmw.toml" ]; then
    cp "${SCRIPT_DIR}/app/nfsmw.toml" "${BIN_DIR}/nfsmw.toml"
fi

# Configuração do ambiente para Vulkan e Dual GPU (NVIDIA GTX 1050 Ti / AMD)
export LD_LIBRARY_PATH="${BIN_DIR}:${LD_LIBRARY_PATH}"

# Por padrão, se a GPU NVIDIA estiver disponível, direciona para ela
if command -v nvidia-smi >/dev/null 2>&1; then
    export __NV_PRIME_RENDER_OFFLOAD=1
    export __VK_LAYER_NV_optimus=NVIDIA_only
fi

EXTRA_ARGS=()
MODE="normal"

for arg in "$@"; do
    case "$arg" in
        debug)
            MODE="debug"
            ;;
        perf)
            MODE="perf"
            ;;
        amd)
            unset __NV_PRIME_RENDER_OFFLOAD
            unset __VK_LAYER_NV_optimus
            export MESA_VK_DEVICE_SELECT=1002:*
            ;;
        nvidia)
            export __NV_PRIME_RENDER_OFFLOAD=1
            export __VK_LAYER_NV_optimus=NVIDIA_only
            ;;
        *)
            EXTRA_ARGS+=("$arg")
            ;;
    esac
done

echo "=========================================================="
echo "  NFSMW Recompiled - Linux (Vulkan Backend)"
echo "=========================================================="
echo "  Modo: $MODE"
echo "  Diretório: $BIN_DIR"
echo "  Executável: $EXE"
echo "=========================================================="

cd "$BIN_DIR"

if [ "$MODE" = "debug" ]; then
    mkdir -p "${BIN_DIR}/logs"
    echo "[*] Iniciando com log detalhado (debug + noisy)..."
    exec "$EXE" \
        --gpu_backend=vulkan \
        --gpu=xenos \
        --log_level=debug \
        --log_noisy=true \
        --log_file="${BIN_DIR}/logs/debug.log" \
        "${EXTRA_ARGS[@]}"
elif [ "$MODE" = "perf" ]; then
    echo "[*] Iniciando em modo performance (V-Sync OFF)..."
    exec "$EXE" \
        --gpu_backend=vulkan \
        --gpu=xenos \
        --vsync=false \
        "${EXTRA_ARGS[@]}"
else
    exec "$EXE" \
        --gpu_backend=vulkan \
        --gpu=xenos \
        "${EXTRA_ARGS[@]}"
fi
