#!/usr/bin/env python3
"""
Genera el C++ del juego para Android, con el CLI del SDK de Android.

    python tools/android/generar_codigo.py
    python tools/android/generar_codigo.py --sdk D:\\ruta\\rexglue-sdk-android

Necesita:
  - el SDK preparado:      python tools/android/preparar_sdk.py
  - tu default.xex en      assets/game_root/default.xex  (EXTRAER_XEX.bat)
  - Clang 18+, CMake 3.25+ y Ninja en el PATH. En Windows, desde un
    "Developer PowerShell for VS" (ver docs/00-entorno.md): es el mismo entorno
    que pide CONSTRUIR.bat.

Deja el resultado en app/generated-android/default. En cuanto existe, Gradle
compila el APK CON el juego en vez de solo la sonda.


POR QUE NO VALE EL CODIGO DE ESCRITORIO
=======================================

Las plantillas del codegen van incrustadas en el CLI rexglue al compilarlo, y
el parche Android del SDK cambia una (pch_h.inja): barreras de memoria para
los hosts ARM64 y el offset de la memoria fisica decidido en tiempo de
ejecucion, que hace falta con paginas de 16 KB. Asi que hay que compilar el CLI
desde el arbol de Android y generar con ESE. Es un programa del PC (x64), no
de Android: solo produce C++.


EL RESULTADO ES EL JUEGO
========================

app/generated-android/ esta en .gitignore y el APK que sale con el es solo
para tus dispositivos. Ver la seccion Legal del README.
"""

import argparse
import pathlib
import platform
import shutil
import subprocess
import sys

RAIZ = pathlib.Path(__file__).resolve().parents[2]
APP = RAIZ / "app"
MANIFIESTO = APP / "nfsmw_manifest_android.toml"
SALIDA = APP / "generated-android" / "default"


def fallar(msg):
    sys.exit(f"[ERROR] {msg}")


def main():
    ap = argparse.ArgumentParser(description="Genera el codigo del juego para Android.")
    ap.add_argument("--sdk", default=str(RAIZ.parent / "rexglue-sdk-android"))
    args = ap.parse_args()
    sdk = pathlib.Path(args.sdk).resolve()

    if not (sdk / ".nfsmw-android").is_dir():
        fallar(f"No hay SDK de Android preparado en {sdk}.\n"
               f"        Lanza antes: python tools/android/preparar_sdk.py")
    xex = RAIZ / "assets" / "game_root" / "default.xex"
    if not xex.is_file():
        fallar(f"No encuentro {xex}.\n        Sacalo de tu ISO con EXTRAER_XEX.bat.")
    for herramienta in ("cmake", "ninja", "clang++"):
        if not shutil.which(herramienta):
            fallar(f"No encuentro '{herramienta}' en el PATH. En Windows, abre un "
                   f"'Developer PowerShell for VS' con el componente de Clang.")

    sistema = platform.system()
    if sistema == "Windows":
        preset = "win-amd64"
        compilar = ["cmake", "--build", f"out/build/{preset}", "--config", "Release",
                    "--target", "rexglue"]
        exe = "rexglue.exe"
    elif sistema == "Linux":
        preset = "linux-amd64" if platform.machine() in ("x86_64", "AMD64") else "linux-arm64"
        compilar = ["cmake", "--build", f"out/build/{preset}", "--config", "Release",
                    "--target", "rexglue"]
        exe = "rexglue"
    else:
        fallar(f"Sistema no soportado para el codegen: {sistema}")

    print(f"== CLI rexglue del SDK de Android ({preset})")
    r = subprocess.run(["cmake", "--preset", preset], cwd=sdk)
    if r.returncode != 0:
        fallar("La configuracion del SDK para el host ha fallado.")
    r = subprocess.run(compilar, cwd=sdk)
    if r.returncode != 0:
        fallar("No compila el CLI rexglue.")

    candidatos = sorted((sdk / "out").rglob(exe), key=lambda p: p.stat().st_mtime, reverse=True)
    if not candidatos:
        fallar(f"Compilado, pero no encuentro {exe} bajo {sdk / 'out'}")
    rexglue = candidatos[0]
    print(f"[ok] {rexglue}")

    print("\n== Codegen")
    r = subprocess.run([str(rexglue), "codegen", MANIFIESTO.name], cwd=APP)
    if r.returncode != 0:
        fallar("El codegen ha fallado. Mira su salida: con el SDK v0.10.0 puede que "
               "overrides.toml o huecos.toml necesiten algun ajuste respecto al de escritorio.")
    if not (SALIDA / "sources.cmake").is_file():
        fallar(f"El codegen termino pero no hay {SALIDA / 'sources.cmake'}")

    n = sum(1 for _ in SALIDA.glob("*.cpp"))
    print(f"\n[ok] {n} ficheros .cpp en {SALIDA}")
    print("     Siguiente: cd android && gradlew assembleRelease")
    return 0


if __name__ == "__main__":
    sys.exit(main())
