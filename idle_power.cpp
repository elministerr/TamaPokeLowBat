#include "idle_power.h"
#include "pet.h"

bool IdlePower::update(Pet &pet) {
  // Leer el reloj DESPUES de la entrada evita restar un timestamp tactil
  // mas reciente que el now capturado al principio del loop (underflow).
  uint32_t idle = millis() - lastActivity;
  isDimmed = idle >= IDLE_SLEEP_MS;
  if (isDimmed && !pet.sleeping && !pet.isEgg() && !pet.ceremony) {
    pet.toggleLight();  // persiste el sueno; solo LUZ vuelve a despertarla
  }
  return idle >= IDLE_SHUTDOWN_MS;
}
