#include "DisplayBridge.h"
#include "p2s_host_hooks.h"

#include <FastLED.h>

#include <cstring>
#include <mutex>

namespace
{
std::mutex g_displayMutex;
uint8_t g_ledSnapshot[p2s::display::kLedCount * 3] = {};
uint8_t g_oledSnapshot[p2s::display::kOledBytes] = {};
uint64_t g_ledSerial = 0;
uint64_t g_oledSerial = 0;
} // namespace

namespace p2s
{
namespace host
{
void onFastLedShow(const void *leds, size_t count)
{
    const CRGB *src = static_cast<const CRGB *>(leds);
    if (!src || count == 0)
        return;
    std::lock_guard<std::mutex> lock(g_displayMutex);
    const size_t n = count < p2s::display::kLedCount ? count : p2s::display::kLedCount;
    for (size_t i = 0; i < n; ++i)
    {
        g_ledSnapshot[i * 3 + 0] = src[i].r;
        g_ledSnapshot[i * 3 + 1] = src[i].g;
        g_ledSnapshot[i * 3 + 2] = src[i].b;
    }
    ++g_ledSerial;
}

void onOledDisplay(const uint8_t *buffer1024)
{
    if (!buffer1024)
        return;
    std::lock_guard<std::mutex> lock(g_displayMutex);
    std::memcpy(g_oledSnapshot, buffer1024, p2s::display::kOledBytes);
    ++g_oledSerial;
}
} // namespace host

namespace display
{
uint64_t copyLeds(uint8_t *outRgb)
{
    std::lock_guard<std::mutex> lock(g_displayMutex);
    std::memcpy(outRgb, g_ledSnapshot, kLedCount * 3);
    return g_ledSerial;
}

uint64_t copyOled(uint8_t *outBytes)
{
    std::lock_guard<std::mutex> lock(g_displayMutex);
    std::memcpy(outBytes, g_oledSnapshot, kOledBytes);
    return g_oledSerial;
}

uint64_t ledFrameSerial()
{
    std::lock_guard<std::mutex> lock(g_displayMutex);
    return g_ledSerial;
}

uint64_t oledFrameSerial()
{
    std::lock_guard<std::mutex> lock(g_displayMutex);
    return g_oledSerial;
}
} // namespace display
} // namespace p2s
