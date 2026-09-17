#pragma once
// Desktop replacement for the Adafruit MPR121 capacitive-touch driver.
//
// Matrix.cpp uses exactly two calls: begin() and touched(). On the desktop
// the GUI owns the touch state and publishes it through p2s::host (see
// shim_runtime.cpp); touched() returns that bitmask. The IRQ-driven scan
// cadence is reproduced by the GUI pushing new touch states, which arm the
// pending-interrupt flag just like the physical GP8 falling edge would.

#include <stdint.h>
#include <atomic>

class TwoWire;

namespace p2s
{
namespace host
{
// GUI -> engine: full 32-pad electrode bitmask (1 = touched).
void setMpr121TouchBits(uint16_t bits);
uint16_t mpr121TouchBits();
}
}

class Adafruit_MPR121
{
public:
    Adafruit_MPR121() = default;

    bool begin(uint8_t addr = 0x5A, TwoWire *wire = nullptr)
    {
        (void)addr;
        (void)wire;
        return true;
    }

    void setAutoconfig(bool) {}
    void setThresholds(uint8_t /*touch*/, uint8_t /*release*/) {}

    uint16_t touched() { return p2s::host::mpr121TouchBits(); }
};
