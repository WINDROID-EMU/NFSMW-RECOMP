/**
 * @file        platform/fpscr.h
 * @brief       Platform-specific FPSCR constants and intrinsics
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <cmath>

#include <rex/types.h>

namespace rex::platform {

struct FPSCRPlatform {
  // RMode
  static constexpr size_t RoundShift = 22;
  static constexpr size_t RoundMaskVal = 3 << RoundShift;
  // FZ and FZ16
  static constexpr size_t FlushMask = (1 << 19) | (1 << 24);
  // Nearest, Zero, -Infinity, -Infinity
  static constexpr size_t GuestToHost[] = {0 << RoundShift, 3 << RoundShift, 1 << RoundShift,
                                           2 << RoundShift};
  // Exception enable bits (0 = exception disabled, ARM defaults to 0)
  static constexpr u32 ExceptionMask = (1 << 8) |   // IOE - Invalid Operation
                                       (1 << 9) |   // DZE - Division by Zero
                                       (1 << 10) |  // OFE - Overflow
                                       (1 << 11) |  // UFE - Underflow
                                       (1 << 12) |  // IXE - Inexact
                                       (1 << 15);   // IDE - Input Denormal

  static inline u32 getcsr() noexcept {
    u64 csr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(csr));
    return csr;
  }

  static inline void setcsr(u32 csr) noexcept { __asm__ __volatile__("msr fpcr, %0" : : "r"(csr)); }

  static inline void InitHostExceptions(u32& csr) noexcept {
    csr &= ~ExceptionMask;  // Clear enable bits to disable exceptions
  }
};

}  // namespace rex::platform
