#pragma once
// Desktop replacement for the Adafruit_SH110X library (SH1106G OLED).
//
// Owns the same 128x64 1bpp page buffer layout as the hardware driver
// (bit y%8 of buffer[x + (y/8)*width]); display() hands the frame to the
// DisplayBridge snapshot instead of pushing I2C bytes.

#include <Adafruit_GFX.h>
#include <Wire.h>
#include "p2s_host_hooks.h"

#define SH110X_WHITE 1
#define SH110X_BLACK 0
#define SH110X_INVERSE 2

class Adafruit_SH1106G : public Adafruit_GFX
{
public:
    Adafruit_SH1106G()
        : Adafruit_GFX(128, 64), buffer{} {}

    Adafruit_SH1106G(uint8_t /*w*/, uint8_t /*h*/, TwoWire * /*twi*/ = nullptr,
                     int8_t /*rst_pin*/ = -1, uint32_t /*preclk*/ = 100000,
                     uint32_t /*postclk*/ = 100000)
        : Adafruit_GFX(128, 64), buffer{} {}

    bool begin(uint8_t /*address*/ = 0x3C, bool /*reset*/ = true)
    {
        clearDisplay();
        return true;
    }

    void clearDisplay() { std::memset(buffer, 0, sizeof(buffer)); }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override
    {
        if (x < 0 || x >= width() || y < 0 || y >= height())
            return;
        const bool on = color != 0;
        uint8_t &page = buffer[x + (y / 8) * width()];
        if (on)
            page |= static_cast<uint8_t>(1 << (y & 7));
        else
            page &= static_cast<uint8_t>(~(1 << (y & 7)));
    }

    uint8_t *getBuffer() { return buffer; }

    // GUI poll point: snapshot the frame exactly where the firmware pushes
    // it to the panel.
    void display() { p2s::host::onOledDisplay(buffer); }

    void dim(bool) {}
    void invertDisplay(bool) {}
    void cp437(bool) {}
    void setRotation(uint8_t) {}

private:
    uint8_t buffer[(128 * 64) / 8];
};
