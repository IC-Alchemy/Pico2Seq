# PY32 satellite tile firmware

The sketches that run on the Alchemy Modular UI tiles the Pico2Seq hub talks to.
They live here, beside the hub, because the two halves share one contract and
breaking it from either side is silent: a tile that stops sweeping still answers
the bus perfectly, and a hub that trusts the wrong signal never notices.

Both run on a **PY32F030F28U6TR**: TSSOP-20, 64 KB flash, 8 KB SRAM.

| Sketch | Type | Addresses | DATA_LEN |
|---|---|---|---|
| [`SliderModule/SliderModule.ino`](SliderModule/SliderModule.ino) | `0x01` | 0x08–0x0A | 11 |
| [`ButtonModule8/ButtonModule8.ino`](ButtonModule8/ButtonModule8.ino) | `0x02` | 0x0B–0x0D | 3 |

The hub side is `src/AlchemyUI/`; the whole arrangement is written up in
[`docs/alchemy-satellite-link.md`](../docs/alchemy-satellite-link.md).

## Building

These are Arduino sketches for the PY32Duino core, built and flashed
independently of the Pico2Seq firmware. **Select the board variant that matches
the part you actually have** — see [Fitting the part](#fitting-the-part); the
variant decides the linker script's FLASH and RAM sizes, and picking a smaller
one than the chip is the usual reason a link fails.

Set `NUM_BUTTONS` to 4 or 8 in `ButtonModule8.ino` to match the PCB; nothing
else differs between the two button variants, and no protocol version moves.

## Fitting the part

Measured with the command below, sketch code only — the core and HAL are on top
of this:

| Sketch | flash (text + data) | RAM (data + bss) |
|---|---|---|
| SliderModule | 2529 | 281 |
| ButtonModule8 | 1913 | 213 |

The hardening in 1.04 costs the slider about 108 bytes of flash and no more than
3 bytes of RAM over the 1.03 filter it is built on.

If the link overflows, get the numbers before changing code:

```bash
arm-none-eabi-g++ -mcpu=cortex-m0plus -mthumb -Os -fno-exceptions -fno-rtti \
  -x c++ -std=gnu++17 -include Arduino.h -c tiles/SliderModule/SliderModule.ino -o /tmp/t.o
arm-none-eabi-size /tmp/t.o
```

An overflow far larger than the sketch's own footprint is not a code problem.

**The board part number is the thing to check first.** `PY32F030F28U6TR` is
TSSOP-20, and by Puya's naming density code 8 is 64 KB flash / 8 KB SRAM — read
from the part number, not from a datasheet, so confirm it. The variant chosen in
the IDE sets the linker script's FLASH and RAM sizes, and the core and HAL are
linked in on top of the sketch, so a variant smaller than the chip overflows by
roughly the size of the core no matter what the sketch does.

One recorded instance: a link overflowed FLASH by 952 bytes and RAM by 48 while
the maintainer's own build of the previous sketch had just flashed. The two
sketches were within 12 bytes of each other in total footprint (2693 vs 2681
with the command above), so the sketch was not what differed. The overflow
arithmetic — an image of ~17.3 KB and ~2.1 KB — is what a 16 KB / 2 KB linker
script would produce, but which variant that build used was never confirmed.

After that, in order:

1. **`--gc-sections`.** Without it every unreferenced core and HAL function is
   linked in.
2. **`--specs=nano.specs`**, for the small newlib.

Only then trim the sketch, and there is one obvious target if you ever do:
`regMap[REG_MAP_SIZE]` is 128 bytes of RAM holding 26 bytes of mostly-constant
identity, 3 bytes of config and ~99 bytes of zero padding. Serving the identity
from a `const` table in flash, the UID straight from its factory address, and
the config from a 3-byte array frees ~125 bytes of RAM for a few tens of bytes
of flash. On a 64 KB / 8 KB part there is no reason to bother.

## Tested on the host

Both sketches are compiled unmodified into the repo's host test suite, against
a shim for the PY32Duino core and its HAL (`tests/py32_stubs/`). A test plays
the master: it raises ADDR, calls `I2C1_IRQHandler()`, takes DR, raises TXE,
and asserts on what came back.

```bash
cmake -B build_test -DCMAKE_BUILD_TYPE=Debug
cmake --build build_test --parallel
./build_test/tests/py32_slider_tests
./build_test/tests/py32_button_tests
```

That covers the slave state machine and the publish path — the half of the
contract the hub cannot check from its end. It does **not** cover timing, ADC
behaviour, the strap divider, or anything else that needs the part. Flash and
bench-test before trusting a change.

## Three rules that are load-bearing

Everything else in these sketches is local; these three are the interface, and
the hub is built on them.

1. **HEARTBEAT toggles on every sample sweep**, change or no change. The hub
   treats a STATUS byte that stops moving as a dead link, whatever the checksum
   says — because a tile whose publish path has stalled goes on answering
   perfectly. Nothing may make this toggle conditional on anything but the
   sweep running.
2. **SEQ advances only when a DATA byte changed**, and wraps at 15. The hub
   compares it for equality only.
3. **Sticky press/release bits clear only once a master has actually received
   them.** A byte sitting in DR has not been received; a master that stops
   short never takes it. Losing a press is worse than repeating one, so the
   accounting is deliberately conservative at the end of a transaction.

## Why there is no `#include <Wire.h>`

The PY32Duino core's Wire slave path wedges the peripheral permanently on a
write-then-read sequence: ADDR or BTF ends up asserted and never cleared, SCL
stays low, and only a power cycle recovers. Slave duty is handled by a small
register-level ISR instead. Keeping `twi.c` out of the link also avoids a
duplicate `I2C1_IRQHandler` symbol, and on the slider tile PF0 carries a button
while being the core's default serial pin — so no `Serial` either.

## Changelog

### Slider 1.04 / Button 1.05

The slider is built on the maintainer's 1.03 conditioning, unchanged: a
pre-deadband one-pole EMA (`RAW_EMA_SHIFT`), sixteen-entry deadband and slew
tables, and a 10-count default deadband. The hub never writes the config page,
so that default is what runs. `[py32][slider][filter]` pins it: a resting
fader's dither does not churn SEQ, a deliberate 30-count move does publish, a
settled fader rests within one deadband of the truth, no index aliases another,
and the smoothing is primed at boot.

- **Bus rate is one named constant** (`kBusClockHz`, 400 kHz) instead of a
  literal in `HAL_I2C_Init`'s arguments. The 1.03 slider now flashed already
  programs 400000, but an earlier 1.03 build programmed 100000 while the button
  tile programmed 400000 — standard-mode timing on a tile driven at fast mode.
  Naming it lets a host test hold both tiles to the same value. Keep it in
  lockstep with `kTileBusFrequencyHz` in `src/app/ControlIO.cpp`.
- **A stale publish latch is reclaimed.** `servingBuf` reserves a buffer for an
  in-flight read and was cleared only in `endTransaction()`, which a
  transaction killed by the bus watchdog never reaches. The reservation then
  left nowhere to publish: `activeFrame` stopped moving and SEQ, DATA and
  HEARTBEAT froze together while the slave went on answering. A transaction
  lasts microseconds, so a latch still held two sweeps later is stale and the
  buffer is taken back (and `LOCAL_FAULT` raised). Transaction state is also
  cleared on every peripheral rebuild, which is where the leak came from.
- **One DR write per byte slot.** TXE and BTF can both be live in a single SR1
  snapshot; servicing them as independent writes put two bytes into one slot
  and skewed every byte after it.
- **Direction latched at ADDR-match** instead of re-reading SR2 mid-transaction,
  which is how an address event gets swallowed.
- **Bounded receive drain**, so no byte is discarded when BTF arrives behind
  RXNE and no flag the peripheral refuses to clear can spin the ISR.
- **Exact sticky-edge accounting.** A byte written into DR but never clocked
  out no longer counts as delivered.
- **An out-of-range register pointer parks on WHO_AM_I.** It used to clamp to
  `REG_MAP_SIZE - 1`, which *is* `REG_SOFT_CMD` — so every stray payload byte
  of an over-long write was dispatched as a soft command, including the reset
  the tile answers by starving its own watchdog.
- **Bus recovery in ~50 ms** instead of ~200 ms, on an explicit progress
  counter rather than `millis()` sampled inside the ISR.
- **IWDG programmed with the documented sequence** (start, unlock, poll SR
  between writes). Writing PR/RLR without waiting can leave the prescaler at
  its reset value, shortening the window to ~3 ms and rebooting a healthy tile.
- **Digital noise filter** enabled where the part's headers expose it, guarded
  so it compiles either way.
- `PY32F030_UID_BASE` is overridable, so a host harness can point it somewhere
  that exists.
