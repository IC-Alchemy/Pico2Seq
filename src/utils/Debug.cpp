#include "Debug.h"

#if AUG_DEBUG_COMPILED

namespace Debug {

static volatile bool s_enabled = true;
static volatile Level s_level = Level::Info;

// Optional prefix per level for readability
static inline const __FlashStringHelper* levelTag(Level lvl) {
    switch (lvl) {
        case Level::Error:  return F("[E] ");
        case Level::Warn:   return F("[W] ");
        case Level::Info:   return F("[I] ");
        case Level::Verbose:return F("[V] ");
        default:            return F("");
    }
}

void begin(unsigned long baud) {
    // Non-blocking: if Serial is already initialized, do nothing
    if (!Serial) {
        Serial.begin(baud);
        // Give USB a brief moment if needed; caller can add delays if required
    }
}

void setEnabled(bool e) { s_enabled = e; }
bool isEnabled() { return s_enabled; }

void setLevel(Level l) { s_level = l; }
Level getLevel() { return s_level; }

void vlogf(Level lvl, const char* fmt, va_list args) {
    if (!s_enabled) return;
    if ((uint8_t)lvl > (uint8_t)s_level) return;

    // Fixed buffer to avoid heap; adjust size as needed
    char buf[160];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    if (n < 0) return; // format error

    // Write level tag then message, end with newline
    const __FlashStringHelper* tag = levelTag(lvl);
    if (tag) Serial.print(tag);
    Serial.println(buf);
}

void logf(Level lvl, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlogf(lvl, fmt, args);
    va_end(args);
}

} // namespace Debug

#else
// Compiled-out stubs to ensure zero code size when not compiled in
namespace Debug {
void begin(unsigned long) {}
void setEnabled(bool) {}
bool isEnabled() { return false; }
void setLevel(Level) {}
Level getLevel() { return Level::Error; }
void vlogf(Level, const char*, va_list) {}
void logf(Level, const char*, ...) {}
} // namespace Debug
#endif

