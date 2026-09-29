# NFSMW Recompiled

A static native recompilation of **Need for Speed: Most Wanted (2005)**, Xbox 360,
built on the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

> Reference title ID: `454107D9`

This is **not an emulator**. The PowerPC code inside the game's `default.xex` is
translated ahead of time into C++, then compiled into a native x86-64 binary. There
is no JIT and no instruction interpreter at runtime — the game's own logic runs as
native code. What the SDK provides is everything *around* that: the Xbox 360 kernel
calls, the filesystem, audio, input, and a translation of the Xenos GPU to Direct3D 12.

**You need your own copy of the game.** This repository contains no game data, no
`default.xex`, no generated C++, and no compiled binary — and it never will. See
[Legal](#legal).

## This fork

This is a fork of [madelrandel-blip/NFSMW-Recompiled](https://github.com/madelrandel-blip/NFSMW-Recompiled)
that adds an **Android port** (arm64, Snapdragon, Vulkan) and performance work on the Xenos
GPU path. See [docs/android.md](docs/android.md), [docs/pipeline-nativo.md](docs/pipeline-nativo.md)
and the [CHANGELOG](CHANGELOG.md).

> **This is a vibe-coded project.** The Android port and the changes in this fork were
> written with an AI coding assistant (Claude Code), directed, tested on real hardware and
> accepted by a human. The documentation records what was measured and what was not; trust
> the numbers in it, not the confidence of any comment. Review the code before reusing it.

---

## Status

Playable, with rough edges. Known to run start to finish through the prologue and
into free roam on an Intel Iris 540 (a weak integrated GPU) at roughly 18–38 FPS
at 854×480.

| Area | State |
|---|---|
| Boot, menus, free roam | Working |
| Audio | Working. A decoder deadlock that killed sound and froze the game on returning to the menu is fixed — see [docs/diario/audio-cuelgue.md](docs/diario/audio-cuelgue.md) |
| Graphics (D3D12) | Working. Both EDRAM emulation paths are selectable; the fast one roughly doubles the frame rate on integrated GPUs |
| Graphics (Vulkan) | Compiles and loads, renders black on Intel. Untested elsewhere |
| Controller and keyboard | Working |
| V-sync and frame limiting | Working, via a local patch — neither exists in the stock SDK |
| Internal resolution scaling | Working, up to 4× |
| Save games | Working |
| Multiplayer | **Not working.** The privilege gate is solved; the network layer underneath is not. See [docs/diario/red-y-privilegios.md](docs/diario/red-y-privilegios.md) |
| Android (Snapdragon, Turnip) | **In progress.** Boots from the ISO, reaches the menus and races with a Bluetooth controller, with sound. Main menu (3D) at ~50 fps on a Snapdragon 8 Elite with the Qualcomm driver; Turnip loads but is far slower. See [docs/android.md](docs/android.md) |

## What you need

- Windows 10 or 11, x64
- Visual Studio 2022 Build Tools (MSVC + Windows SDK)
- CMake 3.28+, Ninja, Clang 20+
- Python 3.10+
- A copy of the ReXGlue SDK checked out next to this repository
- Your own ISO or GOD dump of Need for Speed: Most Wanted for Xbox 360

Full setup instructions: [docs/00-entorno.md](docs/00-entorno.md) (Spanish).

## Build

```bat
tools\bootstrap.ps1          :: clones and builds the ReXGlue SDK into ..\rexglue-sdk
EXTRAER_XEX.bat              :: pulls default.xex out of your ISO into assets\
CONSTRUIR.bat                :: patches the SDK, recompiles it, builds the game, packages build\
```

`CONSTRUIR.bat` is the whole pipeline. It applies every patch this project carries,
rebuilds the SDK (that is where the fixes live), runs the code generator, compiles the
game, assembles a portable `build\` folder, verifies that folder is self-contained, and
finally writes two ready-to-send folders into `..\build release\`:

- `NFSMW Windows x64\` — playable, with the game executable inside. Zip it and send it
  to someone who owns the game; **never** publish it (see [Legal](#legal)).
- `NFSMW Windows x64 - Portable\` — everything except the game executable. This one is
  safe to publish.

The ISO is left out of both. The script refuses to finish if anything that looks like
game data ends up in the publishable folder.

Step-by-step detail, including what to do when something fails:
[docs/compilar.md](docs/compilar.md).

## Documentation

The README is in English; the technical documentation is in Spanish, matching the
source comments.

| Document | What it covers |
|---|---|
| [docs/arquitectura.md](docs/arquitectura.md) | How the pieces fit: SDK, app, patches, launcher |
| [docs/compilar.md](docs/compilar.md) | Building from a clean checkout |
| [docs/parches.md](docs/parches.md) | Every patch: what it changes, why, and how it was verified |
| [docs/lanzador.md](docs/lanzador.md) | The launcher, its settings and how it is built |
| [docs/rendimiento.md](docs/rendimiento.md) | Measured findings: EDRAM paths, resolution scaling, frame pacing |
| [docs/android.md](docs/android.md) | The Android port: building the APK, Turnip, and why each piece is the way it is |
| [docs/problemas-conocidos.md](docs/problemas-conocidos.md) | What is broken and how far each one was traced |
| [docs/diario/](docs/diario/) | Long-form write-ups of the harder diagnoses |

The diary is worth reading before touching the audio or graphics code. Each entry
records what the evidence actually said, including the times a plausible theory turned
out to be wrong.

## Layout

```
NFSMW Recompiled/
├── app/                 the game application: CMake, codegen config, app subclass
│   ├── src/             main.cpp and the ReXApp subclass with the game's quirks
│   ├── nfsmw_manifest.toml   what the code generator reads
│   ├── overrides.toml   hand-written codegen fixes, each with its reason
│   └── huecos.toml      generated gap list (774 entries), see HUECOS.bat
├── tools/
│   ├── parche_*.py      the patches, applied to the SDK before building it
│   ├── lanzador/        the launcher (C#, WinForms)
│   └── diagnostico/     instrumentation, not part of a normal build
├── docs/
└── CONSTRUIR.bat        the build
```

### Why the fixes are patches against the SDK

Almost nothing this project fixes lives in the game application. The audio deadlock,
the missing v-sync, the graphics API selector, the Xbox Live privilege gate — all of
them are in ReXGlue, and they end up compiled into `rexruntime.dll`, not into the game
executable. So the build patches the SDK source, rebuilds it, and only then builds the
game.

Every patch is a Python script that applies and reverts by exact text replacement,
block by block. They refuse to touch anything if an anchor does not match exactly once,
they are idempotent, and `--revertir` restores the original. Run any of them with
`--estado` to see what is applied. The reasoning behind that design, and the bugs that
forced it, are in [docs/parches.md](docs/parches.md).

## Contributing

Pull requests are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) first — the
short version is that patches carry their reasoning in the code, and a change that
fixes something should say what evidence says it is fixed.

## Legal

Static recompilation of a game you own, for your own use, sits on the same ground as
emulation: the translated code derives from a binary you bought.

What must **never** be distributed:

- `default.xex` or any other game file
- the C++ the code generator produces from it
- **the compiled game executable** — it contains the game's own code, translated

What is shared here is the *patch*: configuration, hooks, stubs, scripts and
documentation. Never the game.

The launcher's cover art and icon are Electronic Arts' artwork and are **not** in this
repository. The launcher builds and runs without them; see
[docs/lanzador.md](docs/lanzador.md) if you want to supply your own.

This project is licensed under the GNU General Public License v3.0 — see
[LICENSE](LICENSE). The ReXGlue SDK it builds against is BSD 3-Clause and is a separate
work with its own terms.

Need for Speed and Most Wanted are trademarks of Electronic Arts Inc. This project is
not affiliated with, endorsed by, or connected to Electronic Arts in any way.

⚠️ IMPORTANT ROM REQUIREMENT: This project strictly requires the Need for Speed: Most Wanted (2005) [Xbox 360] ROM in its PAL Spain version. PAL UK (English) or NTSC (US) versions are not acceptable (for now).

## Credits

Thanks to everyone whose work this stands on.

**The original project**

- [madelrandel-blip/NFSMW-Recompiled](https://github.com/madelrandel-blip/NFSMW-Recompiled) —
  the recompilation this fork is based on, by its authors and contributors, including the
  Linux ARM64 launcher and AppImage by MaSieS4Fun

**What it is built on**

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) — the runtime this is built on
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) — the static recompilation approach
- [Xenia](https://xenia.jp/) — the kernel and GPU emulation ReXGlue descends from
- The libraries the SDK bundles, each under its own license: FFmpeg (XMA audio), SDL3,
  glslang and SPIRV-Tools, Dear ImGui, spdlog, fmt, xxHash, Vulkan Memory Allocator and
  toml++, among others

**The Android port**

- [hells-gate-recomp-android](https://github.com/deivid22srk/hells-gate-recomp-android) by
  deivid22srk — the Android patch for the ReXGlue SDK (bionic, ASharedMemory, 16 KB pages,
  ANativeWindow, ARM64 memory barriers). It declares no license, so it is **not** included
  here: `tools/android/preparar_sdk.py` downloads it from that repository at build time
- [libadrenotools](https://github.com/bylaws/libadrenotools) by Billy Laws — loading custom
  Turnip drivers on Adreno GPUs
- [Mesa / Turnip](https://docs.mesa3d.org/drivers/freedreno.html) — the open-source Adreno
  Vulkan driver the app can load
- [StevensND/nfsmw-nx](https://github.com/StevensND/nfsmw-nx) — the Nintendo Switch port of this
  same project. The single-pass scene (`android/app/src/main/cpp/render_targets.cpp`) is ported
  from it, and its documentation on the game's renderer was a guide
- [Xenia Canary](https://github.com/xenia-canary/xenia-canary) and
  [Xenia Edge](https://github.com/has207/xenia-edge) — XMA decoder and ring buffer fixes ported
  in `tools/parche_xma_paquetes.py`, `tools/parche_xma_edge.py` and
  `tools/parche_anillo_bloques.py`
- [XenDroid](https://github.com/rfandango/XenDroid) — the reference for audio on Android
- The Skate 3 Android ports ([andrewnakas/skate3-android](https://github.com/andrewnakas/skate3-android),
  [Buku313/Skate3-Mobile](https://github.com/Buku313/Skate3-Mobile)) — the touch controller
  started from one of them
- [Material Components for Android](https://github.com/material-components/material-components-android)
  — the launcher's interface
