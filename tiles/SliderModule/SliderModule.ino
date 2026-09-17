/*
 * I2C Slide Potentiometer module — Alchemy Modular UI protocol v2 tile
 * MCU: PY32F030F28U6TR — same PCB and MCU as the button tile
 *
 * Wiring (per schematic):
 *   PA0..PA3  - slider wipers (analog in, VCC-referenced)
 *   PA4..PA6  - momentary buttons 0..2 (active-low, internal pull-ups)
 *   PF0       - momentary button 3 (active-low)
 *   PB5       - address strap (1k to GND / 1k to 3V3 / floating)
 *   PB6       - I2C1 SCL (AF6)
 *   PB7       - I2C1 SDA (AF6)
 * SCL/SDA need external pull-ups to 3V3 (4.7k typical).
 *
 * Implements the v2 register protocol, TYPE_ID 0x01 (spec §7.1):
 * identity block 0x00-0x19, STATUS/DATA/SUM from 0x20 with DATA_LEN 11
 * (four uint16 little-endian 12-bit fader readings followed by the shared
 * three-byte button block — levels, sticky pressed, sticky released —
 * byte-identical to ButtonTile DATA, so one hub parser serves both),
 * config 0x40-0x42, LED stubs 0x50-0x53, SOFT_CMD 0x7F.
 *
 * Address: 0x08 + strap offset, the Slider registry block from spec §4.3.
 * Strap offsets 0/1/2 therefore answer at 0x08/0x09/0x0A. Each slider tile
 * on one bank must use a different strap state; button tiles occupy their
 * separate 0x0B-0x0D block and cannot collide with this module.
 *
 * Sampling: every sweep is 4 ms (hub Tier B). Each sweep spreads four
 * per-channel conversions across it (~1 ms apart) and publishes their
 * average — noise reduction only, per spec §7.5. CFG_FILTER picks the
 * published-value deadband (low nibble) and slew ceiling (high nibble);
 * defaults are the mildest possible (1 LSB hysteresis, unlimited slew):
 * a tile that ships aggressive smoothing erases motion the hub can never
 * recover. CFG_DEBOUNCE sets the button window (default 4 ms).
 *
 * ---------------------------------------------------------------------
 * Why this does not use Wire
 * ---------------------------------------------------------------------
 * The PY32Duino core's Wire slave path (libraries/Wire/src/utility/twi.c
 * driving the I2Cv1-style HAL in py32f0xx_hal_i2c.c) wedges the
 * peripheral permanently on a write-then-read sequence. The peripheral
 * ends up with ADDR or BTF asserted and never cleared, which holds SCL
 * low forever: every later transaction hard-times-out and only a power
 * cycle recovers. The failure is in the HAL slave "listen mode" state
 * machine, not in this application, so slave duty is handled here by a
 * small register-level ISR instead. See I2C1_IRQHandler() below.
 *
 * Do not add #include <Wire.h> or Serial calls: keeping twi.c out of the
 * link avoids a duplicate I2C1_IRQHandler symbol, and PF0/PF1 are this
 * core's default serial pins while PF0 carries button 3 here.
 *
 * The independent watchdog is armed as a last-resort backstop: if the
 * peripheral ever does stall the CPU in an interrupt storm, the part
 * reboots in ~250 ms instead of staying dead until it is power cycled.
 * Set USE_IWDG to 0 to disable it.
 */

#include <string.h>

#define USE_IWDG 1

// Every tile on a bank must be programmed for the same bus rate as the
// master drives it. ClockSpeed feeds CR2.FREQ, CCR (including the Fm/Sm
// select bit) and TRISE; a tile left at 100000 on a bank the hub clocks at
// 400 kHz is configured for standard-mode timing while being driven at fast
// mode, and stalls. Change this in lockstep with the hub's bus clock
// (kTileBusFrequencyHz in the Pico2Seq firmware) and with every other tile.
static const uint32_t kBusClockHz = 400000;

// Bus is BUSY this long with no interrupt activity at all -> something is
// holding the line or the peripheral has wedged; rebuild it. Far longer than
// any legitimate transaction (13 bytes at 400 kHz is ~300 us) and far shorter
// than the IWDG, which stays the backstop for a genuinely hung CPU.
#define I2C_STUCK_MS 50

// Where an out-of-range register pointer parks. WHO_AM_I is read-only and
// handleRegisterWrite() drains writes to it. Clamping to REG_MAP_SIZE-1
// instead would park the pointer on REG_SOFT_CMD (0x7F), where every stray
// payload byte would be dispatched as a soft command.
#define REG_PARK 0x00

#define PIN_SCL PB6   // I2C1 SCL, AF6
#define PIN_SDA PB7   // I2C1 SDA, AF6
#define PIN_STRAP PB5 // address strap, tri-state digital probe only

#define NUM_CHANNELS 4
const uint8_t sliderPins[NUM_CHANNELS] = { PA0, PA1, PA2, PA3 };

#define NUM_BUTTONS 4
const uint8_t buttonPins[NUM_BUTTONS] = { PA4, PA5, PA6, PF0 };
#define BUTTON_PIN_MODE      INPUT_PULLUP
#define BUTTON_PRESSED_LEVEL LOW

/*
 * Protocol constants — local mirror of AlchemyProto.h (hub repo). This
 * sketch must build standalone under arduino-cli and cannot reach that
 * library path; keep this block byte-identical to the header (spec v2).
 */
namespace Proto {
const uint8_t WHO_AM_I_MAGIC      = 0x5A;
const uint8_t PROTO_VER_V2        = 0x02;
const uint8_t TYPE_SLIDER         = 0x01;
const uint8_t ADDR_BASE           = 0x08;  // 0x08, 0x09, 0x0A

enum Reg : uint8_t {
    REG_WHO_AM_I    = 0x00,
    REG_TYPE_ID     = 0x01,
    REG_PROTO_VER   = 0x02,
    REG_FW_VER      = 0x03,  // 2 bytes, major then minor
    REG_HW_REV      = 0x05,
    REG_ADDR_OFFSET = 0x06,
    REG_CAPS        = 0x07,  // 2 bytes LE
    REG_UID         = 0x09,  // 12 bytes
    REG_DECLARED_MA = 0x15,  // 2 bytes LE
    REG_LED_COUNT   = 0x17,
    REG_LED_TIER    = 0x18,
    REG_DATA_LEN    = 0x19,

