#pragma once
#include "Arduino.h"
// I2S emulado: libera 32 amostras estéreo por ms de relógio emulado
struct I2S {
    unsigned long frames = 0;
    I2S(int) {}
    void setBCLK(int){} void setDATA(int){} void setBitsPerSample(int){} void setBuffers(int,int){}
    bool begin(long){ return true; }
    int availableForWrite(){ long due = (long)emuMs * 32 - (long)frames; return due > 0 ? (int)(due * 2) : 0; }
    size_t write16(int16_t,int16_t){ frames++; return 1; }
};
