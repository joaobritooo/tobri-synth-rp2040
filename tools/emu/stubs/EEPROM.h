#pragma once
#include "Arduino.h"
struct EEPROMStub {
    uint8_t d[4096];
    EEPROMStub() { memset(d, 0xFF, sizeof d); }
    void begin(int) {}
    uint8_t read(int a) { return (a >= 0 && a < 4096) ? d[a] : 0xFF; }
    void write(int a, uint8_t v) { if (a >= 0 && a < 4096) d[a] = v; }
    bool commit() { return true; }
    template <class T> T &get(int a, T &t) { memcpy(&t, d + a, sizeof(T)); return t; }
    template <class T> const T &put(int a, const T &t) { memcpy(d + a, &t, sizeof(T)); return t; }
};
extern EEPROMStub EEPROM;
