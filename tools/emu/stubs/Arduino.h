// Host-side stand-in for the Arduino core: just enough to run the UI drawing code on a PC.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <algorithm>

#ifndef USE_TINYUSB
#define USE_TINYUSB 1
#endif

#define ARDUINO 10800
typedef uint8_t byte;
class __FlashStringHelper;
struct String { const char *s = ""; String(const char *c = "") : s(c) {} const char *c_str() const { return s; } unsigned length() const { return strlen(s); } void getBytes(unsigned char *b, unsigned n) const { strncpy((char *)b, s, n); } void toCharArray(char *b, unsigned n) const { strncpy(b, s, n); } };
#define radians(d) ((d) * M_PI / 180.0)
typedef bool boolean;

extern unsigned long emuMs;
inline unsigned long millis() { return emuMs; }
inline unsigned long micros() { return emuMs * 1000UL; }
inline void delay(unsigned long ms) { emuMs += ms; }
inline void delayMicroseconds(unsigned) {}
inline void yield() {}

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define CHANGE 3
#define RISING 4
#define FALLING 5
inline void pinMode(int, int) {}
inline int digitalRead(int) { return HIGH; }
inline void digitalWrite(int, int) {}
inline int analogRead(int) { return 0; }
inline int digitalPinToInterrupt(int p) { return p; }
inline void attachInterrupt(int, void (*)(), int) {}
inline void noInterrupts() {}
inline void interrupts() {}

#define __not_in_flash_func(f) f
#define __time_critical_func(f) f
#ifndef PROGMEM
#define PROGMEM
#endif
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#define pgm_read_word(a) (*(const uint16_t *)(a))
#define pgm_read_dword(a) (*(const uint32_t *)(a))
#define pgm_read_pointer(a) (*(void * const *)(a))

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEC 10
#define HEX 16

template <class A, class B, class C> inline auto constrain(A x, B lo, C hi) -> decltype(x + lo + hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
template <class A, class B> inline auto min(A a, B b) -> decltype(a + b) { return a < b ? a : b; }
template <class A, class B> inline auto max(A a, B b) -> decltype(a + b) { return a > b ? a : b; }
template <class A> inline A sq(A a) { return a * a; }
inline long map(long x, long a, long b, long c, long d) { return (x - a) * (d - c) / (b - a) + c; }
inline long random(long hi) { return hi > 0 ? (rand() % hi) : 0; }
inline long random(long lo, long hi) { return hi > lo ? lo + rand() % (hi - lo) : lo; }
inline void randomSeed(unsigned long s) { srand((unsigned)s); }

class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t *b, size_t n) { size_t k = 0; while (n--) k += write(*b++); return k; }
    size_t write(const char *s) { return s ? write((const uint8_t *)s, strlen(s)) : 0; }
    size_t print(const char *s) { return write(s); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(int v) { char b[16]; snprintf(b, sizeof b, "%d", v); return write(b); }
    size_t print(unsigned v) { char b[16]; snprintf(b, sizeof b, "%u", v); return write(b); }
    size_t print(long v) { char b[24]; snprintf(b, sizeof b, "%ld", v); return write(b); }
    size_t print(unsigned long v) { char b[24]; snprintf(b, sizeof b, "%lu", v); return write(b); }
    size_t print(double v, int d = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", d, v); return write(b); }
    template <class T> size_t println(T v) { size_t n = print(v); return n + write("\r\n"); }
    size_t println() { return write("\r\n"); }
    size_t printf(const char *f, ...) { char b[256]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a); return write(b); }
};

struct SerialStub : public Print {
    void begin(unsigned long) {}
    size_t write(uint8_t) override { return 1; }
    operator bool() { return true; }
};
extern SerialStub Serial;
