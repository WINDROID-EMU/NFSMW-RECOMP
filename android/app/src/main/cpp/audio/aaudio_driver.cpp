#include "aaudio_driver.h"
#include <android/log.h>
#include <cstring>
#include <algorithm>
#include <rex/audio/conversion.h>
#include <rex/audio/downmix.h>

#define TAG "NFS-AAudio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace rex::audio::android {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kGuestChannels = 6;
constexpr uint32_t kSamplesPerChannel = 256;
constexpr uint32_t kOutputChannels = 2; // Stereo

AndroidAAudioDriver::AndroidAAudioDriver(memory::Memory* memory, rex::thread::Semaphore* semaphore)
    : AudioDriver(memory), semaphore_(semaphore) {
  ring_buffer_.assign(kRingBufferCapacityFrames * kOutputChannels, 0.0f);
  LOGI("AndroidAAudioDriver constructed");
}

AndroidAAudioDriver::~AndroidAAudioDriver() {
  Shutdown();
  LOGI("AndroidAAudioDriver destroyed");
}

bool AndroidAAudioDriver::Initialize() {
  AAudioStreamBuilder* builder = nullptr;
  aaudio_result_t result = AAudio_createStreamBuilder(&builder);
  if (result != AAUDIO_OK || !builder) {
    LOGE("Failed to create AAudioStreamBuilder: %s", AAudio_convertResultToText(result));
    return false;
  }

  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setChannelCount(builder, kOutputChannels);
  AAudioStreamBuilder_setSampleRate(builder, kSampleRate);
  AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
  AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
  AAudioStreamBuilder_setDataCallback(builder, AudioCallback, this);
  AAudioStreamBuilder_setErrorCallback(builder, ErrorCallback, this);

  result = AAudioStreamBuilder_openStream(builder, &stream_);
  AAudioStreamBuilder_delete(builder);

  if (result != AAUDIO_OK || !stream_) {
    LOGE("Failed to open AAudioStream: %s", AAudio_convertResultToText(result));
    return false;
  }

  result = AAudioStream_requestStart(stream_);
  if (result != AAUDIO_OK) {
    LOGE("Failed to start AAudioStream: %s", AAudio_convertResultToText(result));
    AAudioStream_close(stream_);
    stream_ = nullptr;
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    read_pos_ = 0;
    write_pos_ = 0;
    available_frames_ = 0;
    consumed_samples_acc_ = 0;
    std::fill(ring_buffer_.begin(), ring_buffer_.end(), 0.0f);
  }

  is_running_.store(true, std::memory_order_release);
  LOGI("AAudio stream started successfully: 48kHz Stereo Float, BufferSize=%d",
       AAudioStream_getBufferSizeInFrames(stream_));

  // Prime the audio system with 2 initial frames to get generation rolling
  if (semaphore_) {
    semaphore_->Release(2, nullptr);
  }

  return true;
}

void AndroidAAudioDriver::Shutdown() {
  if (!is_running_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }

  if (stream_) {
    AAudioStream_requestStop(stream_);
    AAudioStream_close(stream_);
    stream_ = nullptr;
  }
}

void AndroidAAudioDriver::SubmitFrame(uint32_t samples_ptr) {
  static uint32_t submit_count = 0;
  if ((++submit_count % 100) == 1) {
    LOGI("SubmitFrame called: count=%u samples_ptr=0x%08X", submit_count, samples_ptr);
  }

  if (!is_running_.load(std::memory_order_relaxed)) {
    if (semaphore_) semaphore_->Release(1, nullptr);
    return;
  }

  uint8_t* frame_data = TranslatePhysical(samples_ptr);
  if (!frame_data) {
    if (semaphore_) semaphore_->Release(1, nullptr);
    return;
  }

  // Convert guest 5.1 big-endian planar float samples to host stereo interleaved little-endian float samples
  float stereo_temp[kSamplesPerChannel * kOutputChannels];
  rex::audio::conversion::sequential_6_BE_to_interleaved_2_LE(
      stereo_temp,
      reinterpret_cast<const float*>(frame_data),
      kSamplesPerChannel,
      rex::audio::GetStereoFold(),
      rex::audio::GetOutputGain()
  );

  // Push into ring buffer
  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    // If buffer would overflow, drop oldest frame to maintain synchronization
    if (available_frames_ + kSamplesPerChannel > kRingBufferCapacityFrames) {
      read_pos_ = (read_pos_ + kSamplesPerChannel * kOutputChannels) % ring_buffer_.size();
      available_frames_ -= kSamplesPerChannel;
    }

    for (size_t i = 0; i < kSamplesPerChannel * kOutputChannels; ++i) {
      ring_buffer_[write_pos_] = stereo_temp[i];
      write_pos_ = (write_pos_ + 1) % ring_buffer_.size();
    }
    available_frames_ += kSamplesPerChannel;
  }
}

