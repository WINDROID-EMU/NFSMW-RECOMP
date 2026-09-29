#!/usr/bin/env python3
"""
Saca en un solo diff los cambios de ESTE proyecto al SDK ReXGlue.

    python tools/android/diff_sdk.py
    python tools/android/diff_sdk.py --sdk D:\\otra\\ruta\\rexglue-sdk-android

Escribe sdk/nfsmw-parches.diff. Es para leer y revisar: lo que se aplica de
verdad son los scripts tools/parche_*.py, en el orden de
tools/android/preparar_sdk.py.


QUE COMPARA
===========

Lo de antes:   ReXGlue v0.10.0 + el parche Android de hells-gate, sin mas.
Lo de despues: el SDK ya preparado (..\\rexglue-sdk-android).

La diferencia son SOLO los parches de este proyecto. El de hells-gate no sale:
esta en los dos lados. Asi el diff no redistribuye nada suyo (su repo no
declara licencia), salvo que una linea suya caiga como contexto de un cambio
nuestro; el script lo cuenta y lo avisa.

Lo de antes se monta en una carpeta temporal con git worktree (el mismo commit
que el SDK preparado) y los parches de hells-gate que preparar_sdk.py dejo en
<sdk>/.nfsmw-android/. El SDK preparado no se toca.
"""

import argparse
import difflib
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

RAIZ = pathlib.Path(__file__).resolve().parents[2]
# Lo que tocan los parches del proyecto. thirdparty/ no: ahi solo hay
# dependencias (y libadrenotools, que se clona entera).
CARPETAS = ["src", "include", "resources", "cmake"]
SUELTOS = ["CMakeLists.txt"]
# Copias de seguridad que dejan algunos parches (el fichero entero de antes):
# no son cambios, y colarian el original completo.
SOBRAS = {".original", ".orig", ".rej", ".bak", ".tmp"}
# Los mismos parches de hells-gate que aplica preparar_sdk.py, con sus exclusiones.
PARCHES_REF = [
    ("rexglue-sdk-v0.10.0-android.patch", []),
    ("rexglue-sdk-v0.10.0-android-perf.patch", ["src/system/mmio_handler.cpp"]),
]


def git(*args, cwd):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout


def ficheros(raiz):
    salida = []
    for carpeta in CARPETAS:
        base = raiz / carpeta
        if base.is_dir():
            salida += [p.relative_to(raiz).as_posix() for p in base.rglob("*")
                       if p.is_file() and p.suffix not in SOBRAS]
    salida += [s for s in SUELTOS if (raiz / s).is_file()]
    return set(salida)


def leer_lineas(ruta):
    if not ruta.is_file():
        return []
    datos = ruta.read_bytes()
    if b"\0" in datos[:8192]:
        return None  # binario
    return datos.decode("utf-8", errors="replace").replace("\r\n", "\n").splitlines(keepends=True)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--sdk", default=str(RAIZ.parent / "rexglue-sdk-android"))
    p.add_argument("--salida", default=str(RAIZ / "sdk" / "nfsmw-parches.diff"))
    args = p.parse_args()

    sdk = pathlib.Path(args.sdk).resolve()
    cache = sdk / ".nfsmw-android"
    if not all((cache / nombre).is_file() for nombre, _ in PARCHES_REF):
        sys.exit(f"[ERROR] No encuentro los parches de hells-gate en {cache}.\n"
                 "        Prepara el SDK antes: python tools/android/preparar_sdk.py")

    temporal = pathlib.Path(tempfile.mkdtemp(prefix="nfsmw-sdk-base-"))
    base = temporal / "base"
    try:
        git("worktree", "add", "--detach", str(base), "HEAD", cwd=sdk)
        for nombre, excluir in PARCHES_REF:
            extra = [f"--exclude={e}" for e in excluir]
            git("apply", "--whitespace=nowarn", *extra, str(cache / nombre), cwd=base)

        # Las lineas que puso hells-gate, para avisar si alguna sale en el diff.
        de_hells_gate = {}
        antes, despues = ficheros(base), ficheros(sdk)
        trozos = []
        sin_contexto = []
        cambiados = 0
        for rel in sorted(antes | despues):
            a = leer_lineas(base / rel)
            b = leer_lineas(sdk / rel)
            if a is None or b is None or a == b:
                continue
            cambiados += 1
            original = subprocess.run(["git", "show", f"HEAD:{rel}"], cwd=sdk,
                                      capture_output=True).stdout
            original = set(original.decode("utf-8", errors="replace").replace("\r\n", "\n")
                           .splitlines(keepends=True))
            de_hells_gate[rel] = {l for l in a if l not in original and l.strip()}
            desde = f"a/{rel}" if a else "/dev/null"
            hasta = f"b/{rel}" if b else "/dev/null"
            trozo = list(difflib.unified_diff(a, b, fromfile=desde, tofile=hasta, n=3))
            # Si el contexto arrastra lineas de hells-gate, ese fichero va sin
            # contexto (se aplica con git apply --unidiff-zero): nada suyo aqui.
            if any(l[:1] in (" ", "-") and not l.startswith("---") and
                   l[1:] in de_hells_gate[rel] for l in trozo):
                trozo = list(difflib.unified_diff(a, b, fromfile=desde, tofile=hasta, n=0))
                sin_contexto.append(rel)
            trozos.append(trozo)

        texto = "".join("".join(t) for t in trozos)
        salida = pathlib.Path(args.salida)
        salida.parent.mkdir(parents=True, exist_ok=True)
        salida.write_text(texto, encoding="utf-8", newline="\n")

        # Cuantas lineas de hells-gate se cuelan como contexto o como borradas.
        coladas = 0
        actual = None
        for linea in texto.splitlines(keepends=True):
            if linea.startswith("+++ b/"):
                actual = linea[6:].rstrip("\n")
            elif actual and linea[:1] in (" ", "-") and not linea.startswith("---"):
                if linea[1:] in de_hells_gate.get(actual, ()):
                    coladas += 1
                    print(f"     de hells-gate en {actual}: {linea.rstrip()}")
        print(f"[ok] {salida.relative_to(RAIZ) if salida.is_relative_to(RAIZ) else salida}: "
              f"{cambiados} ficheros, {len(texto.splitlines())} lineas")
        print(f"     lineas de hells-gate que salen como contexto o borradas: {coladas}")
        if sin_contexto:
            print(f"     sin contexto (git apply --unidiff-zero): {', '.join(sin_contexto)}")
        return 0
    finally:
        subprocess.run(["git", "worktree", "remove", "--force", str(base)], cwd=sdk,
                       capture_output=True)
        subprocess.run(["git", "worktree", "prune"], cwd=sdk, capture_output=True)
        shutil.rmtree(temporal, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
