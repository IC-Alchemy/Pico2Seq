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

alchemy::SatelliteLink::Options linkOptions() {
  alchemy::SatelliteLink::Options opt;
  opt.timeoutMs = AlchemyTiles::kLinkTimeoutMs;
  return opt;
}
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
    links_[slot].begin(linkOptions(), now);
    lastPollMs_[slot] = now;
    lastProbeMs_[slot] = now;
    TileButton::Options opt;
    opt.holdMilliseconds = holdMs_;
    for (std::uint8_t b = 0; b < alchemy::kButtonsPerTile; ++b) {
      buttons_[slot][b].begin(opt, now);
    }
  }

  // Two passes so the layout is deterministic: the slider tile (there is at
  // most one — the rig has four faders) claims slot 0, then every button tile
  // that answered fills slots 1..4 in bank/address order.
  for (int pass = 0; pass < 2; ++pass) {
    const bool wantSlider = (pass == 0);
    for (int bus = 0; bus < 2 && nextSlot_ < kMaxTiles; ++bus) {
      if (buses_[bus] == nullptr) continue;
      for (int i = 0; i < kScanAddressCount && nextSlot_ < kMaxTiles; ++i) {
        alchemy::Identity id;
        // Discovery runs only here, and a slot that never answered is never
        // re-probed (the hot-plug path in update() needs an address claimed
        // at begin()). Retry patiently so power-up ordering or a single
        // NACK/short read cannot hide a tile for the whole session.
        bool identified = false;
        for (std::uint8_t attempt = 0; attempt < kDiscoveryAttempts; ++attempt) {
          if (readIdentity(*buses_[bus], kScanAddresses[i], id)) {
            identified = true;
            break;
          }
          if (attempt + 1 < kDiscoveryAttempts) {
            delay(kDiscoveryRetryDelayMs);
          }
        }
        if (!identified) continue;
        const bool isSlider = id.typeId == alchemy::kTypeSliderButton;
        if (isSlider != wantSlider) continue;
        bus_[nextSlot_] = buses_[bus];
        info_[nextSlot_].address = kScanAddresses[i];
        info_[nextSlot_].identity = id;
        info_[nextSlot_].present = true;
        info_[nextSlot_].lastSeq = 0;
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
  // Age every claimed link first. This costs no bus time and must happen on
  // every pass, not only on a slot's turn in the rotation: a satellite that
  // fell off the bus has to time out on the clock, not on whether the driver
  // got round to asking it again.
  for (int slot = 0; slot < kMaxTiles; ++slot) {
    if (bus_[slot] == nullptr) continue;
    const bool wasFresh = links_[slot].fresh();
    links_[slot].tick(now);
    if (wasFresh && links_[slot].stale()) {
      // The link just timed out. Its cached fader positions stay valid (the
      // faders have not moved), but its buttons must not stay down.
      releaseButtons(slot, now);
    }
  }

  // Round-robin: service at most one due tile per call so a 1 kHz loop pays
  // at most one transaction pair per pass.
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
  return static_cast<float>(faderRaw(channel)) / 4095.0f;
}

std::uint16_t AlchemyTiles::faderRaw(std::uint8_t channel) const {
  // Deliberately not gated on present(): see the header. A claimed slider
  // tile keeps serving its last-known-good position while its link is down.
  if (sliderSlot_ < 0 || channel >= alchemy::kFadersPerTile) return 0;
  return links_[sliderSlot_].slider(channel);
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

std::uint8_t AlchemyTiles::frameDataLen(int slot) const {
  // DATA_LEN is whatever the tile answered the registry with and is never
  // re-validated by the slave, so clamp it to the largest frame the protocol
  // defines (the slider tile's) — a tile reporting a bigger payload would
  // otherwise read past frame_ and corrupt whatever sits next to it.
  const std::uint8_t declared = info_[slot].identity.dataLen;
  return declared <= alchemy::kSliderDataLen ? declared : alchemy::kSliderDataLen;
}

void AlchemyTiles::noteBusError(int slot, std::uint32_t now) {
  TileInfo& tile = info_[slot];
  ++tile.busErrors;
  links_[slot].onFailure(now);
  if (tile.busErrors >= kOfflineAfterBusErrors) {
    tile.present = false;
    // Offline is a harder statement than stale, and it can be reached before
    // the timeout expires. Drop the holds now; the cached fader positions are
    // still served, and the reprobe path brings the tile back.
    releaseButtons(slot, now);
  }
}

void AlchemyTiles::releaseButtons(int slot, std::uint32_t now) {
  for (std::uint8_t b = 0; b < alchemy::kButtonsPerTile; ++b) {
    // consume() first: a press that ends because the wire went quiet is not a
    // tap and must not fire one. The level still has to fall so nothing stays
    // latched as held.
    buttons_[slot][b].consume();
    buttons_[slot][b].update(false, false, false, now);
  }
}

void AlchemyTiles::pollTile(int slot, std::uint32_t now) {
  TileInfo& tile = info_[slot];
  TwoWire* bus = bus_[slot];
  if (bus == nullptr) return;

  // The whole snapshot, one transaction. The satellite keeps a coherent
  // packet ready, so there is nothing to gain by asking it about STATUS first
  // and everything to lose: two reads can straddle two sample sweeps.
  const std::uint8_t dataLen = frameDataLen(slot);
  const std::uint8_t frameLen = static_cast<std::uint8_t>(1 + dataLen + 1);
  if (!writePointer(*bus, tile.address, alchemy::kRegStatus) ||
      !readBytes(*bus, tile.address, frameLen, frame_)) {
    noteBusError(slot, now);
    return;
  }
  tile.busErrors = 0;

  const alchemy::DecodedFrame decoded =
      alchemy::decodeFrame(tile.identity.typeId, frame_, dataLen);
  if (!decoded.valid) {
    // A frame that did not verify is dropped whole: no field of it reaches
    // the cache, so the values downstream stay a snapshot the tile really
    // sent. Not a bus error — the transaction itself worked.
    ++tile.checksumErrors;
    links_[slot].onFailure(now);
    return;
  }

  tile.lastSeq = decoded.packet.seq;
  tile.dataChanged = links_[slot].onPacket(decoded.packet, now);

  // Levels come from the link (so a recovering or stale link's view wins);
  // sticky edges come straight from this frame and are never cached — an
  // edge is true for exactly the one read that consumed it, and replaying a
  // stored one would invent a press that never happened.
  const std::uint8_t level = links_[slot].buttons();
  for (std::uint8_t b = 0; b < alchemy::kButtonsPerTile; ++b) {
    const std::uint8_t mask = static_cast<std::uint8_t>(1u << b);
    buttons_[slot][b].update((level & mask) != 0,
                             (decoded.edges.pressed & mask) != 0,
                             (decoded.edges.released & mask) != 0, now);
  }
}
