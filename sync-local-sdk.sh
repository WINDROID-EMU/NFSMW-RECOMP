#!/usr/bin/env bash
# ==============================================================================
#  sync-local-sdk.sh — Sincroniza o RunTime compilado localmente com o APK
# ==============================================================================
#
#  O que faz:
#    1. Compila o ReXGlue Runtime (RunTime/) para Android arm64 — Release
#    2. Copia o resultado (rexglue-sdk) para android/rexglue-sdk/
#    3. O Gradle (syncRexGlueSoLibs) copiará as .so para jniLibs/ na
#       próxima execução de build_apk.sh
#
#  Uso:
#    ./sync-local-sdk.sh            # compila e sincroniza
#    ./sync-local-sdk.sh --no-build # só sincroniza (RunTime já foi compilado)
#    ./sync-local-sdk.sh --help
#
#  Pré-requisitos:
#    - Android NDK instalado (ANDROID_NDK_HOME ou ANDROID_HOME/ndk/<ver>)
#    - CMake 3.22.1+, Ninja
#    - RunTime/ em: /media/windroid/SSD KING/PROJETO NFSMW RECOMP/RunTime
#      (ajuste RUNTIME_DIR abaixo se necessário)
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── Caminhos configuráveis ────────────────────────────────────────────────────
# Diretório do repositório RunTime (fonte do runtime)
RUNTIME_DIR="${RUNTIME_DIR:-$(dirname "$SCRIPT_DIR")/RunTime}"

# Preset CMake do RunTime (conforme CMakePresets.json)
CMAKE_PRESET="${CMAKE_PRESET:-android-arm64}"

# Resultado do cmake --install (deve bater com CMAKE_INSTALL_PREFIX do preset)
INSTALL_DIR="${RUNTIME_DIR}/out/install/${CMAKE_PRESET}"

# Destino no NFSMW-RECOMP
SDK_DEST="${SCRIPT_DIR}/android/rexglue-sdk"

# NDK — descobre automaticamente se não definido
find_ndk() {
    if [ -n "${ANDROID_NDK_HOME:-}" ] && [ -d "$ANDROID_NDK_HOME" ]; then
        echo "$ANDROID_NDK_HOME"
        return
    fi
    if [ -n "${ANDROID_HOME:-}" ]; then
        local ndk_base="${ANDROID_HOME}/ndk"
        if [ -d "$ndk_base" ]; then
            # Prioriza a versão exata usada pelo APK (27.2.12479018) para compatibilidade de ABI/libc++
            if [ -d "${ndk_base}/27.2.12479018" ]; then
                echo "${ndk_base}/27.2.12479018"
                return
            fi
            # Caso contrário, pega a versão mais recente disponível
            local found
            found=$(ls -v "$ndk_base" 2>/dev/null | tail -1)
            if [ -n "$found" ]; then
                echo "${ndk_base}/${found}"
                return
            fi
        fi
    fi
    echo ""
}

# ── Parse de argumentos ───────────────────────────────────────────────────────
DO_BUILD=true
BUILD_TYPE="Release"

for arg in "$@"; do
    case "$arg" in
        --no-build)     DO_BUILD=false ;;
        --debug)        BUILD_TYPE="Debug" ;;
        --release)      BUILD_TYPE="Release" ;;
        --help|-h)
            sed -n '2,20p' "$0" | sed 's/^# \{0,2\}//'
            exit 0
            ;;
        *)
            echo "Argumento desconhecido: $arg  (use --help)"
            exit 1
            ;;
    esac
done

# ── Banner ────────────────────────────────────────────────────────────────────
echo "============================================================"
echo "  NFSMW Recompiled — Sincronização de SDK Local"
echo "============================================================"
echo "  RunTime dir : $RUNTIME_DIR"
echo "  Preset      : $CMAKE_PRESET ($BUILD_TYPE)"
echo "  Install dir : $INSTALL_DIR"
echo "  SDK destino : $SDK_DEST"
echo "============================================================"