    REG_STATUS      = 0x20,
    REG_DATA        = 0x21,  // DATA_LEN bytes
    // SUM sits at REG_DATA + DATA_LEN

    REG_CFG_RATE     = 0x40,
    REG_CFG_FILTER   = 0x41,  // bits 0-3 deadband idx, bits 4-7 slew idx
    REG_CFG_DEBOUNCE = 0x42,  // ms

    REG_LED_BRIGHTNESS = 0x50,
    REG_LED_SET        = 0x51,
    REG_LED_ANIMATE    = 0x52,
    REG_LED_CLEAR      = 0x53,

    REG_SOFT_CMD       = 0x7F,
};

enum SoftCmd : uint8_t {
    CMD_RESET        = 0x01,
    CMD_REREAD_STRAP = 0x02,
    CMD_BOOTLOADER   = 0xA5,  // rejected: CAP_BOOTLOADER not declared;
                              // bootloader pins carry fader wipers (§5.5)
};

enum Caps : uint16_t {
    CAP_ANALOG_IN  = 1u << 0,
    CAP_DIGITAL_IN = 1u << 1,
};

inline uint8_t cfgFilter(uint8_t deadbandIdx, uint8_t slewIdx) {
    return (uint8_t)((deadbandIdx & 0x0F) | ((slewIdx & 0x0F) << 4));
}

// Slider 4x + Button 4x DATA layout, §7.1. Bytes 8..10 are byte-identical to
// the standalone ButtonTile DATA so one hub-side parser serves both types.
namespace SliderTile {
const uint8_t DATA_LEN       = 11;
const uint8_t D_FADER        = 0;  // 4 x uint16 little-endian, 12-bit right-aligned
const uint8_t D_BTN_LEVEL    = 8;  // bit n = 1 while button n is held
const uint8_t D_BTN_PRESSED  = 9; // sticky pressed-since-last-read, clear on read
const uint8_t D_BTN_RELEASED = 10; // sticky released-since-last-read, clear on read
}

inline uint8_t regSum(uint8_t dataLen) { return (uint8_t)(0x21 + dataLen); }

inline uint8_t frameSum(uint8_t status, const uint8_t* data, uint8_t len) {
    uint8_t s = status;
    for (uint8_t i = 0; i < len; ++i) s = (uint8_t)(s + data[i]);
    return s;
}
}  // namespace Proto

// One up/down pull probe pair on the strap pin.
typedef struct {
  uint8_t offset;  // 0, 1 or 2
  bool    valid;   // false if up/down levels contradict every known wiring
} StrapPair;

#define FW_VER_MAJOR 0x01
#define FW_VER_MINOR 0x04  // 1.04: 400 kHz bus, stale-latch reclaim, ISR hardening

// --- Config page defaults ---------------------------------------------------
// Conditioning defaults must be conservative (§7.1): index 0 everywhere means
// 1 LSB of published-value hysteresis (just enough to stop single-count ADC
// dither) and an unlimited slew ceiling. Nothing steeper unless the hub asks.
#define CFG_FILTER_DEFAULT      Proto::cfgFilter(0, 0)
#define CFG_DEBOUNCE_DEFAULT_MS 4  // Tier B poll is 4 ms: no extra press latency
#define SWEEP_INTERVAL_MS 4

// Deadband lookup by CFG_FILTER low nibble (& 3), in ADC counts.
static const uint8_t kDeadbandCounts[4] = { 1, 2, 4, 8 };
// Slew ceiling per sweep by high nibble (& 3), in counts; 0 = unlimited.
static const uint8_t kSlewPerSweep[4] = { 0, 24, 12, 6 };

// Four evenly spread sub-rounds per 4 ms sweep; every channel converts once
// per sub-round, so all four share equal averaging weight.
#define ADC_SUBSAMPLES      4
#define SUBSTEP_INTERVAL_US 1000UL

// HW_REV is a compile-time constant for this board revision.
#define HW_REV 0x01

// PY32F030 factory unique ID, 128 B region at 0x1FFF0E00
// (PY32F030 Reference Manual V1.6, memory map). First 12 bytes used.
// Overridable so a host harness can point it at a buffer instead of faulting
// on an address that only exists on the part.
#ifndef PY32F030_UID_BASE
#define PY32F030_UID_BASE 0x1FFF0E00UL
#endif

#define REG_MAP_SIZE 128
static uint8_t regMap[REG_MAP_SIZE];  // sparse map, undefined offsets stay 0x00

// --- Live samples ------------------------------------------------------------
static uint16_t rawValue[NUM_CHANNELS]       = {};  // averaged ADC input per sweep
static uint16_t publishedValue[NUM_CHANNELS] = {};  // after deadband/slew
static uint32_t sampleAccum[NUM_CHANNELS]    = {};
static uint8_t  sampleCount                  = 0;

static uint8_t stableButtons = 0;
static uint8_t buttonDebounceCount[NUM_BUTTONS] = {};

// Address strap result. strapStable goes false if the tri-state probe never
// saw 8 consecutive agreeing pairs; surfaced via STATUS.LOCAL_FAULT.
static uint8_t addrOffset  = 2;  // floating default
static bool    strapStable = true;

// Defined below, called from the ISR's receive path above it. Declared here
// rather than relying on the .ino preprocessor to synthesise a prototype for
// a static function.
static void handleRegisterWrite(uint8_t reg, uint8_t value);

