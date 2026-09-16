#pragma once

#include <Arduino_GFX_Library.h>

// The stock CO5300 initialization turns the panel on at brightness 0xD0.
// Keep the driver's reset/configuration sequence, but start at zero brightness
// before DISPON. setup() reveals the first restored frame via updatePower().
class TamaPokeDisplay : public Arduino_CO5300 {
 public:
  using Arduino_CO5300::Arduino_CO5300;

 protected:
  void tftInit() override {
    if (_rst != GFX_NOT_DEFINED) {
      pinMode(_rst, OUTPUT);
      digitalWrite(_rst, HIGH);
      delay(10);
      digitalWrite(_rst, LOW);
      delay(CO5300_RST_DELAY);
      digitalWrite(_rst, HIGH);
      delay(CO5300_RST_DELAY);
    } else {
      _bus->sendCommand(CO5300_C_SWRESET);
      delay(CO5300_RST_DELAY);
    }

    // Based on Arduino_GFX's co5300_init_operations. Set normal brightness
    // before enabling the display so no uninitialized frame becomes visible.
    static const uint8_t initOperations[] = {
      BEGIN_WRITE,
      WRITE_COMMAND_8, CO5300_C_SLPOUT,
      END_WRITE,
      DELAY, CO5300_SLPOUT_DELAY,
      BEGIN_WRITE,
      WRITE_C8_D8, 0xFE, 0x00,
      WRITE_C8_D8, CO5300_W_SPIMODECTL, 0x80,
      WRITE_C8_D8, CO5300_W_PIXFMT, 0x55,
      WRITE_C8_D8, CO5300_W_WCTRLD1, 0x20,
      WRITE_C8_D8, CO5300_W_WDBRIGHTNESSVALHBM, 0xFF,
      WRITE_C8_D8, CO5300_W_WDBRIGHTNESSVALNOR, 0x00,
      WRITE_COMMAND_8, CO5300_C_DISPON,
      WRITE_C8_D8, CO5300_W_WCE, 0x00,
      END_WRITE,
      DELAY, 10
    };
    _bus->batchOperation(initOperations, sizeof(initOperations));
    invertDisplay(false);
  }
};
