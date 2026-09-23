#pragma once

#include <rex/audio/audio_driver.h>
#include <rex/audio/audio_system.h>
#include <rex/thread.h>
#include <aaudio/AAudio.h>

#include <vector>
#include <atomic>
#include <memory>

namespace rex::audio::android {

class AndroidAAudioDriver final : public AudioDriver {
 public:
  AndroidAAudioDriver(memory::Memory* memory, rex::thread::Semaphore* semaphore);
  ~AndroidAAudioDriver() override;

  bool Initialize();
  void Shutdown();

  void SubmitFrame(uint32_t samples_ptr) override;

 private:
  static aaudio_data_callback_result_t AudioCallback(
      AAudioStream* stream,
      void* userData,
      void* audioData,
      int32_t numFrames);

  static void ErrorCallback(
      AAudioStream* stream,
      void* userData,
      aaudio_result_t error);

  rex::thread::Semaphore* semaphore_ = nullptr;
  AAudioStream* stream_ = nullptr;
  std::atomic<bool> is_running_{false};

  // Lock-Free SPSC Ring Buffer for stereo float samples (48kHz)
  static constexpr size_t kRingBufferSampleCapacity = 16384; // 8192 stereo frames (power of 2)
  std::vector<float> ring_buffer_;
  std::atomic<size_t> write_pos_{0};
  std::atomic<size_t> read_pos_{0};
  size_t consumed_frames_ = 0;
};

class AndroidAAudioSystem final : public AudioSystem {
 public:
  explicit AndroidAAudioSystem(runtime::FunctionDispatcher* function_dispatcher);
  ~AndroidAAudioSystem() override;

  static bool IsAvailable() { return true; }
  static std::unique_ptr<AudioSystem> Create(runtime::FunctionDispatcher* function_dispatcher) {
    return std::make_unique<AndroidAAudioSystem>(function_dispatcher);
  }

  X_STATUS CreateDriver(size_t index, rex::thread::Semaphore* semaphore,
                        AudioDriver** out_driver) override;
  void DestroyDriver(AudioDriver* driver) override;

 protected:
  void Initialize() override;
};

}  // namespace rex::audio::android