// I2C slave state, touched only from the ISR.
static volatile uint8_t regPointer  = 0;      // set by a pointer write
static volatile uint8_t txIndex     = 0;      // read cursor for the current read

// --- Double-buffered snapshot ------------------------------------------------
// The sample loop builds a complete STATUS+DATA+SUM frame in the back buffer
// and publishes it with a single index flip — no noInterrupts() around any
// multi-byte publish (§5.6). The ISR latches which buffer to serve on the
// ADDR-match event and keeps serving that buffer for the whole transaction,
// so a sweep landing mid-read can never tear a frame.
#define FRAME_LEN 13  // 1 STATUS + DATA_LEN + 1 SUM

// Two buffers. A third would guarantee a free slot but costs a whole frame of
// RAM on a part that has very little; the stale-latch reclaim in publishFrame()
// closes the same hole for one byte.
#define FRAME_BUFFERS 2
static volatile uint8_t frameBuf[FRAME_BUFFERS][FRAME_LEN];
static volatile uint8_t activeFrame = 0;

// Latched by the ISR on ADDR-match; read-only for the rest of the transaction.
static volatile const uint8_t* servedFrame = frameBuf[0];
// Index of the buffer an in-flight read is serving, 0xFF when idle. The
// publish side never picks it: a 400 kHz transaction can straddle
// a 4 ms publish boundary, and recycling its buffer mid-read tears the
// frame. Skipping a publish costs one sweep of staleness; tearing costs
// integrity.
static volatile uint8_t servingBuf = 0xFF;

// Sticky-button consumption. The ISR marks which sticky bytes the read cursor
// passed; the actual clear happens at transaction end, and is applied by the
// main loop (never mid-transfer, so an aborted read loses nothing).
#define STICKY_PRESSED_BIT  1u
#define STICKY_RELEASED_BIT 2u
static volatile uint8_t stickyConsumed = 0;  // ISR-local, per transaction
static volatile uint8_t stickyClearReq = 0;  // set by ISR, drained by loop()

// Peripheral-watchdog bookkeeping. A plain counter, not millis(): the ISR runs
// at priority 0 and millis() is not guaranteed reentrant against the tick it
// reads. The loop only asks "did the ISR make progress", which a counter
// answers without touching the time base. 16 bits: the loop samples it far
// faster than 65536 interrupts can arrive, so it cannot alias.
static volatile uint16_t isrActivity = 0;

// Transaction state, one byte rather than two bools. TXF_READING is the
// direction latched at ADDR-match -- re-reading SR2 later to ask the
// peripheral again is how an ADDR event gets silently swallowed, and the
// answer cannot change inside one transaction.
#define TXF_READING 0x01
#define TXF_ACTIVE  0x02
#define TXF_POINTER 0x04   // next received byte is the register pointer
static volatile uint8_t txFlags = 0;

// Consecutive sweeps publishFrame() has skipped for a held buffer.
static uint8_t staleLatchSweeps = 0;

// Main-loop-owned live state, folded into the frame on each sweep.
static uint8_t  stickyPressed  = 0;  // pressed-since-last-read, clear on read
static uint8_t  stickyReleased = 0;  // released-since-last-read, clear on read
static bool     firstSweepDone = false;
static bool     localFault     = false;  // latched: strap/I2C/watchdog events

static I2C_HandleTypeDef hi2c;

/*
 * Byte fetch for a master read. The STATUS+DATA+SUM window is served from
 * the frame the ISR latched at ADDR-match; identity/config bytes come from
 * the sparse map; anything past the defined content reads 0x00 — padding,
 * never 0xFF, and never a mid-transaction NACK.
 */
static inline uint8_t readByte(uint8_t index)
{
  if (index >= Proto::REG_STATUS && index < Proto::REG_STATUS + FRAME_LEN) {
    return servedFrame[index - Proto::REG_STATUS];
  }
  return (index < REG_MAP_SIZE) ? regMap[index] : 0x00;
}

// Marks which sticky DATA bytes the master has actually received, so they can
// be cleared once the transaction completes (never mid-transfer).
//
// `deliveredEnd` is one past the last byte known to have clocked out, which is
// not the same as the last byte written into DR: a byte sits in DR for a whole
// bit period before the master takes it, and a master that stops early never
// takes it at all. Callers pass the index they are *about* to write (its
// predecessor has necessarily gone out by then), and endTransaction() passes
// the final index once the STOP or NACK proves the last byte landed. Counting
// a written-but-unsent byte as delivered would clear an edge the hub never
// saw, and a lost press is worse than a repeated one. A STATUS-only
// poll reads byte 0x20 and stops: the cursor stops at 0x21, so no edge is
// consumed by an idle tick.
static void noteCursorPassed(uint8_t deliveredEnd)
{
  if (deliveredEnd > Proto::REG_DATA + Proto::SliderTile::D_BTN_PRESSED) {
    stickyConsumed |= STICKY_PRESSED_BIT;
  }
  if (deliveredEnd > Proto::REG_DATA + Proto::SliderTile::D_BTN_RELEASED) {
    stickyConsumed |= STICKY_RELEASED_BIT;
  }
}

// Ends a transaction: re-arm ACK, drop byte interrupts, request the deferred
// clear of any sticky bits the cursor actually passed. regPointer persists
// on purpose: a pointer write followed by a separate read transaction is
// the standard addressing idiom (the hub always does pointer-write STOP,
// then read).
static inline void endTransaction(void)
{
  // The transaction is over, so the last byte written into DR did clock out.
  // This is the only place that can know it, and without it a full-frame read
  // would leave its final sticky byte uncleared forever.
  if (txFlags & TXF_READING) noteCursorPassed(txIndex);

  I2C1->CR1 |= I2C_CR1_ACK;
  I2C1->CR2 &= ~I2C_CR2_ITBUFEN;
  txFlags    = 0;
  servingBuf = 0xFF;                       // publish side may use it again
  if (stickyConsumed) {
    stickyClearReq |= stickyConsumed;
    stickyConsumed = 0;
  }
}

