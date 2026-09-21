# Compilar e Gerar APK para Android

Guia de compilação da versão Android do **Need for Speed: Most Wanted (2005) - Recompiled** para a arquitetura `arm64-v8a`.

---

## 1. Compilação Automatizada via GitHub Actions (Workflow de CI/CD)

O repositório inclui um workflow completo em [`.github/workflows/build-apk.yml`](../.github/workflows/build-apk.yml).

### Gatilhos automáticos
- **Push ou Pull Request**: Cada commit enviado para as branches `main` ou `master` dispara a verificação e compilação do APK.
- **Tags de versão (`v*`)**: Ao criar e enviar uma tag (ex: `git tag v1.0.0 && git push origin v1.0.0`), o workflow compila o APK e cria automaticamente uma **Release no GitHub** com o arquivo `.apk` pronto para download.
- **Disparo Manual (`workflow_dispatch`)**:
  1. Vá até a aba **Actions** no repositório GitHub.
  2. Selecione o workflow **Build Android APK**.
  3. Clique em **Run workflow** e selecione o tipo (`debug` ou `release`).

### O que o workflow executa
1. Configura o ambiente Linux com **Java JDK 17 (Temurin)**.
2. Provisiona o **Android SDK**, **NDK `27.2.12479018`** e **CMake `3.22.1`**.
3. Aplica cache para dependências do Gradle.
4. Compila a biblioteca nativa `libnfsmw.so` e empacota o APK (`assembleDebug` ou `assembleRelease`).
5. Publica o APK nos **Artifacts** da execução (retenção de 30 dias).
6. Cria/atualiza a **Release** no GitHub com o binário `NFSMW-Android-debug.apk` ou `NFSMW-Android-release.apk`.

> [!NOTE]
> Para compilações no GitHub Actions, garanta que as dependências essenciais do CMake (`android/rexglue-sdk/` e o código gerado em `app/generated/`) estejam disponíveis no repositório remoto ou acessíveis durante o build.

---

## 2. Compilação Local no Linux / Android Studio

### Pré-requisitos
- **Android SDK** com Platform `34` e Build Tools instalados.
- **Android NDK** versão `27.2.12479018`.
- **CMake** versão `3.22.1`.
- **Java JDK 17**.

### Usando o script `build_apk.sh`

Na raiz do repositório, execute:

```bash
# Compilar versão Debug (padrão)
./build_apk.sh

# Compilar versão Release
./build_apk.sh release

# Limpar build anterior e recompilar
./build_apk.sh --clean debug

# Compilar e instalar diretamente no smartphone via ADB
./build_apk.sh debug --install
```

Os APKs gerados ficam localizados em:
- **Debug:** `android/app/build/outputs/apk/debug/app-debug.apk`
- **Release:** `android/app/build/outputs/apk/release/app-release.apk`

---

## 3. Estrutura do Módulo Android

- `android/CMakeLists.txt`: Configuração do CMake para compilação da biblioteca C++23 nativa `libnfsmw.so`.
- `android/rexglue-sdk/`: SDK do runtime ReXGlue (headers e bibliotecas para Android).
- `android/app/src/main/cpp/`: Código de integração nativa (janela, áudio AAudio, controles virtuais e gamepad, armazenamento VFS).
- `android/app/src/main/java/`: Telas Java (seletor de ROM / TitleActivity e GameActivity).
