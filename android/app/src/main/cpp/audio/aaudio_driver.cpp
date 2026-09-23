#include "aaudio_driver.h"
#include <rex/system/xmemory.h>
#include <android/log.h>
#include <cstring>
#include <cmath>
#include <algorithm>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

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
  ring_buffer_.resize(kRingBufferSampleCapacity, 0.0f);
  LOGI("AndroidAAudioDriver constructed (Lock-Free SPSC, Capacity=%zu)", kRingBufferSampleCapacity);
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
  AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
  AAudioStreamBuilder_setDataCallback(builder, AudioCallback, this);
  AAudioStreamBuilder_setErrorCallback(builder, ErrorCallback, this);

  result = AAudioStreamBuilder_openStream(builder, &stream_);
  if (result != AAUDIO_OK || !stream_) {
    // Fallback to SHARED mode
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    result = AAudioStreamBuilder_openStream(builder, &stream_);
  }
  AAudioStreamBuilder_delete(builder);

  if (result == AAUDIO_OK && stream_) {
    int32_t burst = AAudioStream_getFramesPerBurst(stream_);
    if (burst > 0) {
      AAudioStream_setBufferSizeInFrames(stream_, burst * 2);
    }
  }

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

  if (semaphore_) {
    semaphore_->Release(16, nullptr);
  }

  if (stream_) {
    AAudioStream_requestStop(stream_);
    AAudioStream_close(stream_);
    stream_ = nullptr;
  }
}

#if !defined(__aarch64__)
static inline float LoadBEFloat(uint32_t raw_be) {
  uint32_t le = __builtin_bswap32(raw_be);
  float f;
  std::memcpy(&f, &le, sizeof(float));
  return f;
}
#endif

void AndroidAAudioDriver::SubmitFrame(uint32_t samples_ptr) {
  if (!is_running_.load(std::memory_order_relaxed)) {
    return;
  }

  uint8_t* frame_data = nullptr;
  if (memory_) {
    frame_data = memory_->TranslateVirtual<uint8_t*>(samples_ptr);
    if (!frame_data) {
      frame_data = TranslatePhysical(samples_ptr);
    }
  }

  if (!frame_data) {
    return;
  }

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

#if defined(__aarch64__)
  const float32x4_t v_c_gain = vdupq_n_f32(kCenterGain);
  const float32x4_t v_sur_gain = vdupq_n_f32(kSurroundGain);
  const float32x4_t v_lfe_gain = vdupq_n_f32(kLfeGain);
  const float32x4_t v_min = vdupq_n_f32(-1.0f);
  const float32x4_t v_max = vdupq_n_f32(1.0f);

  for (size_t i = 0; i < kSamplesPerChannel; i += 4) {
    // Reverse Big-Endian byte order to Host Little-Endian for 4 floats per channel
    uint32x4_t u_fl  = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(vld1q_u32(ch_fl + i))));
    uint32x4_t u_fr  = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(vld1q_u32(ch_fr + i))));
    uint32x4_t u_c   = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(vld1q_u32(ch_c + i))));
    uint32x4_t u_lfe = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(vld1q_u32(ch_lfe + i))));
    uint32x4_t u_bl  = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(vld1q_u32(ch_bl + i))));
    uint32x4_t u_br  = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(vld1q_u32(ch_br + i))));

    float32x4_t fl  = vreinterpretq_f32_u32(u_fl);
    float32x4_t fr  = vreinterpretq_f32_u32(u_fr);
    float32x4_t c   = vreinterpretq_f32_u32(u_c);
    float32x4_t lfe = vreinterpretq_f32_u32(u_lfe);
    float32x4_t bl  = vreinterpretq_f32_u32(u_bl);
    float32x4_t br  = vreinterpretq_f32_u32(u_br);

    float32x4_t left = fl;
    left = vfmaq_f32(left, c, v_c_gain);
    left = vfmaq_f32(left, bl, v_sur_gain);
    left = vfmaq_f32(left, lfe, v_lfe_gain);
    left = vminq_f32(vmaxq_f32(left, v_min), v_max);

    float32x4_t right = fr;
    right = vfmaq_f32(right, c, v_c_gain);
    right = vfmaq_f32(right, br, v_sur_gain);
    right = vfmaq_f32(right, lfe, v_lfe_gain);
    right = vminq_f32(vmaxq_f32(right, v_min), v_max);

    float32x4x2_t stereo = vzipq_f32(left, right);
    vst1q_f32(stereo_temp + (i * 2) + 0, stereo.val[0]);
    vst1q_f32(stereo_temp + (i * 2) + 4, stereo.val[1]);
  }
