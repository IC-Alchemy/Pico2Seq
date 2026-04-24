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

// GPIO stubs
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int  digitalRead(uint8_t) { return LOW; }
inline void analogWrite(uint8_t, int) {}
inline int  analogRead(uint8_t) { return 0; }

// Arduino primitive types
typedef unsigned long ulong;

// Arduino time stubs
inline unsigned long millis() { return 0; }
inline unsigned long micros() { return 0; }
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