/*
 * Every piece of per-transaction state, cleared together.
 *
 * i2cSlaveBegin() calls this so a rebuilt peripheral cannot inherit the
 * remains of the transaction that wedged it. servingBuf is the one that
 * mattered: it is otherwise cleared only in endTransaction(), and a
 * transaction killed by the bus watchdog never reaches one. The buffer stayed
 * reserved, publishFrame() skipped every sweep from then on, and the tile
 * froze SEQ, DATA and HEARTBEAT alike while the slave went on answering
 * perfectly. Triple buffering means that leak no longer stops publishing, but
 * leaking it at all is still a bug.
 */
static inline void i2cResetTransactionState(void)
{
  // Only the state a dead transaction can strand. regPointer, txIndex and
  // servedFrame are all rewritten at the next ADDR-match before anything
  // reads them, so clearing them here would cost stores for nothing.
  txFlags        = 0;
  servingBuf     = 0xFF;
  stickyConsumed = 0;
}

// One received byte: first byte after address-match is the register pointer,
// the rest are payload dispatched through handleRegisterWrite with an
// auto-incrementing cursor. Shared by RXNE and BTF so no byte is ever
// discarded when the ISR is entered with BTF already set.
static inline void rxByte(uint8_t data)
{
  if (txFlags & TXF_POINTER) {
    // Park, don't clamp: an out-of-range pointer must not silently land
    // somewhere unrelated in the map, least of all on SOFT_CMD.
    regPointer  = (data < REG_MAP_SIZE) ? data : (uint8_t)REG_PARK;
    txFlags &= (uint8_t)~TXF_POINTER;
    return;
  }
  handleRegisterWrite(regPointer, data);
  // Auto-increment across the config/LED/SOFT pages, but never off the end:
  // REG_MAP_SIZE - 1 *is* REG_SOFT_CMD, so clamping there would dispatch
  // every further byte of an over-long write as a soft command.
  regPointer = (uint8_t)((regPointer + 1u < REG_MAP_SIZE) ? (regPointer + 1u)
                                                         : REG_PARK);
}

// --- Address strap ---------------------------------------------------------
//
// PB5 is probed twice per round with opposite internal pulls:
//   1k-to-GND : low with pull-up,    low with pull-down  -> offset 0
//   1k-to-3V3 : high with pull-up,   high with pull-down -> offset 1
//   floating  : high with pull-up,   low with pull-down  -> offset 2
// Eight consecutive agreeing pairs are required before the offset is
// accepted. Afterwards PB5 is left as analog-in/no-pull so the divider
// draws no standing current; the pull is re-enabled only during a
// SOFT_CMD 0x02 re-read.

// Pull direction is driven via PUPDR directly rather than pinMode(): the
// core's INPUT_PULLDOWN path is unverified on 0.2.62, and a silent NOPULL
// fallback here would make the floating case indistinguishable from a
// pulled one — exactly the failure mode the 8-pair rule exists to catch.
static void strapSetPull(uint32_t pupdr)
{
  uint32_t tmp = GPIOB->PUPDR & ~(3UL << (PIN_STRAP * 2));
  GPIOB->PUPDR = tmp | (pupdr << (PIN_STRAP * 2));
}

static StrapPair strapProbeOnce(void)
{
  __HAL_RCC_GPIOB_CLK_ENABLE();

  // Pin as input, no pull first.
  uint32_t tmp = GPIOB->MODER & ~(3UL << (PIN_STRAP * 2));
  GPIOB->MODER = tmp;                       // input mode (00)

  StrapPair p = { 2, false };

  strapSetPull(1UL);                        // 01 = pull-up
  delayMicroseconds(100);
  (void)digitalRead(PIN_STRAP);             // settle SIO after pull switch
  delayMicroseconds(20);
  bool upHigh = (digitalRead(PIN_STRAP) == HIGH);

  strapSetPull(2UL);                        // 10 = pull-down
  delayMicroseconds(100);
  (void)digitalRead(PIN_STRAP);
  delayMicroseconds(20);
  bool downHigh = (digitalRead(PIN_STRAP) == HIGH);

  strapSetPull(0UL);                        // 00 = no pull between probes

  if (!upHigh && !downHigh)      { p.offset = 0; p.valid = true; }
  else if (upHigh && downHigh)   { p.offset = 1; p.valid = true; }
  else if (upHigh && !downHigh)  { p.offset = 2; p.valid = true; }
  return p;
}

static void strapBegin(void)
{
  const uint8_t REQUIRED_PAIRS = 8;

  uint8_t streak    = 0;
  uint8_t candidate = 0xFF;

  for (uint8_t attempt = 0; attempt < 64; attempt++) {
    StrapPair p = strapProbeOnce();
    if (!p.valid || p.offset != candidate) {
      candidate = p.offset;
      streak    = p.valid ? 1 : 0;
      continue;
    }
    if (++streak >= REQUIRED_PAIRS) {
      addrOffset  = candidate;
      strapStable = true;
      pinMode(PIN_STRAP, INPUT_ANALOG);  // no pull: stops the ~80 uA leak
      return;
    }
  }

  addrOffset  = 2;      // behave as floating
  strapStable = false;  // LOCAL_FAULT
  pinMode(PIN_STRAP, INPUT_ANALOG);
}

// --- Boot-time bus recovery ----------------------------------------------
//
// A master reset mid-transaction can leave a slave driving SDA low; the
// I2C peripheral then cannot start (BUSY stuck). Before handing PB6/PB7 to
// I2C, drive SCL as open-drain GPIO and, if SDA is held low, clock up to
// nine pulses plus a manual STOP to release whichever slave is mid-byte.

