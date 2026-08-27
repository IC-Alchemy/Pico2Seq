// AlchemyTiles.cpp — see AlchemyTiles.h for the contract.

#include "AlchemyTiles.h"

namespace {
// Registry addresses probed per bank, in order: the slider block then the
// button block (alchemy::kAddrSliderBase / kAddrButtonBase, three offsets
// each). Scan order is also claim order, so a rig's slot layout is stable.
constexpr std::uint8_t kScanAddresses[] = {
    alchemy::kAddrSliderBase + 0, alchemy::kAddrSliderBase + 1,
    alchemy::kAddrSliderBase + 2, alchemy::kAddrButtonBase + 0,
    alchemy::kAddrButtonBase + 1, alchemy::kAddrButtonBase + 2};
constexpr int kScanAddressCount = static_cast<int>(sizeof(kScanAddresses));
}  // namespace

AlchemyTiles::AlchemyTiles() = default;

void AlchemyTiles::setHoldMilliseconds(std::uint32_t ms) {
  holdMs_ = ms;
  TileButton::Options opt;
  opt.holdMilliseconds = ms;
  for (int slot = 0; slot < kMaxTiles; ++slot) {
    for (std::uint8_t b = 0; b < alchemy::kButtonsPerTile; ++b) {
      buttons_[slot][b].begin(opt, 0);
    }
  }
}

void AlchemyTiles::begin(TwoWire& bankA, TwoWire* bankB, std::uint32_t now) {
  buses_[0] = &bankA;
  buses_[1] = bankB;
  sliderSlot_ = -1;
  nextSlot_ = 0;
  for (int slot = 0; slot < kMaxTiles; ++slot) {
    info_[slot] = TileInfo{};
    bus_[slot] = nullptr;
    lastPollMs_[slot] = now;
    lastProbeMs_[slot] = now;
    TileButton::Options opt;
    opt.holdMilliseconds = holdMs_;
    for (std::uint8_t b = 0; b < alchemy::kButtonsPerTile; ++b) {
      buttons_[slot][b].begin(opt, now);
    }
  }
  for (std::uint16_t& fader : faders_) fader = 0;

  // Two passes so the layout is deterministic: the slider tile (there is at
  // most one — the rig has four faders) claims slot 0, then every button tile
  // that answered fills slots 1..4 in bank/address order.
  for (int pass = 0; pass < 2; ++pass) {
    const bool wantSlider = (pass == 0);
    for (int bus = 0; bus < 2 && nextSlot_ < kMaxTiles; ++bus) {
      if (buses_[bus] == nullptr) continue;
      for (int i = 0; i < kScanAddressCount && nextSlot_ < kMaxTiles; ++i) {
        alchemy::Identity id;
        if (!readIdentity(*buses_[bus], kScanAddresses[i], id)) continue;
        const bool isSlider = id.typeId == alchemy::kTypeSliderButton;
        if (isSlider != wantSlider) continue;
        bus_[nextSlot_] = buses_[bus];
        info_[nextSlot_].address = kScanAddresses[i];
        info_[nextSlot_].identity = id;
        info_[nextSlot_].present = true;
        info_[nextSlot_].lastSeq = 0xFF;  // force a full frame on first poll
        lastPollMs_[nextSlot_] = now;
        if (isSlider) sliderSlot_ = nextSlot_;
        ++nextSlot_;
        if (wantSlider) break;  // one slider tile, then move to pass 1
      }
    }
  }
  nextSlot_ = 0;
}

void AlchemyTiles::update(std::uint32_t now) {
  // Round-robin: service at most one due tile per call so a 1 kHz loop pays
  // at most one transaction pair (~470 us at 400 kHz) per pass.
  for (int step = 0; step < kMaxTiles; ++step) {
    const int slot = nextSlot_;
    nextSlot_ = (nextSlot_ + 1) % kMaxTiles;
    TileInfo& tile = info_[slot];

    if (!tile.present) {
      // Hot-plug / recovery: re-probe a previously claimed slot about once a
      // second, on the bank it was found on. Unclaimed slots stay quiet.
      if (bus_[slot] != nullptr && tile.address != 0 &&
          now - lastProbeMs_[slot] >= kReprobeIntervalMs) {
        lastProbeMs_[slot] = now;
        alchemy::Identity id;
        if (readIdentity(*bus_[slot], tile.address, id)) {
          tile.present = true;
          tile.identity = id;
          tile.busErrors = 0;
          tile.checksumErrors = 0;
          tile.lastSeq = 0xFF;
          lastPollMs_[slot] = now;
        }
      }
      continue;
    }

    if (now - lastPollMs_[slot] < kPollIntervalMs) continue;
    lastPollMs_[slot] = now;
    pollTile(slot, now);
    return;  // one tile per pass
  }
}

