# Satellite Link — state packet format and robustness contract

How the RP2350 talks to the PY32 control satellites (SliderModule, ButtonModule8),
and what it is allowed to believe when one of them misbehaves.

- **Wire format:** [`src/AlchemyUI/src/AlchemyProto.h`](../src/AlchemyUI/src/AlchemyProto.h)
- **Link state:** [`src/AlchemyUI/src/SatelliteLink.h`](../src/AlchemyUI/src/SatelliteLink.h)
- **Bus master:** [`src/AlchemyUI/src/AlchemyTiles.cpp`](../src/AlchemyUI/src/AlchemyTiles.cpp)
- **Tests:** `tests/unit/test_alchemy_proto.cpp`, `tests/unit/test_satellite_link.cpp`,
  `tests/unit/test_alchemy_tiles.cpp` (`pico2seq_tile_tests`)

## The model

A satellite is a **cache**, not a peripheral the hub interrogates:

```
PY32 ADC + buttons
  -> local filtering / debounce          (satellite, its own sample sweep)
  -> compact state packet                (satellite, double-buffered)
  -> one I2C read                        (RP2350 Core 0 control slice)
  -> SatelliteLink                       (sequence / timeout / last-known-good)
  -> UIState, Sequencer, VoiceManager    (Core 0)
  -> lock-free handoff                   (SpscQueue, atomics)
  -> Core 1 audio render
```

