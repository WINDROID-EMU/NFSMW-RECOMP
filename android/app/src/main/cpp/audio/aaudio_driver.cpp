#include "aaudio_driver.h"

#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include <SDL3/SDL_events.h>
#include <android/log.h>

#if NFSMW_MOTOR_NATIVO
// Con el motor nativo (nfsmw-android) su SDK trae lo que su driver de SDL hace
// ademas de sacar el sonido, y aqui se hace igual:
//   - audio_ganancia_pct, el volumen del juego (GetOutputGain);
//   - un limitador con los dos canales enlazados, sobre el pliegue sin recortar;
//   - callar el juego mientras suena la pista propia de una pelicula
//     (IsGameOutputSuppressed), que si no se oye doble;
//   - el subsistema de audio de SDL iniciado: la pista de las peliculas va por
//     SDL (nfsmw_video_wmv3.cpp) y lo da por hecho.
#include <SDL3/SDL_init.h>

#include <rex/audio/downmix.h>
#include <rex/audio/output_limiter.h>
#include <rex/cvar.h>

REXCVAR_DECLARE(int32_t, audio_ganancia_pct);
#endif

#include <algorithm>
#include <cstring>

#define TAG "NFS-AAudio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace rex::audio::android {

namespace {

constexpr int32_t kSampleRate = 48000;

// Pesos del plegado de 5.1 a estereo.
constexpr float kCenterGain = 0.7071f;
constexpr float kSurroundGain = 0.7071f;
constexpr float kLfeGain = 0.5f;

inline float LoadBEFloat(uint32_t raw_be) {
  const uint32_t le = __builtin_bswap32(raw_be);
  float f;
  std::memcpy(&f, &le, sizeof(float));
  return f;
}

bool SDLCALL AAudioEventWatch(void* userdata, SDL_Event* event) {
  auto* driver = static_cast<AndroidAAudioDriver*>(userdata);
  if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND) {
    driver->Pause();
  } else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
    driver->Resume();
  }
  return true;
}

}  // namespace

AndroidAAudioDriver::AndroidAAudioDriver(memory::Memory* memory, rex::thread::Semaphore* semaphore)
    : AudioDriver(memory), semaphore_(semaphore), anillo_(new float[kCapacidad]()) {}

AndroidAAudioDriver::~AndroidAAudioDriver() {
  Shutdown();
}

bool AndroidAAudioDriver::AbrirFlujo() {
  AAudioStreamBuilder* builder = nullptr;
  aaudio_result_t result = AAudio_createStreamBuilder(&builder);
  if (result != AAUDIO_OK || !builder) {
    LOGE("Failed to create AAudioStreamBuilder: %s", AAudio_convertResultToText(result));
    return false;
  }

  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setChannelCount(builder, kCanalesSalida);
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
    stream_ = nullptr;
    return false;
  }

  result = AAudioStream_requestStart(stream_);
  if (result != AAUDIO_OK) {
    LOGE("Failed to start AAudioStream: %s", AAudio_convertResultToText(result));
    AAudioStream_close(stream_);
    stream_ = nullptr;
    return false;
  }

  LOGI("AAudio stream started: 48kHz Stereo Float, BufferSize=%d",
       AAudioStream_getBufferSizeInFrames(stream_));
  return true;
}

void AndroidAAudioDriver::CerrarFlujo() {
  if (stream_) {
    AAudioStream_requestStop(stream_);
    AAudioStream_close(stream_);
    stream_ = nullptr;
  }
}

bool AndroidAAudioDriver::Initialize() {
#if NFSMW_MOTOR_NATIVO
  SetOutputGain(float(REXCVAR_GET(audio_ganancia_pct)) / 100.0f);
  sdl_audio_iniciado_ = SDL_InitSubSystem(SDL_INIT_AUDIO);
  if (!sdl_audio_iniciado_) {
    LOGW("SDL audio no inicia (%s): las peliculas iran sin sonido", SDL_GetError());
  }
#endif
  {
    std::lock_guard<std::mutex> lock(control_);
    if (!AbrirFlujo()) {
      return false;
    }
  }
  is_running_.store(true, std::memory_order_release);
  SDL_AddEventWatch(AAudioEventWatch, this);
  return true;
}

void AndroidAAudioDriver::Pause() {
  std::lock_guard<std::mutex> lock(control_);
  if (stream_) {
    const aaudio_result_t r = AAudioStream_requestPause(stream_);
    REXLOG_INFO("[aaudio] pausa: {}", AAudio_convertResultToText(r));
  }
}

