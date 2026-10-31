#pragma once
#include "Arduino.h"
struct Adafruit_USBD_MIDI { void begin(){} };
struct USBDeviceStub { void setID(int,int){} void setManufacturerDescriptor(const char*){} void setProductDescriptor(const char*){} bool mounted(){ return true; } };
extern USBDeviceStub USBDevice;
