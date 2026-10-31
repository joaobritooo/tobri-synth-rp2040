#pragma once
#include "Arduino.h"
struct SPIClass { void setSCK(int){} void setTX(int){} void setRX(int){} void begin(){} };
extern SPIClass SPI1;
