#pragma once
#include "Arduino.h"
#define MIDI_CHANNEL_OMNI 0
namespace midi { typedef uint8_t DataByte; typedef uint8_t Channel; }
struct MidiStub {
    void begin(int){} bool read(){ return false; } void turnThruOff(){}
    template <class F> void setHandleNoteOn(F){} template <class F> void setHandleNoteOff(F){}
    template <class F> void setHandleControlChange(F){} template <class F> void setHandleProgramChange(F){}
    template <class F> void setHandleClock(F){} template <class F> void setHandleStart(F){}
    template <class F> void setHandleContinue(F){} template <class F> void setHandleStop(F){}
    template <class... A> void sendNoteOn(A...){} template <class... A> void sendNoteOff(A...){}
    template <class... A> void sendControlChange(A...){} template <class... A> void sendRealTime(A...){}
};
#define MIDI_CREATE_INSTANCE(T, port, name) MidiStub name;
