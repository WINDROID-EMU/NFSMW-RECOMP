/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime (Android ARM64)
 */

#pragma once

// Platform definition: ReXGlue ARM64 Android
#define REX_PLATFORM_ANDROID 1
#define REX_PLATFORM_LINUX 1

#define REX_PLATFORM_WIN32 0
#define REX_PLATFORM_MAC 0
#define REX_PLATFORM_GNU_LINUX 0

// Architecture definition: ARM64 (aarch64)
#define REX_ARCH_ARM64 1
#define REX_ARCH_AMD64 0
#define REX_ARCH_PPC 0

// Compiler definition: Clang / LLVM (Android NDK)
#if defined(__clang__)
#define REX_COMPILER_CLANG 1
#define REX_COMPILER_GNUC 0
#elif defined(__GNUC__)
#define REX_COMPILER_CLANG 0
#define REX_COMPILER_GNUC 1
#else
#define REX_COMPILER_CLANG 1
#define REX_COMPILER_GNUC 0
#endif

#define REX_COMPILER_MSVC 0
#define REX_COMPILER_MINGW32 0
#define REX_COMPILER_INTEL 0

#include <bit>
#include <cstdint>

//=============================================================================
// Compiler Polyfills
//=============================================================================

#if !defined(__builtin_debugtrap)
#define __builtin_debugtrap() __builtin_trap()
#endif

#define _REXPACKEDSCOPE(body)    \
  _Pragma("pack(push, 1)") body; \
  _Pragma("pack(pop)");

#define REXPACKEDSTRUCT(name, value) _REXPACKEDSCOPE(struct name value)
#define REXPACKEDSTRUCTANONYMOUS(value) _REXPACKEDSCOPE(struct value)
#define REXPACKEDUNION(name, value) _REXPACKEDSCOPE(union name value)

#define REX_HAS_BUILTIN_STRLEN 1

namespace rex::platform {

inline constexpr char kPathSeparator = '/';

}  // namespace rex::platform
