// AlchemyTiles.h — hub-side client for Alchemy Modular UI I2C tiles
// ---------------------------------------------------------------------------
// One RP2350 host, up to two Qwiic banks, and the tiles that answer on the
// platform registry (slider 0x08-0x0A, button 0x0B-0x0D). This driver:
//
//   - scans both banks at begin() and classifies whatever answers by TYPE_ID,
//     so it does not care which strap offset a tile was built with (an
//     unstrapped tile floats to its type's offset 2);
//   - polls each present tile by reading its whole snapshot in ONE
//     transaction — STATUS+DATA+SUM, checksum-verified, decoded into the
//     canonical 11-byte StatePacket (AlchemyProto.h);
//   - holds each satellite's state in a SatelliteLink, which is what decides
//     how much of a cached packet the firmware is allowed to believe
//     (sequence counter, timeout, last-known-good — SatelliteLink.h);
//   - decodes faders and buttons, turning sticky edges into TileButton
//     press/hold/tap state;
//   - never blocks longer than one transaction pair per update() call,
//     pacing tiles round-robin so a 1 kHz control loop is never stalled
//     servicing five tiles at once.
//
// Why one transaction and not the spec §6 adaptive read: the satellite is a
// cache. It samples, filters and debounces on its own schedule and keeps a
// coherent packet ready, so reading STATUS first to decide whether to read
// the rest saves ~1 ms of bus time per idle poll and costs a second chance
// to tear the snapshot, twice the transactions on every poll that matters,
// and a code path where SEQ and DATA came from different sweeps. At 100 kHz
// an 11-byte payload is nothing next to human-control bandwidth. Read it all,
// every time.
//
// Transaction shape follows the PY32 slave's contract (I2CSliderReader
// precedent): a pointer write terminated with STOP, then a separate read.
// The tile's register pointer persists between transactions, and each frame
// read happens in ONE transaction so the double-buffered snapshot it serves
// is internally coherent. Never split a STATUS+DATA+SUM read in two.
//
// Nothing in the poll path retries, sleeps, or spins: a failed read drops the
// frame and returns. Recovery is whatever the next scheduled poll finds. The
// audio core never waits on this bus because it never touches it — poll from
// the control core only (Pico2Seq runs this from Core 0's control slice, via
// AlchemyControlBridge; audio owns Core 1 alone).
//
// The bus clock is the sketch's business (it is shared with the OLED, the
// TMAG5273 and the VL53L1X): call Wire.setClock() before begin(). Whatever
// rate it picks, every tile on the bank must be programmed for the same one —
// a tile configured for standard-mode timing while the hub clocks it at fast
// mode is the stall this comment used to blame on 400 kHz itself. The rig runs
// 400 kHz, and tiles/*/*.ino say so too.

#ifndef ALCHEMY_UI_TILES_H
#define ALCHEMY_UI_TILES_H

#include <Arduino.h>
#include <Wire.h>

#include "AlchemyProto.h"
#include "SatelliteLink.h"
#include "TileButton.h"

class AlchemyTiles {
 public:
  static constexpr int kMaxTiles = 5;  // 1 slider + up to 4 button tiles
  static constexpr std::uint32_t kPollIntervalMs = 4;  // ~250 Hz tier B
  static constexpr std::uint32_t kReprobeIntervalMs = 1000;
  static constexpr std::uint8_t kOfflineAfterBusErrors = 4;
  // No verified packet for this long and the link is stale: buttons release,
  // faders hold their last-known-good position. Long enough to ride out a
  // handful of missed polls at kPollIntervalMs, short enough that a dead
  // satellite cannot hold a button down past the point a player notices.
  static constexpr std::uint32_t kLinkTimeoutMs = 100;
  // Discovery runs only in begin(). Patient retries make power-up ordering
  // and one-off NACK/short-read failures much less likely to hide a tile.
  static constexpr std::uint8_t kDiscoveryAttempts = 4;
  static constexpr std::uint32_t kDiscoveryRetryDelayMs = 10;

  struct TileInfo {
    bool present = false;
    std::uint8_t address = 0;
    alchemy::Identity identity{};
    std::uint32_t checksumErrors = 0;
    std::uint32_t busErrors = 0;
    std::uint8_t lastSeq = 0;
    bool dataChanged = false;  // the most recent poll published new state
  };

  AlchemyTiles();

  /**
   * Scan the registry addresses on the given banks and claim tiles. bankB may
   * be nullptr for a single-bank rig; four button tiles need both banks
   * because the registry gives each type three offsets per bank (spec §4.2).
   */
  void begin(TwoWire& bankA, TwoWire* bankB, std::uint32_t now);