static void i2cBusRecover(void)
{
  GPIO_InitTypeDef gpio = {};
  gpio.Pin   = GPIO_PIN_6 | GPIO_PIN_7;
  gpio.Mode  = GPIO_MODE_OUTPUT_OD;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio);

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
  delayMicroseconds(10);

  if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_RESET) {
    for (uint8_t i = 0; i < 9; i++) {
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
      delayMicroseconds(5);
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
      delayMicroseconds(5);
      if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) != GPIO_PIN_RESET) break;
    }
    // Manual STOP: SDA low->high while SCL stays high.
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
    delayMicroseconds(5);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    delayMicroseconds(5);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
    delayMicroseconds(5);
  }
}

// --- Control surface -------------------------------------------------------
// Writability is gated per-register: only the config page accepts data.
// Identity/STATUS/DATA/SUM/UID stay read-only even if the master tries;
// LED registers are drained and ignored (LED_TIER=0, never NACK);
// SOFT_CMD dispatches. Commands that must not run in interrupt context
// (IWDG starvation, strap re-probe) are queued for loop().
static volatile bool cmdResetQueued       = false;  // stop feeding IWDG
static volatile bool cmdStrapRereadQueued = false;

static void handleRegisterWrite(uint8_t reg, uint8_t value)
{
  if (reg >= Proto::REG_CFG_RATE && reg <= Proto::REG_CFG_DEBOUNCE) {
    regMap[reg] = value;               // writable config page
    return;
  }
  if (reg >= Proto::REG_LED_BRIGHTNESS && reg <= Proto::REG_LED_CLEAR) {
    return;                            // drain and ignore, no LEDs on this tile
  }
  if (reg == Proto::REG_SOFT_CMD) {
    if      (value == Proto::CMD_RESET)        cmdResetQueued = true;
    else if (value == Proto::CMD_REREAD_STRAP) cmdStrapRereadQueued = true;
    else {                                     // 0xA5 bootloader or unknown:
      localFault = true;                       // CAPS bit9=0, entry impossible
    }
    return;
  }
  // Everything else: silently drained, never NACK.
}

/*
 * Register-level I2C slave interrupt handler.
 *
 * Invariants that keep the peripheral from ever wedging:
 *   - every status flag that can hold SCL low (ADDR, BTF, STOPF, AF) is
 *     cleared on the path that observes it;
 *   - ACK is re-asserted at the end of every transaction;
 *   - the byte interrupt (ITBUFEN) is only enabled while addressed, so an
 *     idle TXE cannot storm the CPU;
 *   - the read cursor is clamped, so the master may read any length;
 *   - every loop here is bounded, so a flag the peripheral refuses to clear
 *     costs one pass, not the CPU.
 *
 * The data phase branches on the direction latched at ADDR-match rather than
 * re-reading SR2. Two reasons: reading SR2 while ADDR happens to be set is
 * exactly how an address event gets swallowed, and TXE and BTF can both be
 * live in one SR1 snapshot -- servicing them as independent `if`s wrote DR
 * twice for a single byte slot, sending the frame out of step from there on.
 */
extern "C" void I2C1_IRQHandler(void)
{
  ++isrActivity;                           // liveness for i2cBusWatchdog()
  uint32_t sr1 = I2C1->SR1;

  // --- Address matched: start of a transaction ------------------------
  if (sr1 & I2C_SR1_ADDR) {
    uint32_t sr2 = I2C1->SR2;              // SR1 then SR2 clears ADDR
    I2C1->CR2 |= I2C_CR2_ITBUFEN;          // want TXE/RXNE while addressed

    txFlags = (sr2 & I2C_SR2_TRA) ? (TXF_ACTIVE | TXF_READING) : TXF_ACTIVE;

    if (txFlags & TXF_READING) {           // master is reading from us
      // Latch one published snapshot for the whole transaction.
      servingBuf  = activeFrame;
      servedFrame = frameBuf[activeFrame];
      txIndex = regPointer;
      I2C1->DR = readByte(txIndex);        // prime the shifter
      if (txIndex < 0xFF) txIndex++;       // nothing is delivered yet
    } else {                               // master is writing to us
      txFlags |= TXF_POINTER;
    }
    return;                                // sr1 is stale; re-enter for data
  }

  // --- STOP: end of a write transaction -------------------------------
  if (sr1 & I2C_SR1_STOPF) {
    I2C1->CR1 |= I2C_CR1_PE;               // SR1 read + CR1 write clears STOPF
    endTransaction();
    return;
  }

  // --- NACK: master ended a read transaction --------------------------
  if (sr1 & I2C_SR1_AF) {
    I2C1->SR1 = (uint32_t)~I2C_SR1_AF;
    endTransaction();                      // credits the final byte
    return;
  }

  // --- Bus errors: clear and stay listening ---------------------------
  if (sr1 & (I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_OVR)) {
    I2C1->SR1 = (uint32_t)~(I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_OVR);
    I2C1->CR1 |= I2C_CR1_ACK;
  }

  // --- Data phase ------------------------------------------------------
  if (!(txFlags & TXF_ACTIVE)) return;     // nothing addressed us; no cursor

  if (txFlags & TXF_READING) {
    // TXE and BTF are the same request here -- "the peripheral wants the next
    // byte" -- and get exactly one DR write between them.
    if (sr1 & (I2C_SR1_TXE | I2C_SR1_BTF)) {
      noteCursorPassed(txIndex);           // the previous byte has clocked out
      uint8_t i = txIndex;
      if (txIndex < 0xFF) txIndex++;
      I2C1->DR = readByte(i);
    }
  } else {
    // RXNE means one byte is waiting; BTF means a second arrived behind it and
    // SCL is being stretched until both are taken. Draining until the
    // peripheral reports empty covers both without discarding a byte, which is
    // routine mid-write at 400 kHz. Bounded so a stuck flag cannot spin here.
    for (uint8_t guard = 0; guard < 3u; guard++) {
      uint32_t s = I2C1->SR1;
      if (!(s & (I2C_SR1_RXNE | I2C_SR1_BTF))) break;
      rxByte((uint8_t)I2C1->DR);
    }
  }
}

