# PY32 satellite tile firmware

The sketches that run on the Alchemy Modular UI tiles the Pico2Seq hub talks to.
They live here, beside the hub, because the two halves share one contract and
breaking it from either side is silent: a tile that stops sweeping still answers
the bus perfectly, and a hub that trusts the wrong signal never notices.

| Sketch | MCU | Type | Addresses | DATA_LEN |
|---|---|---|---|---|
| [`SliderModule/SliderModule.ino`](SliderModule/SliderModule.ino) | PY32F030F28U6TR | `0x01` | 0x08–0x0A | 11 |
| [`ButtonModule8/ButtonModule8.ino`](ButtonModule8/ButtonModule8.ino) | PY32F030F28U6TR | `0x02` | 0x0B–0x0D | 3 |

The hub side is `src/AlchemyUI/`; the whole arrangement is written up in
[`docs/alchemy-satellite-link.md`](../docs/alchemy-satellite-link.md).

## Building

These are Arduino sketches for the PY32Duino core, built and flashed
independently of the Pico2Seq firmware:

```bash
arduino-cli compile --fqbn PY32duino:py32:PY32F0:pnum=PY32F030F2X6 \
  tiles/SliderModule
```

Set `NUM_BUTTONS` to 4 or 8 in `ButtonModule8.ino` to match the PCB; nothing
else differs between the two button variants, and no protocol version moves.

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

- **Bus rate unified at 400 kHz** (`kBusClockHz`). The slider tile was
  programmed for 100 kHz on a bank the hub clocks at 400 kHz — configured for
  standard-mode timing while being driven at fast mode. Keep this in lockstep
  with `kTileBusFrequencyHz` in `src/app/ControlIO.cpp`.
- **Triple-buffered publish.** With two buffers a publish landing during a read
  had nowhere to go and was skipped; the skip left `activeFrame` unchanged, so
  every later publish was skipped too and SEQ, DATA and HEARTBEAT froze
  together while the slave went on answering. A third buffer means a free slot
  always exists.
- **Transaction state is cleared when the peripheral is rebuilt.**
  `servingBuf` was cleared only in `endTransaction()`, which a transaction
  killed by the bus watchdog never reaches — leaving a buffer reserved forever.
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
