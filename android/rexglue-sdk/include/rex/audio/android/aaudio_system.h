/**
 * @file        audio/android/aaudio_system.h
 * @brief       AAudio implementation of the AudioSystem abstraction for Android
 *
 * @copyright   Copyright (c) 2026 ReXGlue Authors
 * @license     BSD 3-Clause License
 */

#pragma once

#include <rex/audio/audio_system.h>

namespace rex::audio::android {

class AAudioSystem : public AudioSystem {
 public:
  explicit AAudioSystem(runtime::FunctionDispatcher* function_dispatcher);
  ~AAudioSystem() override;

  static bool IsAvailable() { return true; }

  static std::unique_ptr<AudioSystem> Create(runtime::FunctionDispatcher* function_dispatcher);

  X_STATUS CreateDriver(size_t index, rex::thread::Semaphore* semaphore,
                        AudioDriver** out_driver) override;
  void DestroyDriver(AudioDriver* driver) override;

 protected:
  void Initialize() override;
};

}  // namespace rex::audio::android