# ── Verifica existência do RunTime ────────────────────────────────────────────
if [ ! -d "$RUNTIME_DIR" ]; then
    echo "[-] Erro: RunTime não encontrado em: $RUNTIME_DIR"
    echo "    Defina RUNTIME_DIR=/caminho/para/RunTime e tente novamente."
    exit 1
fi

# ── Etapa 1: Compilar o Runtime (opcional) ────────────────────────────────────
if [ "$DO_BUILD" = true ]; then
    echo ""
    echo "[1/3] Compilando RunTime ($BUILD_TYPE) para ${CMAKE_PRESET}..."

    NDK_PATH="$(find_ndk)"
    if [ -z "$NDK_PATH" ]; then
        echo "[-] Erro: Android NDK não encontrado."
        echo "    Defina ANDROID_NDK_HOME=/caminho/para/ndk e tente novamente."
        exit 1
    fi
    echo "      NDK: $NDK_PATH"

    TOOLCHAIN_FILE="${NDK_PATH}/build/cmake/android.toolchain.cmake"
    if [ ! -f "$TOOLCHAIN_FILE" ]; then
        echo "[-] Erro: toolchain.cmake não encontrado em: $TOOLCHAIN_FILE"
        exit 1
    fi

    # Configura
    cmake --preset "${CMAKE_PRESET}" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -S "$RUNTIME_DIR" \
        -B "${RUNTIME_DIR}/out/build/${CMAKE_PRESET}"

    # Compila e instala
    cmake --build "${RUNTIME_DIR}/out/build/${CMAKE_PRESET}" \
        --config "${BUILD_TYPE}" \
        --target install \
        --parallel "$(nproc)"

    echo "    ✓ RunTime compilado e instalado em: $INSTALL_DIR"
else
    echo "[1/3] Ignorando compilação (--no-build)."
fi

# ── Etapa 2: Valida o resultado da compilação ─────────────────────────────────
echo ""
echo "[2/3] Validando SDK em: $INSTALL_DIR ..."

REQUIRED_FILES=(
    "lib/librexruntime.so"
    "lib/librexgpu-xenos.so"
    "lib/librexgpu-plume.so"
    "lib/cmake/rexglue/rexglueConfig.cmake"
    "include"
)

ALL_OK=true
for f in "${REQUIRED_FILES[@]}"; do
    if [ ! -e "${INSTALL_DIR}/${f}" ]; then
        echo "    [-] Ausente: ${INSTALL_DIR}/${f}"
        ALL_OK=false
    fi
done

if [ "$ALL_OK" = false ]; then
    echo "[-] Erro: SDK incompleto. Compile o RunTime primeiro."
    echo "    (cmake --build ... --target install)"
    exit 1
fi

echo "    ✓ SDK validado — todos os artefatos presentes."

# ── Etapa 3: Sincroniza para android/rexglue-sdk/ ────────────────────────────
echo ""
echo "[3/3] Sincronizando SDK para: $SDK_DEST ..."

rm -rf "$SDK_DEST"
mkdir -p "$SDK_DEST"
cp -r "${INSTALL_DIR}/." "$SDK_DEST/"

# Confirma as .so-chave
echo ""
echo "  Bibliotecas copiadas:"
for so in librexruntime.so librexgpu-xenos.so librexgpu-plume.so; do
    src="${INSTALL_DIR}/lib/${so}"
    dst="${SDK_DEST}/lib/${so}"
    if [ -f "$dst" ]; then
        size_kb=$(du -k "$dst" | cut -f1)
        sha=$(sha256sum "$dst" | cut -c1-16)
        echo "    ✓ ${so}  (${size_kb} KB  sha256: ${sha}...)"
    else
        echo "    [-] AUSENTE: ${so}"
    fi
done

echo ""
echo "============================================================"
echo "  ✓ SDK sincronizado com sucesso!"
echo ""
echo "  Próximo passo — compilar o APK:"
echo "    ./build_apk.sh debug"
echo "    ./build_apk.sh release"
echo ""
echo "  O Gradle (syncRexGlueSoLibs) copiará automaticamente"
echo "  as .so para jniLibs/ antes de empacotar o APK."
echo "============================================================"