static void i2cSlaveBegin(void)
{
  __HAL_RCC_GPIOB_CLK_ENABLE();

  // Idempotent: this also runs from the bus watchdog, mid-flight, on a
  // peripheral that may still be addressed. Silence the IRQ first so the
  // handler cannot observe a half-rebuilt peripheral.
  HAL_NVIC_DisableIRQ(I2C1_IRQn);

  i2cBusRecover();

  GPIO_InitTypeDef gpio = {};
  gpio.Pin       = GPIO_PIN_6 | GPIO_PIN_7;   // PB6 SCL, PB7 SDA
  gpio.Mode      = GPIO_MODE_AF_OD;
  gpio.Pull      = GPIO_NOPULL;               // board has external pull-ups
  gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF6_I2C;
  HAL_GPIO_Init(GPIOB, &gpio);

  __HAL_RCC_I2C_CLK_ENABLE();
  __HAL_RCC_I2C_FORCE_RESET();
  __HAL_RCC_I2C_RELEASE_RESET();

  // Whatever transaction was in flight is gone with the reset; forget it
  // before the peripheral can be addressed again.
  i2cResetTransactionState();

#if defined(I2C_FLTR_DNF)
  // One-cycle digital filter, programmable only while PE is clear (which it
  // is, straight out of reset). Suppresses spikes shorter than tI2CCLK on a
  // bus shared with a display, a magnetometer and a lidar. Compiled out on
  // parts whose headers do not define the register.
  I2C1->FLTR = (I2C1->FLTR & ~I2C_FLTR_DNF) | 1u;
#endif

  // HAL_I2C_Init only programs FREQ/CCR/TRISE/OAR1/CR1; the slave state
  // machine it would otherwise drive is deliberately not used.
  hi2c.Instance             = I2C1;
  hi2c.Init.ClockSpeed      = kBusClockHz;
  hi2c.Init.DutyCycle       = I2C_DUTYCYCLE_2;
  hi2c.Init.OwnAddress1     = (uint32_t)((Proto::ADDR_BASE + addrOffset) << 1);
  hi2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;  // stretch while the ISR works
  HAL_I2C_Init(&hi2c);

  I2C1->CR1 |= I2C_CR1_ACK;
  I2C1->CR2 |= I2C_CR2_ITEVTEN | I2C_CR2_ITERREN;  // ITBUFEN only while addressed

  HAL_NVIC_SetPriority(I2C1_IRQn, 0, 0);
  HAL_NVIC_ClearPendingIRQ(I2C1_IRQn);
  HAL_NVIC_EnableIRQ(I2C1_IRQn);
}

#if USE_IWDG
// ~250 ms: LSI (~32.768 kHz) / 64 = 512 Hz, reload 128.
//
// PR and RLR are only writable after the counter is started and the write
// protection lifted, and each takes a few LSI cycles to land -- SR reports
// the update pending. Programming them without waiting (or before the start
// key) can leave the prescaler at its reset value, which shortens the window
// to ~3 ms and reboots a perfectly healthy tile.
static void iwdgWaitIdle(void)
{
  // Bounded: a backstop that hangs during its own setup is no backstop. A few
  // LSI cycles at ~32 kHz is tens of core cycles, so this is generous.
  for (uint16_t guard = 0; guard < 8000u && IWDG->SR != 0u; guard++) {
  }
}

static void watchdogBegin(void)
{
  IWDG->KR  = 0xCCCC;   // start (also enables the LSI)
  IWDG->KR  = 0x5555;   // unlock PR/RLR
  iwdgWaitIdle();
  IWDG->PR  = 4;        // prescaler /64
  iwdgWaitIdle();
  IWDG->RLR = 128;
  iwdgWaitIdle();
  IWDG->KR  = 0xAAAA;   // reload with the values just programmed
}
static inline void watchdogFeed(void) { IWDG->KR = 0xAAAA; }
#else
static void watchdogBegin(void) {}
static inline void watchdogFeed(void) {}
#endif

// One conversion per configured channel, folded into the accumulators. Runs
// from loop(), never from the I2C ISR, so conversion time is irrelevant.
static void adcSubstep(void)
{
  if (sampleCount >= ADC_SUBSAMPLES) return;  // don't skew sweep weighting
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    int v = analogRead(sliderPins[ch]);
    if (v < 0) v = 0;
    if (v > 4095) v = 4095;
    sampleAccum[ch] += (uint32_t)v;
  }
  sampleCount++;
}

// Returns a packed mask with one bit per button. The inputs use their internal
// pull-ups, so a low pin level means the corresponding button is pressed.
static uint8_t readButtons(void)
{
  uint8_t buttons = 0;
  for (uint8_t button = 0; button < NUM_BUTTONS; button++) {
    if (digitalRead(buttonPins[button]) == BUTTON_PRESSED_LEVEL) {
      buttons |= (uint8_t)(1U << button);
    }
  }
  return buttons;
}

// Debounces every button independently and latches edges as sticky bits:
// pressed-since-last-read and released-since-last-read. The required stable
// streak is derived from the CFG_DEBOUNCE register (ms) over the sweep
// cadence, so a hub writing a larger window gets proportionally more
// samples before an edge is accepted.
static void updateButtons(uint8_t& pressedOut, uint8_t& releasedOut)
{
  uint8_t sampledButtons = readButtons();
  uint8_t nextButtons = stableButtons;

  // ceil(ms / sweep period), floor of 1 sample.
  uint8_t needed = (uint8_t)((regMap[Proto::REG_CFG_DEBOUNCE] + SWEEP_INTERVAL_MS - 1)
                             / SWEEP_INTERVAL_MS);
  if (needed < 1) needed = 1;

  for (uint8_t button = 0; button < NUM_BUTTONS; button++) {
    uint8_t mask = (uint8_t)(1U << button);
    bool sampledPressed = (sampledButtons & mask) != 0;
    bool stablePressed = (stableButtons & mask) != 0;

    if (sampledPressed == stablePressed) {
      buttonDebounceCount[button] = 0;
      continue;
    }

    if (++buttonDebounceCount[button] < needed) {
      continue;
    }

    buttonDebounceCount[button] = 0;
    if (sampledPressed) {
      nextButtons |= mask;
      pressedOut |= mask;
    } else {
      nextButtons &= (uint8_t)~mask;
      releasedOut |= mask;
    }
  }

  stableButtons = nextButtons;
}

