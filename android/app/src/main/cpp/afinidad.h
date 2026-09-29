// NFSMW Recompiled - hilos fijados a nucleos en Android (ver afinidad.cpp)

#pragma once

#include <memory>

namespace nfsmw::afinidad {

class Vigilante;

struct BorrarVigilante {
  void operator()(Vigilante* v) const;
};

using VigilantePtr = std::unique_ptr<Vigilante, BorrarVigilante>;

// Lee el cvar thread_affinity y, si hay reglas que aplicar, arranca el
// vigilante. Devuelve nullptr si no hay nada que hacer. Destruirlo lo para.
VigilantePtr Arrancar();

}  // namespace nfsmw::afinidad