The satellite updates its snapshot asynchronously; the hub reads a coherent one.
Core 1 never touches this bus and never learns it exists, so **audio never waits
for I2C recovery** — see [Why audio cannot stall](#why-audio-cannot-stall).

## The packet

One 11-byte snapshot carries a satellite's whole control state:

| Byte | Field | Notes |
|---|---|---|
| 0 | sequence counter | advances only when the satellite's own state changed |
| 1 | button bits | level bitmap, bit *n* = button *n* down |
| 2–3 | slider 0 | `uint16` little-endian, 12-bit counts |
| 4–5 | slider 1 | |
| 6–7 | slider 2 | |
| 8–9 | slider 3 | |
| 10 | status / error | `kStatusHeartbeat`, `kStatusLocalFault`, `kStatusNotReady` |

`alchemy::StatePacket` is the decoded form, with `encodeStatePacket()` /
`decodeStatePacket()` for the bytes. The layout is pinned by a test — changing it
is a wire-format change and breaks every satellite in the field.

Two things about it are worth stating plainly:

- **SEQ is an equality field.** It is a byte here so a native producer can use the
  full range, but a v2 tile fills it from a 4-bit counter that wraps at 15.
  "Changed" means *not equal*, never *greater than*.
- **The packet carries no edges.** The sticky pressed/released bytes are events,
  true for exactly the one read that consumed them. They ride alongside the packet
  (`alchemy::DecodedFrame::edges`) and are never cached, because replaying a stored
  edge invents a press that never happened.

### Relationship to the v2 register map

The PY32 firmware serves protocol v2: an identity block at `0x00`, then a
`STATUS + DATA + SUM` frame at `0x20`. `alchemy::decodeFrame()` maps that frame onto
the packet above, so the packet is the hub's single representation regardless of
which tile type produced it and **no satellite firmware change is required**. A
future PY32 that serves the 11 bytes natively needs no hub-side decode change
either, only a different read length and offset.

A button tile (TYPE `0x02`) has no faders; its slider words decode to 0. Fader
values always come from the slider tile's own link.

## Reading it: one transaction

`AlchemyTiles::pollTile()` writes the register pointer and reads
`STATUS + DATA + SUM` in a single block read. There is no `STATUS`-probe-first
adaptive read any more.

The satellite already has a coherent packet ready, so probing `STATUS` to decide
whether to read the rest buys about 1 ms of idle bus time and costs three things: a
second chance to tear the snapshot, twice the transactions on every poll that
actually matters (a moving fader advances SEQ every sweep, so the old path paid
both reads anyway), and a code path where SEQ and DATA could come from different
sweeps. At 100 kHz an 11-byte payload is nothing next to human-control bandwidth.

Tiles are still paced round-robin, one per `update()` call, so a 1 kHz control loop
never stalls servicing five satellites at once.

## The three robustness mechanisms

Exactly three, and no more. They live in `alchemy::SatelliteLink`, one instance per
slot inside `AlchemyTiles`.

### 1. Sequence counter

SEQ advances only when the satellite's own state changed. An equal SEQ means
"nothing new": the link keeps its cached values byte-for-byte, takes the status byte
(the heartbeat toggles every sweep and a fault can raise without the DATA block
moving), and publishes nothing. A re-read of an unchanged snapshot therefore never
re-triggers a control action, and its sticky edges are not delivered a second time.

An equal SEQ on its own is *not* a claim that the satellite is alive — that is the
timeout's business, below.

### 2. Timeout

If the satellite does not prove within `kLinkTimeoutMs` (100 ms) that it is still
*sampling*, the link is **Stale**. That is a statement about the *link*, not the
data: the cached values stay readable, they are just no longer known to be true.

The proof is **SEQ moving or HEARTBEAT toggling** — never merely "a packet
arrived". The tile firmware guards its double buffer with a `servingBuf` interlock
so a publish can never recycle a buffer a read is still serving; if that interlock
is ever left latched (see [Open items on the tile side](#open-items-on-the-tile-side)),
`publishFrame()` returns early forever. The I2C slave then keeps serving the last
latched frame — correct checksum, prompt ACK, every field frozen, including
HEARTBEAT. Treating arrival as liveness would call that healthy indefinitely and
hold a pre-fault snapshot in front of the audio engine, which is exactly what this
layer exists to prevent. The tile's own header says it plainly: *frozen SEQ +
toggling HEARTBEAT = idle, both frozen = wedged.*

The cost is one bit of aliasing: HEARTBEAT is a single bit, so an outage spanning an
even number of sweeps hands back a byte-identical frame and recovery takes one extra
poll (~4 ms). Invisible, and the right side of the trade.

Staleness is evaluated on every `update()` pass for every claimed slot, **not** only
when a slot's turn comes round in the polling rotation. Five slots sharing a
one-tile-per-pass rotation would otherwise let a dead satellite hold its buttons for
however long the rotation took.

Failed reads do not themselves make a link stale — only the clock does. That is what
lets a single flaky transaction pass without a visible glitch.

### 3. Last-known-good

A read that fails (NACK, short read, checksum mismatch) is dropped **whole**. No
field of it reaches the cache, so the values downstream are always a snapshot the
satellite really sent. A corrupt frame that happens to carry a plausible fader word
is exactly what must not reach the audio path.

### What Stale changes, and why it is asymmetric

| Control | While Stale | Why |
|---|---|---|
| Sliders | **hold** last-known-good | A fader is a *position*. The physical control has not moved because the wire went quiet. Utility fader 2 is master volume: collapsing to 0 on a dropped transaction silences the instrument. |
| Buttons | **release** to 0 | A button is *momentary*. A held bitmap frozen by a dead link is a stuck key — it pins Shift, latches parameter recording, keeps a transport chord armed. |

The release is not a tap. `AlchemyTiles::releaseButtons()` calls `TileButton::consume()`
before dropping the level, because nothing the player did ended that press and no
action may fire from it.

`SatelliteLink::Options::holdButtonsWhileStale` exists for a surface where holding is
the right answer. It is off by default and the tile driver does not set it.

When a satellite comes back, the link **re-publishes even if SEQ never moved**:
consumers were shown released buttons for the whole outage and have to be told the
truth again, or a button held throughout stays invisible until the next press.

## What the tile firmware guarantees

The hub's decisions lean on four properties of the PY32 sketches
(`SliderModule.ino`, `ButtonModule8.ino`), which is why they are written down here:

| Property | Why the hub depends on it |
|---|---|
| The frame is **double-buffered** and published with a single index flip; the ISR latches one buffer at ADDR-match and serves it for the whole transaction. | A frame read in one transaction is internally coherent. Splitting the read is what would tear it. |
| **HEARTBEAT toggles on every sweep**, change or no change. | The only liveness signal that distinguishes "idle" from "wedged". |
| **SEQ advances only when a DATA byte changed**, and wraps at 15. | Equality-only comparison; an unchanged SEQ means nothing new, not "older". |
| Sticky press/release bits **clear only after a master's read cursor passes them**, deferred to the tile's loop so an aborted read loses nothing. | An edge is never lost, but the same edge is served again to any read landing before the tile's next sweep — hence the hub delivers edges only on a frame its link actually published. |
| Over-reads pad with `0x00` and **never NACK mid-transaction**. | A wrong read length is safe and distinguishable from an empty bus. |

Note that each button edge costs **two** SEQ advances: one publishing the edge, one
publishing the cleared sticky bytes. Harmless here — the hub reads the whole snapshot
every poll regardless — but it doubles the "changed" rate any SEQ-triggered logic sees.

## Open items on the tile side

Neither is fixable from this repository; both are recorded here because the hub's
behaviour is shaped around them.

1. **`servingBuf` is not reset when the peripheral is re-initialised.** It is cleared
   only in `endTransaction()`. If `i2cBusWatchdog()` force-resets I2C1 mid-read, no
   STOP or NACK ever arrives for that dead transaction, so `servingBuf` stays latched
   at a buffer index. `publishFrame()` then hits `if (servingBuf == back) return;` on
   every subsequent sweep and the tile's frame — SEQ, DATA and HEARTBEAT alike —
   freezes permanently while the slave keeps answering. One line in `i2cSlaveBegin()`
   (`servingBuf = 0xFF;`) closes it. The hub's heartbeat-based timeout is what
   contains it until then.

2. **The two tiles configure different bus speeds.** `SliderModule.ino` sets
   `hi2c.Init.ClockSpeed = 100000`; `ButtonModule8.ino` sets `400000`. They share one
   bank, and the hub drives it at 400 kHz (`kTileBusFrequencyHz` in
   `src/app/ControlIO.cpp`) — while the comment on that same line, this repo's
   `AlchemyTiles.h`, and the control-surface design spec all call 100 kHz the house
   rate because "400 kHz stalls tile transfers on this rig". The tile configured for
   standard mode is exactly the one whose stalling is documented. Cheap experiment:
   set the slider tile to `400000` (or the hub to 100 kHz) and see whether the stall
   follows the setting.

## Why audio cannot stall

- The poll runs on **Core 0**, from `ControlIO::scanControls()` via
  `AlchemyControlBridge::update()`. Core 1 runs `AudioEngine::renderNextBuffer()` and
  nothing else (`Pico2Seq.ino`).
- Nothing in the poll path retries, sleeps, or spins. A failed read increments a
  counter and returns; recovery is whatever the next scheduled poll finds. The only
  `delay()` in the driver is in `begin()`'s discovery retries, which run once at
  startup. Core 1 is paced by buffer availability and is not gated on Core 0 finishing
  a control pass.
- Control values reach Core 1 the way they always did — `std::atomic` for master
  volume, `SpscQueue` for per-voice state — so a stale link changes *what* Core 1
  reads, never *when* it gets to read.

## Diagnostics

`AlchemyTiles::link(slot)` exposes the cached packet and the link's counters:
`goodPackets()`, `duplicatePackets()`, `rejectedReads()`, `staleEvents()`,
`millisecondsSinceGood()`, `localFault()`. `AlchemyTiles::info(slot)` keeps the
transport-level `busErrors` / `checksumErrors` and the present/offline flag, which is
a harder statement than stale: four consecutive bus errors take a slot offline and it
is re-probed about once a second.

## Related documentation

- [`docs/testing.md`](testing.md) — test suites and the stub sets each target builds against
- [`docs/superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md`](superpowers/specs/2026-09-01-alchemy-tile-control-surface-design.md) — what the tiles control
- [`docs/architecture.md`](architecture.md) — the dual-core split
