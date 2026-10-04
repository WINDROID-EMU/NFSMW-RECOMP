#!/usr/bin/env python3
"""
Genera el C++ del juego para Android, con el CLI del SDK de Android.

    python tools/android/generar_codigo.py
    python tools/android/generar_codigo.py --sdk D:\\ruta\\rexglue-sdk-android
    python tools/android/generar_codigo.py --xex D:\\otra\\default.xex --generado app/generated-android/usa

Necesita:
  - el SDK preparado:      python tools/android/preparar_sdk.py
  - tu default.xex en      assets/game_root/default.xex  (EXTRAER_XEX.bat)
  - Clang 18+, CMake 3.25+ y Ninja en el PATH. En Windows, desde un
    "Developer PowerShell for VS" (ver docs/00-entorno.md): es el mismo entorno
    que pide CONSTRUIR.bat.

Deja el resultado en app/generated-android/default. En cuanto existe, Gradle
compila el APK CON el juego en vez de solo la sonda.


LA EDICION DEL JUEGO
====================

Cada default.xex distinto es un programa distinto, y las direcciones del
proyecto (overrides.toml, huecos.toml, los ganchos de C++) son las de la PAL
Espana. Antes de generar se mira que edicion es tu XEX
(tools/ediciones/ediciones.py) y, si no es la de referencia, se traducen esas
direcciones con la tabla de esa edicion. Queda apuntado en
app/generated-android/edicion.json, que es de donde Gradle y CMake saben para
que edicion se compila: el idioma y el pais que se le pasan al juego, y los
ganchos traducidos. Ver docs/ediciones.md.

Un APK vale para UNA edicion: la de la ISO con la que se genero.


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
import json
import os
import pathlib
import platform
import shutil
import subprocess
import sys

RAIZ = pathlib.Path(__file__).resolve().parents[2]
APP = RAIZ / "app"
MANIFIESTO = APP / "nfsmw_manifest_android.toml"
GENERADO = APP / "generated-android"
XEX = RAIZ / "assets" / "game_root" / "default.xex"

sys.path.insert(0, str(RAIZ / "tools" / "ediciones"))
import ediciones  # noqa: E402


def fallar(msg):
    sys.exit(f"[ERROR] {msg}")


def main():
    ap = argparse.ArgumentParser(description="Genera el codigo del juego para Android.")
    ap.add_argument("--sdk", default=str(RAIZ.parent / "rexglue-sdk-android"))
    ap.add_argument("--xex", default=str(XEX),
                    help="el default.xex de tu ISO (por defecto, assets/game_root/default.xex)")
    ap.add_argument("--generado", default=str(GENERADO),
                    help="carpeta del codigo generado (por defecto, app/generated-android). "
                         "Otra sirve para tener dos ediciones a la vez: se le pasa a Gradle "
                         "con -Pnfsmw.generado=<carpeta>")
    ap.add_argument("--rexglue", default=None,
                    help="un CLI rexglue ya compilado DESDE EL SDK DE ANDROID; sin esto se "
                         "compila (o se comprueba que esta al dia) antes de generar")
    args = ap.parse_args()
    sdk = pathlib.Path(args.sdk).resolve()
    xex =pathlib.Path(args.xex).resolve()
    generado = pathlib.Path(args.generado).resolve()
    salida = generado / "default"

    if not (sdk / ".nfsmw-android").is_dir():
        fallar(f"No hay SDK de Android preparado en {sdk}.\n"
               f"        Lanza antes: python tools/android/preparar_sdk.py")
    if not xex.is_file():
        fallar(f"No encuentro {xex}.\n        Sacalo de tu ISO con EXTRAER_XEX.bat.")

    # Lo primero, que edicion es: si no tiene soporte, mejor saberlo antes de
    # compilar el CLI.
    print("== Edicion del juego")
    ficha = ediciones.detectar(str(xex))
    referencia = ficha["tabla"] == ediciones.datos()["referencia"]
    print(f"[ok] {ficha['nombre']} ({ficha['id']}), idioma {ficha['idioma']}, pais {ficha['pais']}")
    if "aviso" in ficha:
        print(f"     {ficha['aviso']}")
    if referencia and xex == XEX and generado == GENERADO:
        # El caso de siempre: los ficheros de app/, tal cual.
        manifiesto = MANIFIESTO
        shutil.rmtree(generado / "edicion", ignore_errors=True)
    else:
        print(f"     direcciones traducidas con tools/ediciones/{ficha['tabla']}/direcciones.tsv")
        manifiesto = ediciones.aplicar(ficha["tabla"], generado / "edicion",
                                       xex=pathlib.Path(os.path.relpath(xex, APP)).as_posix())

    if args.rexglue:
        rexglue = pathlib.Path(args.rexglue).resolve()
        if not rexglue.is_file():
            fallar(f"No encuentro {rexglue}")
    else:
        rexglue = compilar_cli(sdk)
    print(f"[ok] {rexglue}")

    print("\n== Codegen")
    r = subprocess.run([str(rexglue), "codegen", manifiesto.name], cwd=manifiesto.parent)
    if r.returncode != 0:
        fallar("El codegen ha fallado. Mira su salida: con el SDK v0.10.0 puede que "
               "overrides.toml o huecos.toml necesiten algun ajuste respecto al de escritorio.")
    if not (salida / "sources.cmake").is_file():
        fallar(f"El codegen termino pero no hay {salida / 'sources.cmake'}")

    # Para que edicion es este codigo: lo leen Gradle (idioma y pais del juego) y
    # CMake (si hay que traducir los ganchos).
    with open(generado / "edicion.json", "w", encoding="utf-8", newline="\n") as fh:
        json.dump({"id": ficha["id"], "nombre": ficha["nombre"], "tabla": ficha["tabla"],
                   "referencia": referencia, "idioma": ficha["idioma"], "pais": ficha["pais"],
                   "sha256": ficha["sha256"]}, fh, ensure_ascii=False, indent=2)
        fh.write("\n")

    n = sum(1 for _ in salida.glob("*.cpp"))
    print(f"\n[ok] {n} ficheros .cpp en {salida}, edicion {ficha['nombre']}")
    siguiente = "cd android && gradlew assembleRelease"
    if generado != GENERADO:
        siguiente += f" -Pnfsmw.generado={generado}"
    print(f"     Siguiente: {siguiente}")
    return 0


def compilar_cli(sdk):
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
    return candidatos[0]


if __name__ == "__main__":
    sys.exit(main())
