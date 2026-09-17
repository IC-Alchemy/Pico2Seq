#pragma once
// Desktop replacement for the Pico SDK spinlock API (single-threaded host
// equivalents, same shape as tests/stubs/pico/sync.h).

#include <stdint.h>
#include <cstddef>

typedef unsigned int uint;
typedef void spin_lock_t;

inline uint spin_lock_claim_unused(bool) { return 0; }
inline spin_lock_t *spin_lock_init(uint) { return nullptr; }
inline uint32_t spin_lock_blocking(spin_lock_t *) { return 0; }
inline void spin_unlock(spin_lock_t *, uint32_t) {}
inline void spin_lock_unsafe_blocking(spin_lock_t *) {}
inline void spin_lock_unsafe_unblocking(spin_lock_t *) {}