void AndroidAAudioDriver::Resume() {
  std::lock_guard<std::mutex> lock(control_);
  if (stream_) {
    const aaudio_result_t r = AAudioStream_requestStart(stream_);
    REXLOG_INFO("[aaudio] reanudar: {}", AAudio_convertResultToText(r));
  }
}

void AndroidAAudioDriver::Shutdown() {
  if (!is_running_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }

  SDL_RemoveEventWatch(AAudioEventWatch, this);

  // Si habia una reconexion en marcha, que termine antes de cerrar: si no,
  // podria reabrir un flujo sobre un objeto que se esta destruyendo. Se espera
  // FUERA del cerrojo, porque ese hilo tambien lo coge.
  std::thread pendiente;
  {
    std::lock_guard<std::mutex> lock(control_);
    pendiente = std::move(reconexion_);
  }
  if (pendiente.joinable()) {
    pendiente.join();
  }

  // Despertar a quien espere en el semaforo del audio.
  if (semaphore_) {
    semaphore_->Release(16, nullptr);
  }

#if NFSMW_MOTOR_NATIVO
  if (sdl_audio_iniciado_) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sdl_audio_iniciado_ = false;
  }
#endif

  std::lock_guard<std::mutex> lock(control_);
  CerrarFlujo();
}

void AndroidAAudioDriver::SubmitFrame(uint32_t samples_ptr) {
  if (!is_running_.load(std::memory_order_relaxed)) {
    return;
  }

  // El fotograma de audio de la Xbox 360 vive en memoria virtual del invitado.
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

  // Sin hueco para un bloque entero: se descarta el NUEVO. El productor no toca
  // nunca el indice de lectura, que es lo que permite ir sin cerrojo. Con el
  // anillo dimensionado a kMaximumQueuedFrames no deberia pasar.
  const size_t w = escritura_.load(std::memory_order_relaxed);
  const size_t r = lectura_.load(std::memory_order_acquire);
  if (kCapacidad - (w - r) < kFloatsPorBloque) {
    return;
  }

  // PLANAR, 6 canales de 256 muestras cada uno (6144 bytes), en big-endian.
  // Orden de canales: 0=FL, 1=FR, 2=C, 3=LFE, 4=BL, 5=BR.
  const uint32_t* src = reinterpret_cast<const uint32_t*>(frame_data);
  const uint32_t* ch_fl = src + 0 * kMuestrasPorCanal;
  const uint32_t* ch_fr = src + 1 * kMuestrasPorCanal;
  const uint32_t* ch_c = src + 2 * kMuestrasPorCanal;
  const uint32_t* ch_lfe = src + 3 * kMuestrasPorCanal;
  const uint32_t* ch_bl = src + 4 * kMuestrasPorCanal;
  const uint32_t* ch_br = src + 5 * kMuestrasPorCanal;

  // Se pliega directamente en el anillo. Un bloque son 512 floats y la
  // capacidad es multiplo de 512, asi que un bloque nunca cruza el final: basta
  // una sola posicion base, sin partir la copia en dos.
  float* dst = anillo_.get() + (w & kMascara);
#if NFSMW_MOTOR_NATIVO
  if (IsGameOutputSuppressed()) {
    // Suena la pista de una pelicula: el juego calla, pero el bloque se
    // publica igual, que el ritmo lo sigue marcando el DAC.
    std::memset(dst, 0, kFloatsPorBloque * sizeof(float));
  } else {
    // Sin recortar: el limitador necesita ver los picos de verdad.
    const float ganancia = GetOutputGain();
    for (size_t i = 0; i < kMuestrasPorCanal; ++i) {
      const float c = LoadBEFloat(ch_c[i]) * kCenterGain;
      const float lfe = LoadBEFloat(ch_lfe[i]) * kLfeGain;
      const float mid = c + lfe;
      const float left = LoadBEFloat(ch_fl[i]) + mid + LoadBEFloat(ch_bl[i]) * kSurroundGain;
      const float right = LoadBEFloat(ch_fr[i]) + mid + LoadBEFloat(ch_br[i]) * kSurroundGain;
      dst[i * 2 + 0] = left * ganancia;
      dst[i * 2 + 1] = right * ganancia;
    }
    LimitOutput(dst, kMuestrasPorCanal, kCanalesSalida, kSampleRate, limitador_ganancia_);
  }
#else
  for (size_t i = 0; i < kMuestrasPorCanal; ++i) {
    const float c = LoadBEFloat(ch_c[i]) * kCenterGain;
    const float lfe = LoadBEFloat(ch_lfe[i]) * kLfeGain;
    const float mid = c + lfe;
    const float left = LoadBEFloat(ch_fl[i]) + mid + LoadBEFloat(ch_bl[i]) * kSurroundGain;
    const float right = LoadBEFloat(ch_fr[i]) + mid + LoadBEFloat(ch_br[i]) * kSurroundGain;
    dst[i * 2 + 0] = std::clamp(left, -1.0f, 1.0f);
    dst[i * 2 + 1] = std::clamp(right, -1.0f, 1.0f);
  }
#endif

  // Publicar el bloque: el release garantiza que el consumidor ve las muestras
  // escritas antes que el indice nuevo.
  escritura_.store(w + kFloatsPorBloque, std::memory_order_release);

  // El semaforo NO se suelta aqui: lo suelta AudioCallback segun el DAC
  // consume, que es lo que marca el ritmo de 48 kHz.
}

