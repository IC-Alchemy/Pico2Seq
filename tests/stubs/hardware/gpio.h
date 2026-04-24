#pragma once
#include <stdint.h>

// Pico SDK GPIO stubs — no-ops for host testing
inline void gpio_init(uint32_t) {}
inline void gpio_set_dir(uint32_t, bool) {}
inline void gpio_put(uint32_t, bool) {}
inline bool gpio_get(uint32_t) { return false; }
inline void gpio_pull_up(uint32_t) {}
inline void gpio_pull_down(uint32_t) {}
