#pragma once
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <cstdint>

#include <rex/assert.h>

namespace rex {
namespace ui {

// Represents a surface that presenting can be performed to.
// Surface methods can be called only from the UI thread.

class Surface {
 public:
  enum TypeIndex {
    kTypeIndex_AndroidNativeWindow = 0,
  };
  using TypeFlags = uint32_t;
  enum : TypeFlags {
    kTypeFlag_AndroidNativeWindow = TypeFlags(1) << kTypeIndex_AndroidNativeWindow,
  };

  Surface(const Surface& surface) = delete;
  Surface& operator=(const Surface& surface) = delete;
  virtual ~Surface() = default;

  virtual TypeIndex GetType() const = 0;

  // Returns the up-to-date size (and true), or zeros (and false) if not ready
  // to open a presentation connection yet. The size preferably should be
  // exactly the dimensions of the surface in physical pixels of the display
  // (without stretching performed by the platform's composition), but even if
  // stretching happens, it is required that surface pixels have 1:1 aspect
  // ratio relatively to the physical display.
  bool GetSize(uint32_t& width_out, uint32_t& height_out) {
    // If any dimension is 0 (like, resized completely to zero in one direction,
    // but not in another), the surface is zero-area - don't try to present to
    // it.
    uint32_t width, height;
    if (!GetSizeImpl(width, height) || !width || !height) {
      width_out = 0;
      height_out = 0;
      return false;
    }
    width_out = width;
    height_out = height;
    return true;
  }

 protected:
  Surface() = default;

  // Returns the up-to-date size in physical pixels (and true), or zeros (and
  // optionally false) if not ready to open a presentation connection yet.
  virtual bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const = 0;
};

}  // namespace ui
}  // namespace rex
