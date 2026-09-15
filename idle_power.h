#pragma once
#include <Arduino.h>

class Pet;

// Ambos plazos se cuentan desde la ultima entrada del usuario.
constexpr uint32_t IDLE_SLEEP_MS = 90000UL;
constexpr uint32_t IDLE_SHUTDOWN_MS = 120000UL;

class IdlePower {
public:
  void recordActivity() { lastActivity = millis(); }
  // Duerme a la mascota al atenuar; true solicita apagar mediante el PMU.
  bool update(Pet &pet);
  bool dimmed() const { return isDimmed; }

private:
  uint32_t lastActivity = 0;
  bool isDimmed = false;
};