aaudio_data_callback_result_t AndroidAAudioDriver::AudioCallback(
    AAudioStream* stream,
    void* userData,
    void* audioData,
    int32_t numFrames) {
  auto* self = static_cast<AndroidAAudioDriver*>(userData);
  float* out = static_cast<float*>(audioData);
  size_t frames_needed = static_cast<size_t>(numFrames);
  size_t frames_to_read = 0;

  static uint32_t callback_count = 0;
  if ((++callback_count % 100) == 1) {
    LOGI("AudioCallback: count=%u numFrames=%d available_frames=%zu",
         callback_count, numFrames, self->available_frames_);
  }

  {
    std::lock_guard<std::mutex> lock(self->buffer_mutex_);
    frames_to_read = std::min(frames_needed, self->available_frames_);

    for (size_t f = 0; f < frames_to_read; ++f) {
      out[f * kOutputChannels + 0] = self->ring_buffer_[self->read_pos_];
      self->read_pos_ = (self->read_pos_ + 1) % self->ring_buffer_.size();
      out[f * kOutputChannels + 1] = self->ring_buffer_[self->read_pos_];
      self->read_pos_ = (self->read_pos_ + 1) % self->ring_buffer_.size();
    }
    self->available_frames_ -= frames_to_read;

    // Fill underrun with silence
    for (size_t f = frames_to_read; f < frames_needed; ++f) {
      out[f * kOutputChannels + 0] = 0.0f;
      out[f * kOutputChannels + 1] = 0.0f;
    }

    // Every time we consume 256 samples (1 guest frame), release 1 semaphore token
    self->consumed_samples_acc_ += frames_to_read;
    while (self->consumed_samples_acc_ >= kSamplesPerChannel) {
      self->consumed_samples_acc_ -= kSamplesPerChannel;
      if (self->semaphore_) {
        self->semaphore_->Release(1, nullptr);
      }
    }
  }

  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void AndroidAAudioDriver::ErrorCallback(
    AAudioStream* stream,
    void* userData,
    aaudio_result_t error) {
  LOGW("AAudio stream error occurred: %s", AAudio_convertResultToText(error));
  if (error == AAUDIO_ERROR_DISCONNECTED) {
    auto* self = static_cast<AndroidAAudioDriver*>(userData);
    self->Shutdown();
    self->Initialize();
  }
}

// ---------------------------------------------------------------------------
//  AndroidAAudioSystem implementation
// ---------------------------------------------------------------------------

AndroidAAudioSystem::AndroidAAudioSystem(runtime::FunctionDispatcher* function_dispatcher)
    : AudioSystem(function_dispatcher) {
  LOGI("AndroidAAudioSystem created");
}

AndroidAAudioSystem::~AndroidAAudioSystem() {
  LOGI("AndroidAAudioSystem destroyed");
}

void AndroidAAudioSystem::Initialize() {
  AudioSystem::Initialize();
}

X_STATUS AndroidAAudioSystem::CreateDriver(size_t index, rex::thread::Semaphore* semaphore,
                                          AudioDriver** out_driver) {
  auto driver = std::make_unique<AndroidAAudioDriver>(memory_, semaphore);
  if (!driver->Initialize()) {
    LOGE("Failed to initialize AndroidAAudioDriver for client %zu", index);
    return X_STATUS_UNSUCCESSFUL;
  }
  *out_driver = driver.release();
  return X_STATUS_SUCCESS;
}

void AndroidAAudioSystem::DestroyDriver(AudioDriver* driver) {
  delete driver;
}

}  // namespace rex::audio::android
