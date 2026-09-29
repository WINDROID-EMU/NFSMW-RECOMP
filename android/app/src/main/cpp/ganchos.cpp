// NFSMW Recompiled - funciones del juego sustituidas en Android
//
// Las funciones generadas son simbolos debiles que saltan a __imp__sub_X (ver
// DEFINE_REX_FUNC en nfsmw_pch.h). Definir aqui sub_X con enlace fuerte la
// sustituye, y __imp__sub_X sigue siendo la original.
//
// ESPERA DE ESPACIO EN EL ANILLO DE COMANDOS (sub_825A5D18)
//
// Cuando el anillo de comandos de la GPU esta lleno, el D3D del juego espera a
// que el procesador de comandos (CP) avance su puntero de lectura. Lo hace
// GIRANDO: sub_82597690 y sub_82597380 releen el puntero una y otra vez, y en
// cada vuelta llaman a sub_825A5D18, el vigilante de cuelgues (devuelve 0 si
// el puntero lleva 2 s sin moverse). En la Xbox 360 eso era barato. Aqui el
// CP es el cuello de botella y solo publica el puntero dos veces por fotograma:
// el hilo de render del juego se pasaba el fotograma entero al 100 % en un
// nucleo prime, calentando el movil hasta el estrangulamiento termico, que a su
// vez frena al CP y a la GPU.
//
// El gancho: si el puntero no se ha movido desde la ultima vuelta, dormir en
// un futex hasta que el CP publique uno nuevo (nfsmw_cp_rptr_seq, de
// tools/parche_espera_anillo.py), con un tope de 1 ms. Sin ese contador (SDK
// sin el parche) no hace nada distinto.
//
// Solo cuando llaman los dos bucles de espacio en el anillo, reconocidos por
// la direccion de retorno. Los demas usuarios del vigilante esperan fences que
// el CP escribe a mitad de tramo, y esperarlos aqui les anadiria latencia.
//
// Para comparar: adb shell setprop debug.nfsmw.espera_anillo 0 lo desactiva
// (se lee al arrancar la partida).
//
// SIN POSPROCESADO (sub_822246F0 y sub_82224380)
//
// El "Disable Post Processing" de los parches de Xenia para este juego
// (game-patches, 454107D9, de illusion): las dos unicas lecturas del byte
// global 0x828F48B2 -lbz en 0x82224710 y 0x8222442C- pasan a leer 0. Aqui,
// sin tocar el codigo generado: mientras corren esas dos funciones el byte
// vale 0, y al salir se restaura. Cvar nfsmw_sin_posprocesado, apagado por
// defecto.

#include <dlfcn.h>
#include <sys/system_properties.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <ctime>

#include "nfsmw_pch.h"

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(nfsmw_sin_posprocesado, false, "Juego",
                    "Quitar el posprocesado: filtro de color, bloom y desenfoque. Menos trabajo "
                    "de GPU, pero el juego pierde su aspecto");

extern "C" void __imp__sub_825A5D18(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_822246F0(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82224380(PPCContext& __restrict ctx, uint8_t* base);

namespace {

// Direcciones de retorno de las llamadas al vigilante desde los dos bucles de
// espera de espacio en el anillo.
constexpr uint32_t kVueltaEsperaAnillo1 = 0x82597730;  // sub_82597690
constexpr uint32_t kVueltaEsperaAnillo2 = 0x82597404;  // sub_82597380

// Desplazamiento, dentro del dispositivo D3D, del puntero a la copia en
// memoria del puntero de lectura del CP (lwz rX,10384(r31) en ambos bucles).
constexpr uint32_t kDispositivoPunteroRptr = 10384;

std::atomic<uint32_t>* SecuenciaRptr() {
  static std::atomic<uint32_t>* const secuencia = []() -> std::atomic<uint32_t>* {
    char valor[PROP_VALUE_MAX] = {};
    if (__system_property_get("debug.nfsmw.espera_anillo", valor) > 0 && valor[0] == '0') {
      return nullptr;
    }
    void* plugin = dlopen("librexgpu-xenos.so", RTLD_NOW | RTLD_NOLOAD);
    return plugin ? static_cast<std::atomic<uint32_t>*>(dlsym(plugin, "nfsmw_cp_rptr_seq"))
                  : nullptr;
  }();
  return secuencia;
}

}  // namespace

extern "C" REX_FUNC(sub_825A5D18) {
  const uint32_t vuelta = uint32_t(ctx.lr);
  if (vuelta == kVueltaEsperaAnillo1 || vuelta == kVueltaEsperaAnillo2) {
    if (std::atomic<uint32_t>* secuencia = SecuenciaRptr()) {
      // Primero la secuencia y luego el puntero: si el CP publica entre medias,
      // la secuencia ya habra cambiado y el futex vuelve en el acto.
      const uint32_t antes = secuencia->load(std::memory_order_acquire);
      // r3 apunta al estado del vigilante: { dispositivo, tick, puntero visto }.
      const uint32_t estado = ctx.r3.u32;
      const uint32_t dispositivo = REX_LOAD_U32(estado + 0);
      const uint32_t visto = REX_LOAD_U32(estado + 8);
      const uint32_t rptr = REX_LOAD_U32(REX_LOAD_U32(dispositivo + kDispositivoPunteroRptr));
      if (rptr == visto) {
        timespec tope{0, 1000000};  // 1 ms
        syscall(SYS_futex, secuencia, FUTEX_WAIT_PRIVATE, antes, &tope, nullptr, 0);
      }
    }
  }
  __imp__sub_825A5D18(ctx, base);
}

namespace {

// Byte global que activa el posprocesado (lis r11,-32113 + 18610).
constexpr uint32_t kPosprocesadoActivo = 0x828F48B2;

template <void (*kOriginal)(PPCContext&, uint8_t*)>
void SinPosprocesado(PPCContext& __restrict ctx, uint8_t* base) {
  if (!REXCVAR_GET(nfsmw_sin_posprocesado)) {
    kOriginal(ctx, base);
    return;
  }
  const uint8_t antes = REX_LOAD_U8(kPosprocesadoActivo);
  REX_STORE_U8(kPosprocesadoActivo, 0);
  kOriginal(ctx, base);
  REX_STORE_U8(kPosprocesadoActivo, antes);
}

}  // namespace

extern "C" REX_FUNC(sub_822246F0) {
  SinPosprocesado<__imp__sub_822246F0>(ctx, base);
}

extern "C" REX_FUNC(sub_82224380) {
  SinPosprocesado<__imp__sub_82224380>(ctx, base);
}