  /** Poll due tiles. Call from the control loop; one tile per pass at most. */
  void update(std::uint32_t now);

  void setHoldMilliseconds(std::uint32_t ms);

  [[nodiscard]] int tileCount() const { return kMaxTiles; }
  [[nodiscard]] const TileInfo& info(int slot) const { return info_[slot]; }

  /** Cached control state and link health for a slot, for diagnostics. */
  [[nodiscard]] const alchemy::SatelliteLink& link(int slot) const {
    return links_[slot];
  }

  /** Count of tiles currently marked present. */
  [[nodiscard]] int presentTileCount() const {
    int count = 0;
    for (int slot = 0; slot < kMaxTiles; ++slot) {
      if (info_[slot].present) ++count;
    }
    return count;
  }

  /** True when any tile (slider or button) answered and is present. */
  [[nodiscard]] bool hasTile() const { return presentTileCount() > 0; }

  /** True when the slot holds a present slider tile. */
  [[nodiscard]] bool hasSlider() const { return sliderSlot_ >= 0 && info_[sliderSlot_].present; }

  /** Slot the slider tile claimed, or -1 when no slider tile answered. */
  [[nodiscard]] int sliderSlot() const { return hasSlider() ? sliderSlot_ : -1; }

  /**
   * Lowest slot holding a present tile of the given TYPE_ID, or -1. Callers
   * that need "the button tile" should ask by type rather than assume a fixed
   * slot: scan order shifts when a tile is missing at begin().
   */
  [[nodiscard]] int firstSlotOfType(std::uint8_t typeId) const {
    for (int slot = 0; slot < kMaxTiles; ++slot) {
      if (info_[slot].present && info_[slot].identity.typeId == typeId) return slot;
    }
    return -1;
  }

  /** Fader 0..3 as 0..1 from the slider tile (0 when absent). */
  [[nodiscard]] float fader(std::uint8_t channel) const;

  /**
   * Fader 0..3 as raw 12-bit counts from the slider tile.
   *
   * This is last-known-good, not "the last read that happened to work": a
   * tile that has gone offline keeps serving the position its fader was
   * physically at, because that is still where the fader is. Collapsing to 0
   * on a dropped transaction would slam master volume (utility fader 2) to
   * silence on a flaky bus, which is precisely the propagation the link
   * layer exists to stop. Zero here means only "no slider tile ever
   * answered".
   */
  [[nodiscard]] std::uint16_t faderRaw(std::uint8_t channel) const;

  /**
   * Button state for a tile slot (index 0..3). The slot order is scan order:
   * slider tile first (if found), then button tiles by bus and address.
   */
  TileButton& button(int slot, std::uint8_t index) { return buttons_[slot][index]; }

 private:
  bool readIdentity(TwoWire& bus, std::uint8_t address, alchemy::Identity& out);
  bool writePointer(TwoWire& bus, std::uint8_t address, std::uint8_t reg);
  bool readBytes(TwoWire& bus, std::uint8_t address, std::uint8_t count,
                 std::uint8_t* out);
  void pollTile(int slot, std::uint32_t now);
  /** DATA_LEN to read for a slot, clamped to the largest frame we can hold. */
  [[nodiscard]] std::uint8_t frameDataLen(int slot) const;
  /** Record a failed read and take the slot offline once they pile up. */
  void noteBusError(int slot, std::uint32_t now);
  /** Drop every held button on this slot without firing a tap. */
  void releaseButtons(int slot, std::uint32_t now);

  TwoWire* buses_[2] = {nullptr, nullptr};
  TwoWire* bus_[kMaxTiles] = {nullptr, nullptr, nullptr, nullptr, nullptr};
  TileInfo info_[kMaxTiles];
  // One cached control-state snapshot per satellite. This is the only place
  // fader positions and button levels live; the driver keeps no parallel copy
  // that could disagree with it.
  alchemy::SatelliteLink links_[kMaxTiles];
  TileButton buttons_[kMaxTiles][alchemy::kButtonsPerTile];
  std::uint32_t lastPollMs_[kMaxTiles] = {0, 0, 0, 0, 0};
  std::uint32_t lastProbeMs_[kMaxTiles] = {0, 0, 0, 0, 0};
  std::uint8_t frame_[1 + alchemy::kSliderDataLen + 1] = {0};
  int sliderSlot_ = -1;
  int nextSlot_ = 0;
  std::uint32_t holdMs_ = 400;
};

#endif  // ALCHEMY_UI_TILES_H
