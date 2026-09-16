#pragma once
#include <stdint.h>

struct TwoWire {
  void beginTransmission(uint8_t) {}
  void write(uint8_t) {}
  uint8_t endTransmission(bool stop = true);
  void requestFrom(uint8_t, int) {}
  int available() { return 1; }
  uint8_t read() { return 0; }
};
extern TwoWire Wire;
