#!/usr/bin/env bash
# ==============================================================================
# Script de Compilação e Geração do APK - NFSMW Android
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ANDROID_DIR="${SCRIPT_DIR}/android"
BUILD_TYPE="debug"
DO_CLEAN=false
DO_INSTALL=false

show_help() {
    echo "Uso: $0 [opções] [debug|release]"
    echo ""
    echo "Opções:"
    echo "  debug        Compilar APK em modo Debug (padrão)"
    echo "  release      Compilar APK em modo Release"
    echo "  --clean      Executar './gradlew clean' antes de compilar"
    echo "  --install    Instalar automaticamente no dispositivo via adb após o build"
    echo "  --help, -h   Mostrar esta mensagem de ajuda"
    echo ""
    echo "Exemplos:"
    echo "  $0 debug"
    echo "  $0 release --clean"
    echo "  $0 debug --install"
}

# Parse de argumentos
for arg in "$@"; do
    case "$arg" in
        debug)
            BUILD_TYPE="debug"
            ;;
        release)
            BUILD_TYPE="release"
            ;;
        --clean)
            DO_CLEAN=true
            ;;
        --install)
            DO_INSTALL=true
            ;;
        --help|-h)
            show_help
            exit 0
            ;;
        *)
            echo "Argumento desconhecido: $arg"
            show_help
            exit 1
            ;;
    esac
done

echo "=========================================================="
echo "  NFSMW Recompiled - Build APK Android ($BUILD_TYPE)"
echo "=========================================================="

# 1. Checagem do diretório Android
if [ ! -d "$ANDROID_DIR" ]; then
    echo "[-] Erro: Diretório android não encontrado em: $ANDROID_DIR"
    exit 1
fi

# 2. Localização do Android SDK
if [ -z "$ANDROID_HOME" ] && [ -z "$ANDROID_SDK_ROOT" ]; then
    if [ -f "$ANDROID_DIR/local.properties" ]; then
        SDK_DIR_LINE=$(grep "^sdk.dir=" "$ANDROID_DIR/local.properties" | cut -d'=' -f2)
        if [ -n "$SDK_DIR_LINE" ] && [ -d "$SDK_DIR_LINE" ]; then
            export ANDROID_HOME="$SDK_DIR_LINE"
            export ANDROID_SDK_ROOT="$SDK_DIR_LINE"
        fi
    fi
    if [ -z "$ANDROID_HOME" ] && [ -d "$HOME/Android/Sdk" ]; then
        export ANDROID_HOME="$HOME/Android/Sdk"
        export ANDROID_SDK_ROOT="$HOME/Android/Sdk"
    fi
fi

if [ -n "$ANDROID_HOME" ]; then
    echo "[+] Android SDK: $ANDROID_HOME"
else
    echo "[!] Aviso: ANDROID_HOME não definido no ambiente (verificando local.properties)"
fi

# 3. Verificação do ReXGlue SDK
if [ -d "$ANDROID_DIR/rexglue-sdk" ]; then
    echo "[+] ReXGlue SDK encontrado em: $ANDROID_DIR/rexglue-sdk"
else
    echo "[!] Aviso: $ANDROID_DIR/rexglue-sdk não encontrado. O CMake pode falhar se REXGLUE_SDK_DIR não estiver configurado."
fi

# 4. Tornar gradlew executável
chmod +x "$ANDROID_DIR/gradlew"

# 5. Executar limpeza se solicitado
cd "$ANDROID_DIR"
if [ "$DO_CLEAN" = true ]; then
    echo "[*] Executando clean..."
    ./gradlew clean
fi

# 6. Compilação
if [ "$BUILD_TYPE" = "release" ]; then
    TASK="assembleRelease"
    OUTPUT_APK="${ANDROID_DIR}/app/build/outputs/apk/release/app-release.apk"
else
    TASK="assembleDebug"
    OUTPUT_APK="${ANDROID_DIR}/app/build/outputs/apk/debug/app-debug.apk"
fi

echo "[*] Compilando APK com tarefa: ./gradlew $TASK ..."
./gradlew "$TASK" --stacktrace

# 7. Verificar se o APK foi gerado com sucesso
if [ -f "$OUTPUT_APK" ]; then
    FILE_SIZE=$(du -h "$OUTPUT_APK" | cut -f1)
    echo ""
    echo "=========================================================="
    echo "  [✓] APK gerado com sucesso!"
    echo "  Caminho: $OUTPUT_APK"
    echo "  Tamanho: $FILE_SIZE"
    echo "=========================================================="
    
    # 8. Instalação opcional via adb
    if [ "$DO_INSTALL" = true ]; then
        echo "[*] Verificando dispositivo conectado via adb..."
        if command -v adb >/dev/null 2>&1; then
            adb install -r "$OUTPUT_APK"
            echo "[✓] APK instalado no dispositivo com sucesso!"
        else
            echo "[-] Erro: Comando 'adb' não encontrado no PATH."
            exit 1
        fi
    fi
else
    echo "[-] Erro: O APK não foi encontrado no caminho esperado: $OUTPUT_APK"
    exit 1
fi
