#pragma once
#include <stdint.h>
#include <stddef.h>

constexpr int I2S_MODE_STD = 0, I2S_DATA_BIT_WIDTH_16BIT = 0;
constexpr int I2S_SLOT_MODE_STEREO = 0, I2S_STD_SLOT_BOTH = 0;
class I2SClass {
public:
  void setPins(int, int, int, int, int) {}
  bool begin(int, int, int, int, int) { return true; }
  size_t write(uint8_t *data, size_t size);
};
