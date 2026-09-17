#pragma once
// Desktop replacement for the Arduino Wire library.
//
// Phase 0 keeps the no-op shape proven by tests/stubs/Wire.h. Phase 2
// upgrades this into a routed virtual I2C bus (fake MPR121 / TMAG5273 /
// Alchemy tiles) without changing any firmware include.

#include <stdint.h>
#include <cstddef>

class TwoWire
{
public:
    void begin() {}
    void begin(uint8_t) {}
    void end() {}
    void setClock(uint32_t) {}
    void setSDA(uint8_t) {}
    void setSCL(uint8_t) {}
    void beginTransmission(uint8_t) {}
    uint8_t endTransmission(bool = true) { return 0; }
    uint8_t requestFrom(uint8_t, uint8_t, bool = true) { return 0; }
    size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t *, size_t n) { return n; }
    int read() { return 0; }
    int available() { return 0; }
    int peek() { return 0; }
    void setTimeout(uint32_t, bool = true) {}
};

inline TwoWire Wire;
inline TwoWire Wire1;
