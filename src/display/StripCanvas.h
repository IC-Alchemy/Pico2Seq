#pragma once

#include <Adafruit_GFX.h>
#include <algorithm>
#include <cstdint>

// A 320x8 RGB565 strip, not a 300KB framebuffer. Replaying a pure renderer into
// this clipped canvas bounds each SPI transaction to 5120 bytes. Core 0 owns it;
// render and transfer one strip, then return to clock/control processing.
class StripCanvas : public Adafruit_GFX {
public:
    static constexpr int kWidth = 320;
    static constexpr int kRows = 8;
    StripCanvas() : Adafruit_GFX(kWidth, 480) { setTextWrap(false); }

    void beginStrip(int y, uint16_t background) {
        top_ = y;
        std::fill(pixels_, pixels_ + kWidth * kRows, background);
    }
    uint16_t *pixels() { return pixels_; }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x >= 0 && x < kWidth && y >= top_ && y < top_ + kRows)
            pixels_[(y - top_) * kWidth + x] = color;
    }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
        const int left = std::max<int>(0, x);
        const int right = std::min<int>(kWidth, x + w);
        const int first = std::max<int>(top_, y);
        const int last = std::min<int>(top_ + kRows, y + h);
        if (left >= right || first >= last) return;
        for (int row = first; row < last; ++row)
            std::fill(pixels_ + (row - top_) * kWidth + left,
                      pixels_ + (row - top_) * kWidth + right, color);
    }
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
        fillRect(x, y, w, 1, color);
    }
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
        fillRect(x, y, 1, h, color);
    }
    void text(int x, int y, const char *value, uint16_t color, uint8_t size = 1) {
        setTextSize(size);
        setTextColor(color);
        setCursor(x, y);
        print(value);
    }

private:
    int top_ = 0;
    uint16_t pixels_[kWidth * kRows] = {};
};
