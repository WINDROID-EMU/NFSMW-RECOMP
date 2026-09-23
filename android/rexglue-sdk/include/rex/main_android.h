/**
 * @file        rex/main_android.h
 * @brief       Android platform initialization and device level helpers
 *
 * @copyright   Copyright (c) 2026 ReXGlue Authors
 * @license     BSD 3-Clause License
 */

#pragma once

#include <android/api-level.h>

namespace rex {

inline int GetAndroidApiLevel() {
  return android_get_device_api_level();
}

}  // namespace rex
