#pragma once
// Desktop replacement for the Arduino core header.
//
// Unlike tests/stubs/Arduino.h (compile-only no-ops), this shim provides a
// REAL monotonic millis()/micros(), a working delay(), and a Serial sink so
// the firmware's app-plane code keeps its timing semantics on Windows.
// It shadows the real Arduino.h via the include path, exactly like the test
// stubs do for the host test suite.

#include <stdint.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>

#define PROGMEM
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))

#define A0 26

// Pin-mode constants collide with winuser.h's INPUT typedef when a
// translation unit pulls in windows.h before this header; only firmware
// files that never see windows.h use these identifiers.
#ifndef _WINDOWS_
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#endif
#define HIGH 1
#define LOW 0

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

#define PI 3.1415926535897932384626433832795

#define CHANGE 1
#define FALLING 2
#define RISING 3

typedef unsigned long ulong;
typedef uint8_t byte;

namespace p2s
{
namespace host
{
// Monotonic host clock with natural uint32 wraparound, matching Arduino.
uint32_t clockMillis();
uint32_t clockMicros();
// Serial sink: writes through when P2S_SERIAL_LOG is set, discards otherwise.
void serialWrite(const char *text);
bool serialEnabled();
}
}

inline unsigned long millis() { return p2s::host::clockMillis(); }
inline unsigned long micros() { return p2s::host::clockMicros(); }
void delay(unsigned long ms);
inline void delayMicroseconds(unsigned long us) { delay((us + 999) / 1000); }

// GPIO: no hardware behind the desktop port.
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t) { return LOW; }
inline void analogWrite(uint8_t, int) {}
inline int analogRead(uint8_t) { return 0; }
inline unsigned long pulseIn(uint8_t, uint8_t, unsigned long = 1000000UL) { return 0; }

// The desktop port maps interrupt masking onto the same global critical
// section the HostClock tick runner uses (ISR-equivalence for the control
// thread). Definitions live in shim_runtime.cpp.
void noInterrupts();
void interrupts();

namespace p2s
{
namespace host
{
// attachInterrupt registry: the fake MPR121 touch driver fires the stored
// callback on touch-state changes, standing in for the GP8 falling edge.
using InterruptFn = void (*)();
void registerInterrupt(uint8_t pin, InterruptFn fn);
}
}

inline void attachInterrupt(uint8_t pin, void (*fn)(), int)
{
    p2s::host::registerInterrupt(pin, fn);
}
inline void detachInterrupt(uint8_t) {}
inline uint8_t digitalPinToInterrupt(uint8_t pin) { return pin; }

inline void randomSeed(unsigned long) {}

// Arduino math helpers (same behaviour as the test stubs).
inline long random(long max) { return 0 < max ? std::min<long>(max - 1, 0) : 0; }
inline long random(long min, long max) { return min < max ? min : max; }
inline long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
inline long constrain(long x, long lo, long hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Arduino min/max: unqualified functions (not macros) so std::min/std::max
// in library headers keep compiling.
template <typename T>
constexpr const T &max(const T &a, const T &b) { return a > b ? a : b; }
template <typename T>
constexpr const T &min(const T &a, const T &b) { return a < b ? a : b; }

// Arduino String: std::string with the Arduino core's converting
// constructors (numbers, chars; floats with digit count). Note Arduino's
// String(unsigned char) keeps char semantics, matching the hardware build.
class String : public std::string
{
public:
    using std::string::string;
    String() = default;
    String(const std::string &s) : std::string(s) {}
    String(char c) : std::string(1, c) {}
    String(unsigned char c) : std::string(1, static_cast<char>(c)) {}
    String(int v) { char b[24]; std::snprintf(b, sizeof(b), "%d", v); assign(b); }
    String(unsigned int v) { char b[24]; std::snprintf(b, sizeof(b), "%u", v); assign(b); }
    String(long v) { char b[32]; std::snprintf(b, sizeof(b), "%ld", v); assign(b); }
    String(unsigned long v) { char b[32]; std::snprintf(b, sizeof(b), "%lu", v); assign(b); }
    String(float v, int decimals = 2)
    {
        char b[48];
        std::snprintf(b, sizeof(b), "%.*f", decimals, static_cast<double>(v));
        assign(b);
    }
    String(double v, int decimals = 2)
    {
        char b[48];
        std::snprintf(b, sizeof(b), "%.*f", decimals, v);
        assign(b);
    }
};

inline String operator+(const String &a, const String &b)
{
    String out = a;
    out.append(b);
    return out;
}

#define F(s) (s)

// Serial replacement: template print/println funnel any streamable value
// through the sink; printf is declared here and defined in shim_runtime.cpp.
struct HostSerial
{
    void begin(unsigned long) {}
    void end() {}
    void flush() {}
    operator bool() const { return p2s::host::serialEnabled(); }
    int available() { return 0; }
    int read() { return -1; }
    int peek() { return -1; }
    size_t write(uint8_t) { return 1; }

    template <typename T>
    void print(T value)
    {
        std::ostringstream os;
        os << value;
        p2s::host::serialWrite(os.str().c_str());
    }
    template <typename T>
    void println(T value)
    {
        print(value);
        p2s::host::serialWrite("\n");
    }
    template <typename T, typename U>
    void print(T value, U) // base/format argument ignored
    {
        print(value);
    }
    template <typename T, typename U>
    void println(T value, U base)
    {
        print(value, base);
        p2s::host::serialWrite("\n");
    }
    void println() { p2s::host::serialWrite("\n"); }
    void printf(const char *fmt, ...);
};

extern HostSerial Serial;
