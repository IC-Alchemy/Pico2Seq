#pragma once
// Desktop replacement for the FastLED library.
//
// The LED-matrix code needs CRGB values (color math + theme palettes) and
// the FastLED controller object. Rendering to virtual pixels happens in
// desktop/native DisplayBridge, which snapshots ledMatrix's LED array for
// the GUI; FastLED.show() here is a no-op.

#include <stdint.h>
#include <cstddef>
#include <cstdlib>
#include "p2s_host_hooks.h"

struct CHSV
{
    uint8_t h, s, v;
    CHSV() : h(0), s(0), v(0) {}
    CHSV(uint8_t h_, uint8_t s_, uint8_t v_) : h(h_), s(s_), v(v_) {}
};

struct CRGB
{
    uint8_t r, g, b;

    enum HTMLColorCode : uint32_t
    {
        Black = 0x000000,
        White = 0xFFFFFF,
        Red = 0xFF0000,
        Green = 0x00FF00,
        Blue = 0x0000FF
    };

    CRGB() : r(0), g(0), b(0) {}
    constexpr CRGB(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}
    constexpr CRGB(uint32_t code)
        : r(static_cast<uint8_t>((code >> 16) & 0xFF)),
          g(static_cast<uint8_t>((code >> 8) & 0xFF)),
          b(static_cast<uint8_t>(code & 0xFF)) {}
    constexpr CRGB(HTMLColorCode code) : CRGB(static_cast<uint32_t>(code)) {}
    CRGB(const CHSV &hsv); // defined in shim_runtime.cpp

    CRGB &operator=(HTMLColorCode code)
    {
        *this = CRGB(static_cast<uint32_t>(code));
        return *this;
    }

    bool operator==(const CRGB &o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const CRGB &o) const { return !(*this == o); }

    // FastLED nscale8: scale each channel by scale/256 (255 = unity).
    CRGB &nscale8(uint8_t scale)
    {
        r = static_cast<uint8_t>((r * scale) >> 8);
        g = static_cast<uint8_t>((g * scale) >> 8);
        b = static_cast<uint8_t>((b * scale) >> 8);
        return *this;
    }

    uint8_t &operator[](uint8_t i) { return i == 0 ? r : (i == 1 ? g : b); }
    const uint8_t &operator[](uint8_t i) const { return i == 0 ? r : (i == 1 ? g : b); }
};

// Blend `overlay` into `existing` by amount/256 (255 replaces outright).
inline void nblend(CRGB &existing, const CRGB &overlay, uint8_t amount)
{
    if (amount == 255)
    {
        existing = overlay;
        return;
    }
    existing.r = static_cast<uint8_t>(existing.r + (((overlay.r - existing.r) * amount) >> 8));
    existing.g = static_cast<uint8_t>(existing.g + (((overlay.g - existing.g) * amount) >> 8));
    existing.b = static_cast<uint8_t>(existing.b + (((overlay.b - existing.b) * amount) >> 8));
}

inline CRGB blend(const CRGB &a, const CRGB &b, uint8_t amount)
{
    CRGB out = a;
    nblend(out, b, amount);
    return out;
}

// ---- Gradient palettes ----------------------------------------------------

typedef uint8_t TProgmemRGBGradientPalette_byte;
#define DEFINE_GRADIENT_PALETTE(name) const TProgmemRGBGradientPalette_byte name[] =

class CRGBPalette16
{
public:
    CRGB entries[16];

    CRGBPalette16()
    {
        for (auto &e : entries)
            e = CRGB(0, 0, 0);
    }

    // Expand a (index, r, g, b) gradient table (terminated by index 255)
    // into 16 evenly spread entries, matching FastLED's construction.
    CRGBPalette16(const TProgmemRGBGradientPalette_byte *gradient)
    {
        for (uint8_t slot = 0; slot < 16; ++slot)
        {
            const uint8_t pos = static_cast<uint8_t>((slot * 255UL) / 15UL);
            // Find the stops bracketing pos.
            uint8_t loIdx = 0, loR = 0, loG = 0, loB = 0;
            uint8_t hiIdx = 255, hiR = 0, hiG = 0, hiB = 0;
            for (uint8_t s = 0;; s += 4)
            {
                const uint8_t idx = gradient[s];
                if (idx >= pos)
                {
                    hiIdx = idx;
                    hiR = gradient[s + 1];
                    hiG = gradient[s + 2];
                    hiB = gradient[s + 3];
                    break;
                }
                loIdx = idx;
                loR = gradient[s + 1];
                loG = gradient[s + 2];
                loB = gradient[s + 3];
            }
            uint8_t r = loR, g = loG, b = loB;
            if (hiIdx > loIdx)
            {
                const uint8_t frac =
                    static_cast<uint8_t>(((pos - loIdx) * 255UL) / (hiIdx - loIdx));
                r = static_cast<uint8_t>(loR + (((hiR - loR) * frac) >> 8));
                g = static_cast<uint8_t>(loG + (((hiG - loG) * frac) >> 8));
                b = static_cast<uint8_t>(loB + (((hiB - loB) * frac) >> 8));
            }
            entries[slot] = CRGB(r, g, b);
        }
    }

