#pragma once
#include "Adafruit_GFX.h"
#include "SPI.h"
// tela emulada: um canvas 320x240 já na orientação horizontal
struct Adafruit_ILI9341 : public GFXcanvas16 {
    Adafruit_ILI9341(SPIClass *, int, int, int) : GFXcanvas16(320, 240) {}
    void begin(uint32_t = 0) {}
    void invertDisplay(bool) {}
    void setRotation(uint8_t) override {}
};
