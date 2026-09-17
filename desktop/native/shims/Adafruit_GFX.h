#pragma once
// Desktop replacement for the Adafruit_GFX graphics core.
//
// Implements the exact API surface oled.cpp uses (Print-derived text with
// the classic 5x7 glcdfont, cursor/size/color state, rectangles, circles,
// fast hlines) drawing into subclass framebuffers. Geometry and glyph
// rendering follow the upstream algorithms (Adafruit_GFX, BSD license) so
// the OLED canvas matches the hardware display.

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "glcdfont.h"

#define GFX_NOT_DEFINED -1

// ---- Arduino Print base ---------------------------------------------------
class Print
{
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size)
    {
        size_t n = 0;
        while (size--)
            n += write(*buffer++);
        return n;
    }

    size_t print(const String &s) { return write(reinterpret_cast<const uint8_t *>(s.c_str()), s.size()); }
    size_t print(const char s[]) { return write(reinterpret_cast<const uint8_t *>(s), std::strlen(s)); }
    size_t print(char c) { return write(static_cast<uint8_t>(c)); }
    size_t print(unsigned char n, int = DEC) { return printNumber(static_cast<unsigned long>(n), 10); }
    size_t print(int n, int = DEC) { return printNumber(n < 0 ? 0UL - static_cast<unsigned long>(n) : static_cast<unsigned long>(n), 10, n < 0); }
    size_t print(unsigned int n, int = DEC) { return printNumber(n, 10); }
    size_t print(long n, int = DEC) { return printNumber(n < 0 ? 0UL - static_cast<unsigned long>(n) : static_cast<unsigned long>(n), 10, n < 0); }
    size_t print(unsigned long n, int = DEC) { return printNumber(n, 10); }
    size_t print(double n, int digits = 2) { return printFloat(n, digits); }

    size_t println() { return write('\r') + write('\n'); }
    template <typename T>
    size_t println(T value) { return print(value) + println(); }

private:
    size_t printNumber(unsigned long n, uint8_t base, bool negative = false)
    {
        char buf[8 * sizeof(long) + 1];
        char *str = &buf[sizeof(buf) - 1];
        *str = '\0';
        if (base < 2) base = 10;
        do
        {
            char c = static_cast<char>(n % base);
            n /= base;
            *--str = c < 10 ? c + '0' : c + 'A' - 10;
        } while (n);
        size_t written = 0;
        if (negative)
            written += write('-');
        return written + write(reinterpret_cast<const uint8_t *>(str), std::strlen(str));
    }

    size_t printFloat(double number, uint8_t digits)
    {
        size_t n = 0;
        if (number != number) return print("nan");
        if (number > 4294967040.0) return print("ovf");
        if (number < -4294967040.0) return print("ovf");
        if (number < 0.0) { n += print('-'); number = -number; }
        double rounding = 0.5;
        for (uint8_t i = 0; i < digits; ++i) rounding /= 10.0;
        number += rounding;
        unsigned long intPart = static_cast<unsigned long>(number);
        double remainder = number - static_cast<double>(intPart);
        n += print(intPart);
        if (digits > 0) n += print('.');
        while (digits-- > 0)
        {
            remainder *= 10.0;
            unsigned int toPrint = static_cast<unsigned int>(remainder);
            n += print(toPrint);
            remainder -= toPrint;
        }
        return n;
    }
};

// ---- GFX core --------------------------------------------------------------
class Adafruit_GFX : public Print
{
public:
    Adafruit_GFX(int16_t w, int16_t h)
        : _width(w), _height(h),
          textsize_x(1), textsize_y(1), textcolor(1), textbgcolor(0),
          cursor_x(0), cursor_y(0), wrap(true) {}

    virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;

    int16_t width() const { return _width; }
    int16_t height() const { return _height; }
    uint8_t getRotation() const { return rotation; }

    void setCursor(int16_t x, int16_t y) { cursor_x = x; cursor_y = y; }
    int16_t getCursorX() const { return cursor_x; }
    int16_t getCursorY() const { return cursor_y; }
    void setTextSize(uint8_t s) { textsize_x = textsize_y = (s > 0 ? s : 1); }
    void setTextWrap(bool w) { wrap = w; }
    void setTextColor(uint16_t c) { textcolor = c; }
    void setTextColor(uint16_t c, uint16_t bg)
    {
        textcolor = c;
        textbgcolor = bg;
    }

    // Text output through the Print interface.
    size_t write(uint8_t c) override
    {
        if (c == '\n')
        {
            cursor_y += textsize_y * 8;
        }
        else if (c != '\r')
        {
            drawChar(cursor_x, cursor_y, static_cast<char>(c), textcolor, textbgcolor,
                     static_cast<uint8_t>(textsize_x));
            cursor_x += textsize_x * 6;
            if (wrap && cursor_x > _width - textsize_x * 6)
            {
                cursor_y += textsize_y * 8;
                cursor_x = 0;
            }
        }
        return 1;
    }

