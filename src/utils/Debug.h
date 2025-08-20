#pragma once

// Lightweight Arduino-style debug utility with runtime toggle and log levels.
// - Zero cost when compiled out via AUG_DEBUG_COMPILED=0
// - Runtime enable/disable and level control when compiled in
// - Avoids dynamic allocation; suitable for microcontrollers

#include <Arduino.h>
#include <stdarg.h>
#include <stdint.h>

#ifndef AUG_DEBUG_COMPILED
#define AUG_DEBUG_COMPILED 1
#endif

namespace Debug {
    enum class Level : uint8_t { Error = 1, Warn = 2, Info = 3, Verbose = 4 };

    // Initialize Serial if needed (non-blocking if already begun)
    void begin(unsigned long baud = 115200);

    // Runtime control
    void setEnabled(bool e);
    bool isEnabled();

    void setLevel(Level l);
    Level getLevel();

    // Formatted logging
    void logf(Level lvl, const char* fmt, ...);
    void vlogf(Level lvl, const char* fmt, va_list args);
}

#if AUG_DEBUG_COMPILED
    #define DBG_ERROR(fmt, ...)   do { if (Debug::isEnabled() && (uint8_t)Debug::getLevel() >= (uint8_t)Debug::Level::Error)   Debug::logf(Debug::Level::Error,   fmt, ##__VA_ARGS__); } while(0)
    #define DBG_WARN(fmt, ...)    do { if (Debug::isEnabled() && (uint8_t)Debug::getLevel() >= (uint8_t)Debug::Level::Warn)    Debug::logf(Debug::Level::Warn,    fmt, ##__VA_ARGS__); } while(0)
    #define DBG_INFO(fmt, ...)    do { if (Debug::isEnabled() && (uint8_t)Debug::getLevel() >= (uint8_t)Debug::Level::Info)    Debug::logf(Debug::Level::Info,    fmt, ##__VA_ARGS__); } while(0)
    #define DBG_VERBOSE(fmt, ...) do { if (Debug::isEnabled() && (uint8_t)Debug::getLevel() >= (uint8_t)Debug::Level::Verbose) Debug::logf(Debug::Level::Verbose, fmt, ##__VA_ARGS__); } while(0)
#else
    #define DBG_ERROR(fmt, ...)   do { (void)0; } while(0)
    #define DBG_WARN(fmt, ...)    do { (void)0; } while(0)
    #define DBG_INFO(fmt, ...)    do { (void)0; } while(0)
    #define DBG_VERBOSE(fmt, ...) do { (void)0; } while(0)
#endif