    const CRGB &operator[](uint8_t i) const { return entries[i & 15]; }
};

enum TBlendType { NOBLEND = 0, LINEARBLEND = 1 };

inline uint8_t scale8_video(uint8_t i, uint8_t scale)
{
    uint8_t j = static_cast<uint8_t>(((i * scale) >> 8) + (i && scale ? 1 : 0));
    return j ? j : 1;
}

// Map a 0-255 index across the 16-entry palette with linear blending, then
// apply brightness like FastLED's ColorFromPalette.
inline CRGB ColorFromPalette(const CRGBPalette16 &pal, uint8_t index,
                             uint8_t brightness = 255,
                             TBlendType = LINEARBLEND)
{
    const uint8_t entry = static_cast<uint8_t>((index >> 4) & 0x0F);
    const uint8_t frac = static_cast<uint8_t>(index & 0x0F);
    const CRGB &a = pal.entries[entry];
    const CRGB &b = pal.entries[(entry + 1) & 15];
    const uint8_t amount = static_cast<uint8_t>(frac * 16 + frac / 2); // ~ frac/16
    CRGB out = blend(a, b, amount);
    if (brightness != 255)
    {
        out.r = scale8_video(out.r, brightness);
        out.g = scale8_video(out.g, brightness);
        out.b = scale8_video(out.b, brightness);
    }
    return out;
}

// Saturating channel add used by feedback overlays.
inline uint8_t qadd8(uint8_t a, uint8_t b)
{
    unsigned int s = static_cast<unsigned int>(a) + b;
    return static_cast<uint8_t>(s > 255 ? 255 : s);
}

inline CRGB &operator+=(CRGB &a, const CRGB &b)
{
    a.r = qadd8(a.r, b.r);
    a.g = qadd8(a.g, b.g);
    a.b = qadd8(a.b, b.b);
    return a;
}
inline CRGB operator+(const CRGB &a, const CRGB &b)
{
    CRGB t = a;
    t += b;
    return t;
}

inline uint8_t random8() { return static_cast<uint8_t>(rand() & 0xFF); }
inline uint8_t beatsin8(int, int lo = 0, int hi = 255, uint32_t = 0, uint32_t = 0)
{
    return static_cast<uint8_t>((lo + hi) / 2);
}
inline void fadeToBlackBy(CRGB *leds, uint16_t n, uint8_t fade)
{
    for (uint16_t i = 0; i < n; ++i)
    {
        leds[i].r = static_cast<uint8_t>((leds[i].r * (255 - fade)) >> 8);
        leds[i].g = static_cast<uint8_t>((leds[i].g * (255 - fade)) >> 8);
        leds[i].b = static_cast<uint8_t>((leds[i].b * (255 - fade)) >> 8);
    }
}
inline void fill_solid(CRGB *leds, uint16_t n, const CRGB &c)
{
    for (uint16_t i = 0; i < n; ++i)
        leds[i] = c;
}

// Chipset/ordering identifiers accepted by FastLED.addLeds<...>.
struct WS2812B
{
};
enum EOrder : uint8_t
{
    RGB = 0012,
    RBG = 0021,
    GRB = 0102,
    GBR = 0120,
    BRG = 0201,
    BGR = 0210
};

struct CFastLED
{
    CRGB *ledArray = nullptr;
    int ledCount = 0;

    template <typename Chipset, uint8_t DataPin, EOrder Order>
    int addLeds(CRGB *data, int n = -1)
    {
        (void)Chipset{};
        (void)DataPin;
        (void)Order;
        ledArray = data;
        ledCount = n;
        return 0;
    }
    void setBrightness(uint8_t) {}
    void setCorrection(const CRGB &) {}
    // GUI poll point: snapshot the frame exactly where the firmware pushes
    // it to the WS2812 strip.
    void show() { p2s::host::onFastLedShow(ledArray, static_cast<size_t>(ledCount)); }
    void clear(bool write = false)
    {
        if (ledArray && ledCount > 0)
            for (int i = 0; i < ledCount; ++i)
                ledArray[i] = CRGB::Black;
        if (write)
            show();
    }
    void showColor(const CRGB &) {}
    uint16_t count() { return static_cast<uint16_t>(ledCount); }
};

inline CFastLED FastLED;