    // Classic 5x7 glyph renderer (upstream drawChar algorithm).
    void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg,
                  uint8_t size)
    {
        if (x > _width || y > _height || (x + 6 * size - 1) < 0 || (y + 8 * size - 1) < 0)
            return;
        for (int8_t i = 0; i < 5; i++)
        {
            uint8_t line = p2s_glcdfont[c * 5 + i];
            for (int8_t j = 0; j < 8; j++)
            {
                uint8_t drawColor = (line & 1) ? color : bg;
                if (drawColor != color && bg == 0xFFFF) continue; // transparent bg sentinel unused here
                for (uint8_t a = 0; a < size; a++)
                    for (uint8_t b = 0; b < size; b++)
                        drawPixel(x + i * size + a, y + j * size + b, drawColor);
                line >>= 1;
            }
        }
        // 6th column: background (spacing).
        if (bg != color && bg != 0xFFFF)
            fillRect(x + 5 * size, y, size, 8 * size, bg);
    }

    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color)
    {
        for (int16_t i = 0; i < w; ++i)
            drawPixel(x + i, y, color);
    }
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color)
    {
        for (int16_t i = 0; i < h; ++i)
            drawPixel(x, y + i, color);
    }
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
    {
        drawFastHLine(x, y, w, color);
        drawFastHLine(x, y + h - 1, w, color);
        drawFastVLine(x, y, h, color);
        drawFastVLine(x + w - 1, y, h, color);
    }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
    {
        for (int16_t j = y; j < y + h; ++j)
            for (int16_t i = x; i < x + w; ++i)
                drawPixel(i, j, color);
    }
    void fillScreen(uint16_t color) { fillRect(0, 0, _width, _height, color); }

    void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
    {
        int16_t f = 1 - r;
        int16_t ddF_x = 1, ddF_y = -2 * r;
        int16_t x = 0, y = r;
        drawPixel(x0, y0 + r, color);
        drawPixel(x0, y0 - r, color);
        drawPixel(x0 + r, y0, color);
        drawPixel(x0 - r, y0, color);
        while (x < y)
        {
            if (f >= 0)
            {
                y--;
                ddF_y += 2;
                f += ddF_y;
            }
            x++;
            ddF_x += 2;
            f += ddF_x;
            drawPixel(x0 + x, y0 + y, color);
            drawPixel(x0 - x, y0 + y, color);
            drawPixel(x0 + x, y0 - y, color);
            drawPixel(x0 - x, y0 - y, color);
            drawPixel(x0 + y, y0 + x, color);
            drawPixel(x0 - y, y0 + x, color);
            drawPixel(x0 + y, y0 - x, color);
            drawPixel(x0 - y, y0 - x, color);
        }
    }
    void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
    {
        drawFastVLine(x0, y0 - r, 2 * r + 1, color);
        int16_t f = 1 - r;
        int16_t ddF_x = 1, ddF_y = -2 * r;
        int16_t x = 0, y = r;
        while (x < y)
        {
            if (f >= 0)
            {
                y--;
                ddF_y += 2;
                f += ddF_y;
            }
            x++;
            ddF_x += 2;
            f += ddF_x;
            drawFastVLine(x0 + x, y0 - y, 2 * y + 1, color);
            drawFastVLine(x0 - x, y0 - y, 2 * y + 1, color);
            drawFastVLine(x0 + y, y0 - x, 2 * x + 1, color);
            drawFastVLine(x0 - y, y0 - x, 2 * x + 1, color);
        }
    }
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
    {
        int16_t steep = abs(y1 - y0) > abs(x1 - x0);
        if (steep) { int16_t t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
        if (x0 > x1) { int16_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        int16_t dx = x1 - x0, dy = abs(y1 - y0);
        int16_t err = dx / 2;
        int16_t ystep = y0 < y1 ? 1 : -1;
        for (; x0 <= x1; x0++)
        {
            if (steep) drawPixel(y0, x0, color);
            else drawPixel(x0, y0, color);
            err -= dy;
            if (err < 0) { y0 += ystep; err += dx; }
        }
    }
    void drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h,
                    uint16_t color)
    {
        for (int16_t j = 0; j < h; ++j)
            for (int16_t i = 0; i < w; ++i)
            {
                if (pgm_read_byte(bitmap + j * ((w + 7) / 8) + i / 8) & (128 >> (i & 7)))
                    drawPixel(x + i, y + j, color);
            }
    }

private:
    int16_t _width, _height;
    uint8_t rotation = 0;
protected:
    int16_t cursor_x, cursor_y;
    uint16_t textcolor, textbgcolor;
    uint8_t textsize_x, textsize_y;
    bool wrap;
};
