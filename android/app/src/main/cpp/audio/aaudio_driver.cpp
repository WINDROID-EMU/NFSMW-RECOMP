#include "aaudio_driver.h"
#include <android/log.h>
#include <cstring>
#include <algorithm>

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
  ring_buffer_.resize(kRingBufferCapacityFrames * kOutputChannels, 0.0f);
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

  is_running_.store(true, std::memory_order_release);
  LOGI("AAudio stream started successfully: 48kHz Stereo Float, BufferSize=%d",
       AAudioStream_getBufferSizeInFrames(stream_));
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
  if (!is_running_.load(std::memory_order_relaxed)) {
    if (semaphore_) semaphore_->Release(1, nullptr);
    return;
  }

  uint8_t* frame_data = TranslatePhysical(samples_ptr);
  if (!frame_data) {
    if (semaphore_) semaphore_->Release(1, nullptr);
    return;
  }

  const float* src = reinterpret_cast<const float*>(frame_data);

  // Xbox 360 5.1 layout: 0=FL, 1=FR, 2=C, 3=LFE, 4=BL, 5=BR
  constexpr float kCenterGain = 0.7071f;
  constexpr float kSurroundGain = 0.7071f;
  constexpr float kLfeGain = 0.5f;

  float stereo_temp[kSamplesPerChannel * 2];

  for (size_t i = 0; i < kSamplesPerChannel; ++i) {
    const float fl  = src[i * kGuestChannels + 0];
    const float fr  = src[i * kGuestChannels + 1];
    const float c   = src[i * kGuestChannels + 2];
    const float lfe = src[i * kGuestChannels + 3];
    const float bl  = src[i * kGuestChannels + 4];
    const float br  = src[i * kGuestChannels + 5];

    stereo_temp[i * 2 + 0] = fl + (c * kCenterGain) + (bl * kSurroundGain) + (lfe * kLfeGain);
    stereo_temp[i * 2 + 1] = fr + (c * kCenterGain) + (br * kSurroundGain) + (lfe * kLfeGain);
  }

  // Push into ring buffer
  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    for (size_t i = 0; i < kSamplesPerChannel * kOutputChannels; ++i) {
      ring_buffer_[write_pos_] = stereo_temp[i];
      write_pos_ = (write_pos_ + 1) % ring_buffer_.size();
    }
  }

  if (semaphore_) {
    semaphore_->Release(1, nullptr);
  }
}

aaudio_data_callback_result_t AndroidAAudioDriver::AudioCallback(
    AAudioStream* stream,
    void* userData,
    void* audioData,
    int32_t numFrames) {
  auto* self = static_cast<AndroidAAudioDriver*>(userData);
  float* out = static_cast<float*>(audioData);
  size_t samples_needed = static_cast<size_t>(numFrames * kOutputChannels);

  std::lock_guard<std::mutex> lock(self->buffer_mutex_);
  for (size_t i = 0; i < samples_needed; ++i) {
    if (self->read_pos_ != self->write_pos_) {
      out[i] = self->ring_buffer_[self->read_pos_];
      self->read_pos_ = (self->read_pos_ + 1) % self->ring_buffer_.size();
    } else {
      out[i] = 0.0f; // Silence if underrun
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