// Noise reduction only (§7.5): move each published value toward the averaged
// reading through a deadband evaluated against the published value, then a
// slew limit. Deadband idx 0 (default) is 1 count — just enough to kill
// resting dither without erasing motion; slew idx 0 (default) is unlimited.
static void applyFilterToPublished(void)
{
  uint8_t dbIdx  = regMap[Proto::REG_CFG_FILTER] & 0x03;
  uint8_t slwIdx = (uint8_t)((regMap[Proto::REG_CFG_FILTER] >> 4) & 0x03);

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    int16_t delta = (int16_t)rawValue[ch] - (int16_t)publishedValue[ch];
    int16_t mag   = (delta < 0) ? (int16_t)-delta : delta;
    if (mag <= (int16_t)kDeadbandCounts[dbIdx]) continue;  // inside hysteresis

    // Step out of the deadband fully toward the target, honouring slew.
    int16_t step = mag;
    uint8_t slew = kSlewPerSweep[slwIdx];
    if (slew != 0 && step > (int16_t)slew) step = (int16_t)slew;

    publishedValue[ch] =
        (uint16_t)((int32_t)publishedValue[ch] + ((delta < 0) ? -step : step));
  }
}

// Builds a complete STATUS+DATA+SUM frame in a free buffer and publishes it
// with a single index flip.
//
// HEARTBEAT toggles every sweep regardless of change, and the hub treats that
// as the tile's only proof of life: a satellite that answers the bus while its
// STATUS byte never moves is reported as a stale link rather than believed.
// Nothing may make this toggle conditional on anything but the sweep running.
// SEQ advances only when any of the 11 DATA bytes changed (a fader
// crossing its deadband or any button edge); NOT_READY stays set until the
// first sweep completes. Frozen SEQ + toggling HEARTBEAT = idle, both frozen
// = wedged.
#define ST_HEARTBEAT   0x01
#define ST_LOCAL_FAULT 0x02
#define ST_NOT_READY   0x04

static uint8_t seqCounter     = 0;
static uint8_t heartbeatState = 0;

static void publishFrame(void)
{
  uint8_t back = (uint8_t)(activeFrame ^ 1u);

  // Never overwrite a buffer an in-flight read is still serving -- but never
  // wait on that latch forever either. A transaction lasts microseconds; if it
  // is still held two sweeps later the reader is gone (a master reset
  // mid-read, or the peripheral rebuilt under it) and the latch is stale.
  // Skipping publishes indefinitely instead would freeze SEQ, DATA and
  // HEARTBEAT together while the slave kept answering -- on the wire, a dead
  // panel. One skipped sweep is 4 ms; the hub's link timeout is 100 ms.
  if ((uint8_t)servingBuf == back) {
    if (++staleLatchSweeps < 2u) return;
    servingBuf = 0xFF;
    localFault = true;   // surfaced in STATUS: this should not have happened
  }
  staleLatchSweeps = 0;

  volatile uint8_t* f = frameBuf[back];

  heartbeatState ^= ST_HEARTBEAT;

  uint8_t status = heartbeatState;
  if (localFault)          status |= ST_LOCAL_FAULT;
  if (!firstSweepDone)     status |= ST_NOT_READY;
  status |= (uint8_t)(seqCounter << 4);

  f[0] = status;
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    f[1 + Proto::SliderTile::D_FADER + ch * 2]     = (uint8_t)(publishedValue[ch] & 0xFF);
    f[1 + Proto::SliderTile::D_FADER + ch * 2 + 1] = (uint8_t)((publishedValue[ch] >> 8) & 0xFF);
  }
  f[1 + Proto::SliderTile::D_BTN_LEVEL]    = stableButtons;
  f[1 + Proto::SliderTile::D_BTN_PRESSED]  = stickyPressed;
  f[1 + Proto::SliderTile::D_BTN_RELEASED] = stickyReleased;

  // SEQ advances only when DATA actually changed (compare vs published).
  volatile const uint8_t* cur = frameBuf[activeFrame];
  bool changed = false;
  for (uint8_t i = 1; i <= Proto::SliderTile::DATA_LEN; i++) {
    if (f[i] != cur[i]) { changed = true; break; }
  }
  if (changed && firstSweepDone) {
    seqCounter = (uint8_t)((seqCounter + 1u) & 0x0Fu);
    f[0] = (uint8_t)((status & ~0xF0u) | (seqCounter << 4));
  }

  uint8_t sum = f[0];
  for (uint8_t i = 1; i <= Proto::SliderTile::DATA_LEN; i++) {
    sum = (uint8_t)(sum + f[i]);
  }
  f[1 + Proto::SliderTile::DATA_LEN] = sum;

  activeFrame = back;      // single-flip publish
  firstSweepDone = true;
}

// Applies deferred clears for sticky bits a completed transaction consumed.
// Runs in the loop, so a master aborting early never loses an unreceived event.
static void drainStickyClears(void)
{
  noInterrupts();
  uint8_t req = stickyClearReq;
  stickyClearReq = 0;
  interrupts();

  if (req & STICKY_PRESSED_BIT)  stickyPressed = 0;
  if (req & STICKY_RELEASED_BIT) stickyReleased = 0;
}

// --- Peripheral-level I2C watchdog ---------------------------------------
//
// BUSY stuck with no interrupt activity means a slave is holding the bus or
// the peripheral wedged itself: normal traffic clears BUSY at every STOP, and
// anything addressing us bumps isrActivity. Soft re-init the I2C peripheral
// alone -- no reboot, no SEQ discontinuity, since the frame state lives in RAM
// untouched. IWDG remains the backstop for genuine CPU hangs.