#else
  for (size_t i = 0; i < kSamplesPerChannel; ++i) {
    const float fl  = LoadBEFloat(ch_fl[i]);
    const float fr  = LoadBEFloat(ch_fr[i]);
    const float c   = LoadBEFloat(ch_c[i]);
    const float lfe = LoadBEFloat(ch_lfe[i]);
    const float bl  = LoadBEFloat(ch_bl[i]);
    const float br  = LoadBEFloat(ch_br[i]);

    float left  = fl + (c * kCenterGain) + (bl * kSurroundGain) + (lfe * kLfeGain);
    float right = fr + (c * kCenterGain) + (br * kSurroundGain) + (lfe * kLfeGain);

    stereo_temp[i * 2 + 0] = std::clamp(left, -1.0f, 1.0f);
    stereo_temp[i * 2 + 1] = std::clamp(right, -1.0f, 1.0f);
  }
#endif

  // Lock-Free SPSC Push to Ring Buffer (Zero Mutex Contentions)
  size_t w = write_pos_.load(std::memory_order_relaxed);
  size_t r = read_pos_.load(std::memory_order_acquire);

  constexpr size_t kTotalSamples = kSamplesPerChannel * kOutputChannels; // 512
  constexpr size_t kCapacity = kRingBufferSampleCapacity;                // 16384
  constexpr size_t kMask = kCapacity - 1;

  if (w - r + kTotalSamples > kCapacity) {
    read_pos_.store(w + kTotalSamples - kCapacity, std::memory_order_release);
  }

  for (size_t i = 0; i < kTotalSamples; ++i) {
    ring_buffer_[(w + i) & kMask] = stereo_temp[i];
  }
  write_pos_.store(w + kTotalSamples, std::memory_order_release);
}

aaudio_data_callback_result_t AndroidAAudioDriver::AudioCallback(
    AAudioStream* stream,
    void* userData,
    void* audioData,
    int32_t numFrames) {
  auto* self = static_cast<AndroidAAudioDriver*>(userData);
  float* out = static_cast<float*>(audioData);

  size_t r = self->read_pos_.load(std::memory_order_relaxed);
  size_t w = self->write_pos_.load(std::memory_order_acquire);

  size_t available_samples = (w > r) ? (w - r) : 0;
  size_t samples_to_read = static_cast<size_t>(numFrames) * kOutputChannels;
  size_t actual_samples = std::min(available_samples, samples_to_read);

  constexpr size_t kMask = kRingBufferSampleCapacity - 1;
  for (size_t i = 0; i < actual_samples; ++i) {
    out[i] = self->ring_buffer_[(r + i) & kMask];
  }
  if (actual_samples < samples_to_read) {
    std::memset(out + actual_samples, 0, (samples_to_read - actual_samples) * sizeof(float));
  }

  self->read_pos_.store(r + actual_samples, std::memory_order_release);

  size_t frames_read = actual_samples / kOutputChannels;
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

// =============================================================================
//  AndroidAAudioSystem implementation
// =============================================================================

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
  assert_not_null(out_driver);
  auto driver = std::make_unique<AndroidAAudioDriver>(memory_, semaphore);
  if (!driver->Initialize()) {
    LOGE("Failed to initialize AndroidAAudioDriver for client %zu", index);
    return X_STATUS_UNSUCCESSFUL;
  }

  *out_driver = driver.release();
  return X_STATUS_SUCCESS;
}

void AndroidAAudioSystem::DestroyDriver(AudioDriver* driver) {
  assert_not_null(driver);
  auto* aaudio_driver = static_cast<AndroidAAudioDriver*>(driver);
  aaudio_driver->Shutdown();
  delete aaudio_driver;
}

}  // namespace rex::audio::android
