#pragma once

#include <rex/audio/audio_driver.h>
#include <rex/audio/audio_system.h>
#include <rex/thread.h>
#include <aaudio/AAudio.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>

namespace rex::audio::android {

class AndroidAAudioDriver final : public AudioDriver {
 public:
  AndroidAAudioDriver(memory::Memory* memory, rex::thread::Semaphore* semaphore);
  ~AndroidAAudioDriver() override;

  bool Initialize();
  void Shutdown();

  void Pause();
  void Resume();

  void SubmitFrame(uint32_t samples_ptr) override;

 private:
  static aaudio_data_callback_result_t AudioCallback(AAudioStream* stream, void* userData,
                                                     void* audioData, int32_t numFrames);
  static void ErrorCallback(AAudioStream* stream, void* userData, aaudio_result_t error);

  bool AbrirFlujo();
  void CerrarFlujo();

  // Un bloque del juego: 256 muestras por canal, ya plegadas a estereo.
  static constexpr size_t kMuestrasPorCanal = 256;
  static constexpr size_t kCanalesSalida = 2;
  static constexpr size_t kFloatsPorBloque = kMuestrasPorCanal * kCanalesSalida;

  // Capacidad del anillo: los 64 bloques de AudioSystem::kMaximumQueuedFrames.
  // Con el control de flujo por semaforo nunca hay mas bloques en vuelo que
  // permisos, asi que esto no desborda con NINGUN valor de audio_maxqframes
  // (4-64). Potencia de dos para indexar con mascara, sin divisiones.
  static constexpr size_t kBloquesAnillo = 64;
  static constexpr size_t kCapacidad = kBloquesAnillo * kFloatsPorBloque;  // 32768 floats, 128 KB
  static_assert((kCapacidad & (kCapacidad - 1)) == 0, "la capacidad tiene que ser potencia de 2");
  static constexpr size_t kMascara = kCapacidad - 1;

  rex::thread::Semaphore* semaphore_ = nullptr;
  AAudioStream* stream_ = nullptr;
  std::atomic<bool> is_running_{false};

  // Anillo de un productor (SubmitFrame, hilo del juego) y un consumidor
  // (AudioCallback, hilo de tiempo real de AAudio). SIN CERROJO: el callback de
  // tiempo real no puede esperar a nadie, o el audio se corta. Los contadores
  // son monotonos y solo se enmascaran al indexar, asi que "lleno" y "vacio" no
  // se confunden.
  std::unique_ptr<float[]> anillo_;
  alignas(64) std::atomic<size_t> escritura_{0};  // solo lo toca el productor
  alignas(64) std::atomic<size_t> lectura_{0};    // solo lo toca el consumidor

  // Fotogramas consumidos por el DAC que aun no llegan a un bloque entero. Solo
  // lo toca el callback.
  size_t consumidos_ = 0;

  // AAUDIO_ERROR_DISCONNECTED (cascos, Bluetooth): el flujo no se puede cerrar
  // desde su propio callback de error, asi que se reabre desde este hilo.
  std::thread reconexion_;

  // Cerrojo SOLO del camino de control: abrir, cerrar, pausar, reanudar y
  // reconectar, que tocan stream_ y reconexion_ desde hilos distintos. El
  // callback de datos no lo usa nunca.
  std::mutex control_;
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
};

}  // namespace rex::audio::android
