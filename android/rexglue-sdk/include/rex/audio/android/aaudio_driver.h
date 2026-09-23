/**
 * @file        audio/android/aaudio_driver.h
 * @brief       Direct NDK AAudio driver with low-latency / MMAP hardware audio path
 *
 * @copyright   Copyright (c) 2026 ReXGlue Authors
 * @license     BSD 3-Clause License
 */

#pragma once

#include <mutex>
#include <queue>
#include <stack>

#include <aaudio/AAudio.h>

#include <rex/audio/audio_driver.h>
#include <rex/thread.h>

namespace rex::audio::android {

class AAudioDriver : public AudioDriver {
 public:
  AAudioDriver(memory::Memory* memory, rex::thread::Semaphore* semaphore);
  ~AAudioDriver() override;

  bool Initialize();
  void SubmitFrame(uint32_t frame_ptr) override;
  void Shutdown();

 protected:
  static aaudio_data_callback_result_t DataCallback(AAudioStream* stream, void* user_data,
                                                    void* audio_data, int32_t num_frames);
  static void ErrorCallback(AAudioStream* stream, void* user_data, aaudio_result_t error);

  rex::thread::Semaphore* semaphore_ = nullptr;
  AAudioStream* aaudio_stream_ = nullptr;
  int32_t channel_count_ = 2;

  static constexpr uint32_t kFrameFrequency = 48000;
  static constexpr uint32_t kGuestChannels = 6;
  static constexpr uint32_t kChannelSamples = 256;
  static constexpr uint32_t kGuestFrameSamples = kGuestChannels * kChannelSamples;
  static constexpr uint32_t kGuestFrameSize = sizeof(float) * kGuestFrameSamples;
  static constexpr size_t kRingBufferCapacity = 64;

  struct alignas(64) AudioRingFrame {
    float samples[kGuestFrameSamples];
  };

  std::unique_ptr<AudioRingFrame[]> ring_buffer_;
  alignas(64) std::atomic<size_t> write_index_{0};
  alignas(64) std::atomic<size_t> read_index_{0};

  // Intermediate buffer for leftover frames when callback frame count != kChannelSamples
  std::vector<float> leftover_buffer_ = {};
  size_t leftover_offset_ = 0;
};

}  // namespace rex::audio::android
