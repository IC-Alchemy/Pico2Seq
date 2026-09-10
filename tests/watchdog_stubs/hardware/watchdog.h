#pragma once
#include <cstdint>

struct WatchdogTestHardware { uint32_t scratch[8]{}; };
inline WatchdogTestHardware fakeWatchdog;
inline auto *watchdog_hw = &fakeWatchdog;
inline bool fakeWatchdogReset = false;
inline bool fakeUploadReset = false;
inline uint32_t watchdogFeeds = 0;
inline bool watchdog_caused_reboot() { return fakeWatchdogReset || fakeUploadReset; }
inline bool watchdog_enable_caused_reboot() { return fakeWatchdogReset; }
inline void watchdog_update() { ++watchdogFeeds; }
inline void watchdog_enable(uint32_t, bool) {}
