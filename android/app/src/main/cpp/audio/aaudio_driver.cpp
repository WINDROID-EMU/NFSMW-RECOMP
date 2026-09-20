#include "aaudio_driver.h"
#include <rex/system/xmemory.h>
#include <android/log.h>
#include <cstring>
#include <cmath>
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

  // Wake up any threads waiting on audio semaphore
  if (semaphore_) {
    semaphore_->Release(16, nullptr);
  }

  if (stream_) {
    AAudioStream_requestStop(stream_);
    AAudioStream_close(stream_);
    stream_ = nullptr;
  }
}

static inline float LoadBEFloat(uint32_t raw_be) {
  uint32_t le = __builtin_bswap32(raw_be);
  float f;
  std::memcpy(&f, &le, sizeof(float));
  return f;
}

void AndroidAAudioDriver::SubmitFrame(uint32_t samples_ptr) {
  if (!is_running_.load(std::memory_order_relaxed)) {
    return;
  }

  // Xbox 360 audio frame buffer is allocated in guest virtual memory (heap).
  uint8_t* frame_data = nullptr;
  if (memory_) {
    frame_data = memory_->TranslateVirtual<uint8_t*>(samples_ptr);
    if (!frame_data) {
      frame_data = TranslatePhysical(samples_ptr);
    }
  }

  if (!frame_data) {
    LOGW("SubmitFrame: null frame_data for samples_ptr=0x%08X", samples_ptr);
    return;
  }

  // Xbox 360 audio frame is PLANAR, 6 channels of 256 samples each (6144 bytes).
  // Channel layout: 0=FL, 1=FR, 2=C, 3=LFE, 4=BL, 5=BR
  // All float samples are stored in Big-Endian byte order.
  constexpr size_t kChannelStride = 256;
  const uint32_t* raw_src = reinterpret_cast<const uint32_t*>(frame_data);

  const uint32_t* ch_fl  = raw_src + 0 * kChannelStride;
  const uint32_t* ch_fr  = raw_src + 1 * kChannelStride;
  const uint32_t* ch_c   = raw_src + 2 * kChannelStride;
  const uint32_t* ch_lfe = raw_src + 3 * kChannelStride;
  const uint32_t* ch_bl  = raw_src + 4 * kChannelStride;
  const uint32_t* ch_br  = raw_src + 5 * kChannelStride;

  constexpr float kCenterGain = 0.7071f;
  constexpr float kSurroundGain = 0.7071f;
  constexpr float kLfeGain = 0.5f;

  float stereo_temp[kSamplesPerChannel * kOutputChannels];
  float max_val = 0.0f;

  for (size_t i = 0; i < kSamplesPerChannel; ++i) {
    const float fl  = LoadBEFloat(ch_fl[i]);
    const float fr  = LoadBEFloat(ch_fr[i]);
    const float c   = LoadBEFloat(ch_c[i]);
    const float lfe = LoadBEFloat(ch_lfe[i]);
    const float bl  = LoadBEFloat(ch_bl[i]);
    const float br  = LoadBEFloat(ch_br[i]);

    float left  = fl + (c * kCenterGain) + (bl * kSurroundGain) + (lfe * kLfeGain);
    float right = fr + (c * kCenterGain) + (br * kSurroundGain) + (lfe * kLfeGain);

    left  = std::clamp(left, -1.0f, 1.0f);
    right = std::clamp(right, -1.0f, 1.0f);

    stereo_temp[i * 2 + 0] = left;
    stereo_temp[i * 2 + 1] = right;

    max_val = std::max(max_val, std::max(std::abs(left), std::abs(right)));
  }

  // Push into ring buffer
  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    for (size_t i = 0; i < kSamplesPerChannel * kOutputChannels; ++i) {
      size_t next_write = (write_pos_ + 1) % ring_buffer_.size();
      if (next_write == read_pos_) {
        // Buffer full: drop oldest 2 samples (1 stereo frame) to keep latency low
        read_pos_ = (read_pos_ + 2) % ring_buffer_.size();
      }
      ring_buffer_[write_pos_] = stereo_temp[i];
      write_pos_ = next_write;
    }
  }

  static int s_frame_cnt = 0;
  if (++s_frame_cnt % 300 == 1 || (max_val > 0.05f && s_frame_cnt % 60 == 1)) {
    LOGI("SubmitFrame #%d: max_sample=%.4f (L=%.3f, R=%.3f)",
         s_frame_cnt, max_val, stereo_temp[0], stereo_temp[1]);
  }

  // Flow control: Semaphore is NOT released here!
  // It is released in AudioCallback as frames are actually consumed by the audio hardware.
}

aaudio_data_callback_result_t AndroidAAudioDriver::AudioCallback(
    AAudioStream* stream,
    void* userData,
    void* audioData,
    int32_t numFrames) {
  auto* self = static_cast<AndroidAAudioDriver*>(userData);
  float* out = static_cast<float*>(audioData);

  size_t frames_read = 0;
  {
    std::lock_guard<std::mutex> lock(self->buffer_mutex_);
    for (int32_t f = 0; f < numFrames; ++f) {
      if (self->read_pos_ != self->write_pos_) {
        out[f * 2 + 0] = self->ring_buffer_[self->read_pos_];
        self->read_pos_ = (self->read_pos_ + 1) % self->ring_buffer_.size();
        out[f * 2 + 1] = self->ring_buffer_[self->read_pos_];
        self->read_pos_ = (self->read_pos_ + 1) % self->ring_buffer_.size();
        frames_read++;
      } else {
        out[f * 2 + 0] = 0.0f;
        out[f * 2 + 1] = 0.0f;
      }
    }
  }

  // Flow control: Release 1 permit on semaphore for every 256 frames consumed by the audio DAC.
  // This throttles the guest audio worker thread to exactly 48kHz (187.5 frames/second).
  if (self->semaphore_ && frames_read > 0) {
    self->consumed_frames_ += frames_read;
    int release_count = static_cast<int>(self->consumed_frames_ / kSamplesPerChannel);
    if (release_count > 0) {
      self->consumed_frames_ %= kSamplesPerChannel;
      self->semaphore_->Release(release_count, nullptr);
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
