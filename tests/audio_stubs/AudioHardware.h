#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

// Deterministic hardware doubles for the real audio pool/driver code. These
// model ownership and IRQ completion, not physical DMA timing or I2S clocks.
using uint = unsigned;
#define __isr
#define __packed __attribute__((packed))
#define __time_critical_func(name) name
#define AUDIO_CONCAT_(a, b) a##b
#define __CONCAT(a, b) AUDIO_CONCAT_(a, b)
#define CU_REGISTER_DEBUG_PINS(...)
#define DEBUG_PINS_XOR(...)
#define DEBUG_PINS_SET(...)
#define DEBUG_PINS_CLR(...)
inline void __mem_fence_release() {}
inline void __sev() {}
inline void __wfe() { std::abort(); } // A blocking wait is a test failure.
inline void panic(const char*) { std::abort(); }
inline uint get_core_num() { return 1; }

inline bool audioIrqsDisabled = false;
inline uint32_t save_and_disable_interrupts() {
    const bool previous = audioIrqsDisabled;
    audioIrqsDisabled = true;
    return previous;
}
inline void restore_interrupts(uint32_t previous) { audioIrqsDisabled = previous; }
struct spin_lock_t { bool held = false; };
inline spin_lock_t audioLocks[32];
inline spin_lock_t* spin_lock_init(uint id) { return &audioLocks[id]; }
inline uint32_t spin_lock_blocking(spin_lock_t* lock) {
    assert(audioIrqsDisabled && !lock->held);
    lock->held = true;
    return 0;
}
inline void spin_unlock(spin_lock_t* lock, uint32_t) {
    assert(audioIrqsDisabled && lock->held);
    lock->held = false;
}

struct WriteOneToClear {
    uint32_t bits = 0;
    operator uint32_t() const { return bits; }
    void operator=(uint32_t mask) { bits &= ~mask; }
};
struct PioHardware { uint32_t txf[4]{}; WriteOneToClear fdebug; };
using PIO = PioHardware*;
inline PioHardware audioPio;
inline PIO pio1 = &audioPio;
constexpr uint PIO_FDEBUG_TXSTALL_LSB = 24;
constexpr uint GPIO_FUNC_PIO1 = 1, DREQ_PIO1_TX0 = 8;
constexpr uint PIO_FIFO_JOIN_TX = 1;
struct pio_sm_config {};
struct pio_program { const uint16_t* instructions; uint length; int origin; };
inline pio_sm_config pio_get_default_sm_config() { return {}; }
inline void sm_config_set_wrap(pio_sm_config*, uint, uint) {}
inline void sm_config_set_sideset(pio_sm_config*, uint, bool, bool) {}
inline void sm_config_set_out_pins(pio_sm_config*, uint, uint) {}
inline void sm_config_set_sideset_pins(pio_sm_config*, uint) {}
inline void sm_config_set_out_shift(pio_sm_config*, bool, bool, uint) {}
inline void sm_config_set_fifo_join(pio_sm_config*, uint) {}
inline void pio_sm_init(PIO, uint, uint, pio_sm_config*) {}
inline void pio_sm_set_pindirs_with_mask(PIO, uint, uint32_t, uint32_t) {}
inline void pio_sm_set_pins(PIO, uint, uint32_t) {}
inline void pio_sm_exec(PIO, uint, uint) {}
inline uint pio_encode_jmp(uint offset) { return offset; }
inline void gpio_set_function(uint, uint) {}
inline void pio_sm_claim(PIO, uint) {}
inline int pio_claim_unused_sm(PIO, bool) { return 0; }
inline void pio_sm_unclaim(PIO, uint) {}
inline uint pio_add_program(PIO, const pio_program*) { return 0; }
inline void pio_sm_set_clkdiv_int_frac(PIO, uint, uint16_t, uint8_t) {}
inline void pio_sm_set_enabled(PIO, uint, bool) {}
constexpr uint clk_sys = 0;
inline uint32_t clock_get_hz(uint) { return 150000000; }

constexpr uint DMA_SIZE_16 = 1, DMA_SIZE_32 = 2, DMA_IRQ_0 = 0;
constexpr uint PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY = 0;
constexpr uint PICO_HIGHEST_IRQ_PRIORITY = 0;
struct dma_channel_config { bool increment = true; bool highPriority = false; };
inline dma_channel_config audioDmaConfig;
inline const void* audioDmaSource = nullptr;
inline uint32_t audioDmaCount = 0;
inline bool audioDmaPending = false;
inline void (*audioDmaHandler)() = nullptr;
inline int dma_claim_unused_channel(bool) { return 3; }
inline void dma_channel_claim(uint) {}
inline void dma_channel_unclaim(uint) {}
inline dma_channel_config dma_channel_get_default_config(uint) { return {}; }
inline void channel_config_set_high_priority(dma_channel_config* c, bool value) { c->highPriority = value; }
inline void channel_config_set_dreq(dma_channel_config*, uint) {}
inline void channel_config_set_transfer_data_size(dma_channel_config*, uint) {}
inline void dma_channel_configure(uint, dma_channel_config* c, void*, const void*, uint32_t, bool) { audioDmaConfig = *c; }
inline void irq_add_shared_handler(uint, void (*handler)(), uint) { audioDmaHandler = handler; }
inline void irq_set_exclusive_handler(uint, void (*handler)()) { audioDmaHandler = handler; }
inline void irq_set_priority(uint, uint) {}
inline void dma_irqn_set_channel_enabled(uint, uint, bool) {}
inline bool dma_irqn_get_channel_status(uint, uint) { return audioDmaPending; }
inline void dma_irqn_acknowledge_channel(uint, uint) { audioDmaPending = false; }
inline dma_channel_config dma_get_channel_config(uint) { return audioDmaConfig; }
inline void channel_config_set_read_increment(dma_channel_config* c, bool value) { c->increment = value; }
inline void dma_channel_set_config(uint, dma_channel_config* c, bool) { audioDmaConfig = *c; }
inline void dma_channel_transfer_from_buffer_now(uint, const void* source, uint32_t count) {
    audioDmaSource = source;
    audioDmaCount = count;
}
inline void irq_set_enabled(uint, bool) {}
