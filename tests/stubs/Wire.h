#pragma once
#include <stdint.h>
#include <cstddef>

// Minimal I2C stub — all operations are no-ops
class TwoWire {
public:
    void begin() {}
    void begin(uint8_t) {}
    void setClock(uint32_t) {}
    void beginTransmission(uint8_t) {}
    uint8_t endTransmission(bool = true) { return 0; }
    uint8_t requestFrom(uint8_t, uint8_t, bool = true) { return 0; }
    size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t*, size_t n) { return n; }
    int read() { return 0; }
    int available() { return 0; }
    int peek() { return 0; }
};

inline TwoWire Wire;