static void i2cBusWatchdog(uint32_t now)
{
  static uint32_t lastProgressMs = 0;
  static uint32_t lastActivity   = 0;

  const uint32_t activity = isrActivity;
  const bool busy = (I2C1->SR2 & I2C_SR2_BUSY) != 0;

  if (!busy || activity != lastActivity) {
    lastActivity   = activity;
    lastProgressMs = now;
    return;
  }
  if (now - lastProgressMs < I2C_STUCK_MS) return;

  lastProgressMs = now;
  localFault = true;   // latched: "I2C recovered" shows up in STATUS

  // i2cSlaveBegin() masks the IRQ, bit-bangs the bus free, resets the
  // peripheral and clears the transaction state it died holding.
  i2cSlaveBegin();
}

void setup()
{
  analogReadResolution(12);  // native ADC resolution on this part

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    pinMode(sliderPins[ch], INPUT_ANALOG);
  }

  for (uint8_t button = 0; button < NUM_BUTTONS; button++) {
    pinMode(buttonPins[button], BUTTON_PIN_MODE);
  }

  // Identity block, §5.1. Undefined map bytes stay 0x00.
  regMap[Proto::REG_WHO_AM_I]  = Proto::WHO_AM_I_MAGIC;
  regMap[Proto::REG_TYPE_ID]   = Proto::TYPE_SLIDER;
  regMap[Proto::REG_PROTO_VER] = Proto::PROTO_VER_V2;
  regMap[Proto::REG_FW_VER]     = FW_VER_MAJOR;
  regMap[Proto::REG_FW_VER + 1] = FW_VER_MINOR;
  regMap[Proto::REG_HW_REV]    = HW_REV;
  regMap[Proto::REG_CAPS]      = (uint8_t)(Proto::CAP_ANALOG_IN |
                                           Proto::CAP_DIGITAL_IN);
  regMap[Proto::REG_CAPS + 1]  = 0x00;
  memcpy(&regMap[Proto::REG_UID], (const uint8_t*)PY32F030_UID_BASE, 12);
  // DECLARED_MA, LED_COUNT, LED_TIER stay 0x00.
  regMap[Proto::REG_DATA_LEN] = Proto::SliderTile::DATA_LEN;

  // Config page defaults: 1-count deadband, unlimited slew, 4 ms debounce.
  regMap[Proto::REG_CFG_RATE]     = 0;  // stored, advisory on this tile
  regMap[Proto::REG_CFG_FILTER]   = CFG_FILTER_DEFAULT;
  regMap[Proto::REG_CFG_DEBOUNCE] = CFG_DEBOUNCE_DEFAULT_MS;

  strapBegin();
  regMap[Proto::REG_ADDR_OFFSET] = addrOffset;
  if (!strapStable) localFault = true;

  // Prime the filter state with one live sample per channel so the first
  // published frame doesn't ramp from zero.
  adcSubstep();
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    rawValue[ch]       = (uint16_t)sampleAccum[ch];
    publishedValue[ch] = rawValue[ch];
    sampleAccum[ch]    = 0;
  }
  sampleCount = 0;

  stableButtons = readButtons();
  updateButtons(stickyPressed, stickyReleased);
  publishFrame();           // initial NOT_READY frame; real data within 4 ms

  i2cSlaveBegin();
  watchdogBegin();
}

void loop()
{
  static uint32_t lastUpdate    = 0;
  static uint32_t lastSubstepUs = 0;
  uint32_t now = millis();

  watchdogFeed();

  // Spread ADC conversions across the sweep instead of blocking once per
  // sweep; adcSubstep() self-limits to ADC_SUBSAMPLES rounds.
  uint32_t us = micros();
  if (us - lastSubstepUs >= SUBSTEP_INTERVAL_US) {
    lastSubstepUs = us;
    adcSubstep();
  }

  // SOFT_CMD dispatch (queued from the ISR, run here):
  if (cmdStrapRereadQueued) {
    cmdStrapRereadQueued = false;
    uint8_t oldOffset = addrOffset;
    strapBegin();                       // re-probes PB5 with pulls re-enabled
    regMap[Proto::REG_ADDR_OFFSET] = addrOffset;
    if (!strapStable) localFault = true;
    if (addrOffset != oldOffset && strapStable) {
      // Follow the strap: answer at the new registry slot immediately. Keep
      // the handle in sync too — the bus watchdog re-inits the peripheral
      // through i2cSlaveBegin(), which would otherwise restore the old slot.
      hi2c.Init.OwnAddress1 = (uint32_t)((Proto::ADDR_BASE + addrOffset) << 1);
      I2C1->OAR1 = hi2c.Init.OwnAddress1;
    }
  }
  if (cmdResetQueued) {
    return;                             // stop reaching watchdogFeed(): IWDG
  }                                     // fires in ~250 ms, clean reset

  if (now - lastUpdate >= SWEEP_INTERVAL_MS) {
    lastUpdate = now;

    // Finalize this sweep's averaged readings and restart accumulation.
    if (sampleCount > 0) {
      for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
        rawValue[ch]    = (uint16_t)(sampleAccum[ch] / sampleCount);
        sampleAccum[ch] = 0;
      }
      sampleCount = 0;
    }

    // Sticky edges are cleared ONLY by drainStickyClears(), i.e. only once a
    // master's read cursor has actually passed those bytes. They must NOT be
    // zeroed unconditionally here: that turns them into "edges in the last
    // 4 ms" instead of "edges since the last read", and a tap that begins and
    // ends between two hub polls then vanishes from level, pressed and
    // released alike. updateButtons() ORs this sweep's new edges on top.
    drainStickyClears();
    updateButtons(stickyPressed, stickyReleased);

    applyFilterToPublished();
    publishFrame();         // 250 Hz
  }

  i2cBusWatchdog(now);
}