aaudio_data_callback_result_t AndroidAAudioDriver::AudioCallback(AAudioStream* /*stream*/,
                                                                 void* userData, void* audioData,
                                                                 int32_t numFrames) {
  auto* self = static_cast<AndroidAAudioDriver*>(userData);
  float* out = static_cast<float*>(audioData);
  if (numFrames <= 0) {
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
  }

  const size_t pedidos = size_t(numFrames) * kCanalesSalida;
  const size_t r = self->lectura_.load(std::memory_order_relaxed);
  const size_t w = self->escritura_.load(std::memory_order_acquire);
  const size_t n = std::min(w - r, pedidos);

  // Copiar lo que haya, en uno o dos tramos segun cruce el final del anillo.
  const size_t pos = r & kMascara;
  const size_t primero = std::min(n, kCapacidad - pos);
  std::memcpy(out, self->anillo_.get() + pos, primero * sizeof(float));
  if (n > primero) {
    std::memcpy(out + primero, self->anillo_.get(), (n - primero) * sizeof(float));
  }
  // Lo que falte, silencio.
  if (n < pedidos) {
    std::memset(out + n, 0, (pedidos - n) * sizeof(float));
  }
  self->lectura_.store(r + n, std::memory_order_release);

  // Un permiso por cada 256 fotogramas que consume el DAC, haya dato o no: eso
  // es lo que clava el hilo de audio del juego a 48 kHz (187,5 bloques/s).
  if (self->semaphore_) {
    self->consumidos_ += size_t(numFrames);
    const size_t soltar = self->consumidos_ / kMuestrasPorCanal;
    if (soltar > 0) {
      self->consumidos_ -= soltar * kMuestrasPorCanal;
      self->semaphore_->Release(int32_t(soltar), nullptr);
    }
  }

  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void AndroidAAudioDriver::ErrorCallback(AAudioStream* /*stream*/, void* userData,
                                        aaudio_result_t error) {
  LOGW("AAudio stream error: %s", AAudio_convertResultToText(error));
  if (error != AAUDIO_ERROR_DISCONNECTED) {
    return;
  }
  auto* self = static_cast<AndroidAAudioDriver*>(userData);

  // AAudio NO permite cerrar ni reabrir el flujo desde su propio callback de
  // error: puede colgarse. Se hace en un hilo aparte. El anillo sobrevive a la
  // reconexion, asi que no se pierde lo ya plegado.
  //
  // Una reconexion anterior, si la hubo, se espera FUERA del cerrojo: ese hilo
  // tambien lo coge y se bloquearian los dos.
  std::thread anterior;
  {
    std::lock_guard<std::mutex> lock(self->control_);
    if (!self->is_running_.load(std::memory_order_acquire)) {
      return;
    }
    anterior = std::move(self->reconexion_);
  }
  if (anterior.joinable()) {
    anterior.join();
  }

  std::lock_guard<std::mutex> lock(self->control_);
  if (!self->is_running_.load(std::memory_order_acquire)) {
    return;
  }
  self->reconexion_ = std::thread([self]() {
    std::lock_guard<std::mutex> lock(self->control_);
    self->CerrarFlujo();
    if (self->is_running_.load(std::memory_order_acquire)) {
      self->AbrirFlujo();
    }
  });
}

// ---------------------------------------------------------------------------
//  AndroidAAudioSystem
// ---------------------------------------------------------------------------

AndroidAAudioSystem::AndroidAAudioSystem(runtime::FunctionDispatcher* function_dispatcher)
    : AudioSystem(function_dispatcher) {}

AndroidAAudioSystem::~AndroidAAudioSystem() = default;

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
