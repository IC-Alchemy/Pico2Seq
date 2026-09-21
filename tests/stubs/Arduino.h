#pragma once
#include <stdint.h>
#include <cstddef>
#include <string>

// GPIO constants
#define INPUT     0
#define OUTPUT    1
#define INPUT_PULLUP 2
#define HIGH      1
#define LOW       0

// Print bases, macros in the real core (ArduinoCore-API Print.h). Kept here so
// a host build catches identifiers they would break, like an enum value OCT.
#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

// GPIO stubs
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int  digitalRead(uint8_t) { return LOW; }
inline void analogWrite(uint8_t, int) {}
inline int  analogRead(uint8_t) { return 0; }

// Arduino primitive types
typedef unsigned long ulong;

// Arduino time stubs. Host tests can drive the clock via
// arduino_testing::setMillis()/advanceMillis(); the default of 0 matches the
// previous hardwired stub, so existing suites keep passing unchanged.
namespace arduino_testing {
inline unsigned long g_stubMillis = 0;
inline void setMillis(unsigned long ms) { g_stubMillis = ms; }
inline void advanceMillis(unsigned long ms) { g_stubMillis += ms; }
} // namespace arduino_testing
inline unsigned long millis() { return arduino_testing::g_stubMillis; }
inline unsigned long micros() { return arduino_testing::g_stubMillis * 1000UL; }
inline void delay(unsigned long) {}
inline void delayMicroseconds(unsigned long) {}

// Arduino math stubs
inline long random(long max) { return 0; }
inline long random(long min, long max) { return min; }
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
inline long constrain(long x, long lo, long hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

// Arduino String (minimal)
using String = std::string;

// Arduino F() macro (returns string literal unchanged on host)
#define F(s) (s)

// Serial stub
struct HardwareSerial {
    void begin(long) {}
    void flush() {}
    template<typename T>             void print(T)    {}
    template<typename T>             void println(T)  {}
    template<typename T, typename U> void print(T, U) {}
    void println() {}
};
inline HardwareSerial Serial;
