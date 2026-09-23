/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#pragma once

#include <algorithm>
#include <cstdint>

#include <rex/assert.h>
#include <rex/audio/downmix.h>
#include <rex/platform.h>
#include <rex/types.h>

namespace rex::audio::conversion {

inline void sequential_6_BE_to_interleaved_6_LE(float* output, const float* input,
                                                size_t ch_sample_count, const SurroundMix& mix,
                                                float gain) {
  const float w[6] = {
      gain, gain, mix.center * gain, mix.lfe * gain, mix.surround * gain, mix.surround * gain};
  for (size_t sample = 0; sample < ch_sample_count; sample++) {
    for (size_t channel = 0; channel < 6; channel++) {
      const float v = rex::byte_swap(input[channel * ch_sample_count + sample]) * w[channel];
      output[sample * 6 + channel] = std::clamp(v, -1.0f, 1.0f);
    }
  }
}

inline void sequential_6_BE_to_interleaved_2_LE(float* output, const float* input,
                                                size_t ch_sample_count, const StereoFold& fold,
                                                float gain) {
  // Default 5.1 channel mapping is fl, fr, fc, lf, bl, br
  const float scale = fold.scale * gain;
  for (size_t sample = 0; sample < ch_sample_count; sample++) {
    const float fl = rex::byte_swap(input[0 * ch_sample_count + sample]);
    const float fr = rex::byte_swap(input[1 * ch_sample_count + sample]);
    const float fc = rex::byte_swap(input[2 * ch_sample_count + sample]);
    const float lf = rex::byte_swap(input[3 * ch_sample_count + sample]);
    const float bl = rex::byte_swap(input[4 * ch_sample_count + sample]);
    const float br = rex::byte_swap(input[5 * ch_sample_count + sample]);
    // Center and LFE land on both sides.
    const float mid = fc * fold.center + lf * fold.lfe;
    output[sample * 2] = std::clamp((fl + mid + bl * fold.surround) * scale, -1.0f, 1.0f);
    output[sample * 2 + 1] = std::clamp((fr + mid + br * fold.surround) * scale, -1.0f, 1.0f);
  }
}

}  // namespace rex::audio::conversion
