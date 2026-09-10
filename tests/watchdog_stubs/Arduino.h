#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

inline uint32_t fakeMillis = 0;
inline uint32_t millisCalls = 0;
inline uint32_t millis() { ++millisCalls; return fakeMillis; }

struct WatchdogTestSerial
{
    bool connected = false;
    std::string output;
    explicit operator bool() const { return connected; }
    void printf(const char *format, ...)
    {
        char buffer[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        output += buffer;
    }
};
inline WatchdogTestSerial Serial;
