#pragma once
#include "Arduino.h"
#include "SPI.h"
struct TS_Point { int16_t x = 0, y = 0, z = 0; };
extern bool emuTouch; extern int emuTX, emuTY;
struct XPT2046_Touchscreen {
    XPT2046_Touchscreen(int, int = 255) {}
    bool begin(SPIClass &) { return true; }
    void setRotation(int) {}
    bool touched() { return emuTouch; }
    TS_Point getPoint();
};
