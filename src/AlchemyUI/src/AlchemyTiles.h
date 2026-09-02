// AlchemyTiles.h — hub-side client for Alchemy Modular UI I2C tiles
// ---------------------------------------------------------------------------
// One RP2350 host, up to two Qwiic banks, and the tiles that answer on the
// platform registry (slider 0x08-0x0A, button 0x0B-0x0D). This driver:
//
//   - scans both banks at begin() and classifies whatever answers by TYPE_ID,
//     so it does not care which strap offset a tile was built with (an
//     unstrapped tile floats to its type's offset 2);
//   - polls each present tile at ~200 Hz with the spec §6 adaptive read:
//     one STATUS byte (100 us) while SEQ is static, the whole
//     STATUS+DATA+SUM frame (checksum-verified) the moment SEQ moves;
//   - decodes faders and buttons, turning sticky edges into TileButton
//     press/hold/tap state;
//   - never blocks longer than one transaction pair (~1.9 ms at 100 kHz) per
//     update() call, pacing tiles round-robin so a 1 kHz control loop is
//     never stalled servicing five tiles at once.
//
// Transaction shape follows the PY32 slave's contract (I2CSliderReader
// precedent): a pointer write terminated with STOP, then a separate read.
// The tile's register pointer persists between transactions, and each frame
// read happens in ONE transaction so the double-buffered snapshot it serves
// is internally coherent. Never split a STATUS+DATA+SUM read in two.
//
// The bus clock is the sketch's business (it is shared with the OLED, the
// TMAG5273 and the VL53L1X): call Wire.setClock() before begin(). 100 kHz is
// the house rate — 400 kHz stalls transfers on this rig.
//
// Core 1 only. This is control-surface I/O; nothing here may run on the
// audio core (see the repo's Docs/realtime_rules.md).

#ifndef ALCHEMY_UI_TILES_H
#define ALCHEMY_UI_TILES_H

#include <Arduino.h>
#include <Wire.h>

#include "AlchemyProto.h"
#include "TileButton.h"

class AlchemyTiles {
 public:
  static constexpr int kMaxTiles = 5;  // 1 slider + up to 4 button tiles
  static constexpr std::uint32_t kPollIntervalMs = 4;  // ~250 Hz tier B
  static constexpr std::uint32_t kReprobeIntervalMs = 1000;
  static constexpr std::uint8_t kOfflineAfterBusErrors = 4;
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
    bool dataChanged = false;  // the most recent poll saw SEQ advance
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

  /** Fader 0..3 as raw 12-bit counts from the slider tile. */
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

  TwoWire* buses_[2] = {nullptr, nullptr};
  TwoWire* bus_[kMaxTiles] = {nullptr, nullptr, nullptr, nullptr, nullptr};
  TileInfo info_[kMaxTiles];
  TileButton buttons_[kMaxTiles][alchemy::kButtonsPerTile];
  // Last decoded level bitmap per slot. Idle polls (SEQ static) carry no
  // frame data, but TileButton::update() must still be fed every poll or a
  // sustained press never ages into longPress().
  std::uint8_t lastButtonLevels_[kMaxTiles] = {0, 0, 0, 0, 0};
  std::uint16_t faders_[alchemy::kFadersPerTile] = {0, 0, 0, 0};
  std::uint32_t lastPollMs_[kMaxTiles] = {0, 0, 0, 0, 0};
  std::uint32_t lastProbeMs_[kMaxTiles] = {0, 0, 0, 0, 0};
  std::uint8_t frame_[1 + alchemy::kSliderDataLen + 1] = {0};
  int sliderSlot_ = -1;
  int nextSlot_ = 0;
  std::uint32_t holdMs_ = 400;
};

#endif  // ALCHEMY_UI_TILES_H