float AlchemyTiles::fader(std::uint8_t channel) const {
  if (!hasSlider() || channel >= alchemy::kFadersPerTile) return 0.0f;
  return static_cast<float>(faders_[channel]) / 4095.0f;
}

std::uint16_t AlchemyTiles::faderRaw(std::uint8_t channel) const {
  if (!hasSlider() || channel >= alchemy::kFadersPerTile) return 0;
  return faders_[channel];
}

// --- Private ------------------------------------------------------------------

bool AlchemyTiles::writePointer(TwoWire& bus, std::uint8_t address,
                                std::uint8_t reg) {
  bus.beginTransmission(address);
  bus.write(reg);
  // Full STOP, not a repeated start: the tile's register pointer is a plain
  // global that persists, so two STOP-terminated transactions are equivalent
  // and dodge flaky repeated-start behaviour on small slaves.
  return bus.endTransmission(true) == 0;
}

bool AlchemyTiles::readBytes(TwoWire& bus, std::uint8_t address,
                             std::uint8_t count, std::uint8_t* out) {
  const std::uint8_t got =
      static_cast<std::uint8_t>(bus.requestFrom(address, count));
  if (got != count) {
    while (bus.available() > 0) bus.read();  // drain a short read
    return false;
  }
  for (std::uint8_t i = 0; i < count; ++i) {
    out[i] = static_cast<std::uint8_t>(bus.read());
  }
  return true;
}

bool AlchemyTiles::readIdentity(TwoWire& bus, std::uint8_t address,
                                alchemy::Identity& out) {
  out = alchemy::Identity{};
  if (!writePointer(bus, address, alchemy::kRegWhoAmI)) return false;
  std::uint8_t raw[alchemy::kIdentityReadLength];
  if (!readBytes(bus, address, alchemy::kIdentityReadLength, raw)) return false;
  out = alchemy::decodeIdentity(raw, alchemy::kIdentityReadLength);
  return out.valid;
}

void AlchemyTiles::pollTile(int slot, std::uint32_t now) {
  TileInfo& tile = info_[slot];
  TwoWire* bus = bus_[slot];
  if (bus == nullptr) return;

  // Adaptive read, spec §6: one STATUS byte while the tile is idle.
  std::uint8_t status = 0;
  if (!writePointer(*bus, tile.address, alchemy::kRegStatus) ||
      !readBytes(*bus, tile.address, 1, &status)) {
    ++tile.busErrors;
    if (tile.busErrors >= kOfflineAfterBusErrors) tile.present = false;
    return;
  }
  tile.busErrors = 0;

  const alchemy::FrameStatus fs = alchemy::decodeStatus(status);
  if (!alchemy::seqChanged(fs.seq, tile.lastSeq)) {
    tile.dataChanged = false;
    return;  // idle: SEQ static, HEARTBEAT toggling — 100 us well spent
  }

  // SEQ moved: read the whole frame in ONE transaction (coherent snapshot,
  // and the read cursor passes the sticky bytes so their edges are consumed).
  const std::uint8_t dataLen = tile.identity.dataLen;
  const std::uint8_t frameLen = static_cast<std::uint8_t>(1 + dataLen + 1);
  if (!writePointer(*bus, tile.address, alchemy::kRegStatus) ||
      !readBytes(*bus, tile.address, frameLen, frame_)) {
    ++tile.busErrors;
    if (tile.busErrors >= kOfflineAfterBusErrors) tile.present = false;
    return;
  }
  tile.busErrors = 0;
  tile.lastSeq = fs.seq;
  tile.dataChanged = true;

  if (!alchemy::frameChecksumOk(frame_, dataLen)) {
    ++tile.checksumErrors;  // keep the last good frame's data
    return;
  }

  const std::uint8_t* data = frame_ + 1;
  if (tile.identity.typeId == alchemy::kTypeSliderButton &&
      dataLen >= alchemy::kSliderDataLen) {
    for (std::uint8_t ch = 0; ch < alchemy::kFadersPerTile; ++ch) {
      faders_[ch] = alchemy::decodeFader(data, ch);
    }
  }
  if (dataLen >= alchemy::kButtonDataLen) {
    const alchemy::ButtonBlock block = alchemy::decodeButtonBlock(
        data[alchemy::kDataBtnLevel], data[alchemy::kDataBtnPressed],
        data[alchemy::kDataBtnReleased]);
    for (std::uint8_t b = 0; b < alchemy::kButtonsPerTile; ++b) {
      const std::uint8_t mask = static_cast<std::uint8_t>(1u << b);
      buttons_[slot][b].update((block.level & mask) != 0,
                               (block.pressed & mask) != 0,
                               (block.released & mask) != 0, now);
    }
  }
}
