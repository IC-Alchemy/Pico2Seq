// SatelliteLink.h — cached control state for one PY32 satellite
// ---------------------------------------------------------------------------
// A satellite is a cache, not a peripheral the hub interrogates. It samples
// its own ADCs and buttons, filters and debounces them locally, and keeps a
// coherent StatePacket ready. The RP2350 reads that whole snapshot in one
// transaction and does nothing else on the bus.
//
// This class is the hub's half of that arrangement: it holds the last packet
// the satellite served and decides how much of it the rest of the firmware is
// allowed to believe. Exactly three mechanisms, no more:
//
//   1. Sequence counter — SEQ advances only when the satellite's own state
//      changed. Equal SEQ means "alive, nothing new", so a re-read of an
//      unchanged snapshot never re-publishes and never re-fires an edge.
//      Equality only: the counter wraps, so "newer" has no meaning.
//
//   2. Timeout — if the satellite does not prove within timeoutMs that it is
//      still sampling, the link is Stale. The proof is SEQ moving or
//      HEARTBEAT toggling, never merely "a packet arrived": the tile firmware
//      has a path where its publish stalls while the I2C peripheral keeps
//      serving the last frame, checksum and all, indefinitely. Counting that
//      as liveness would freeze a snapshot into the audio path and call it
//      healthy. Stale is a statement about the *link*, not about the data:
//      the cached values stay readable, they are just no longer known to be
//      true.
//
//   3. Last-known-good — a failed read (NACK, short read, bad checksum) is
//      dropped whole. It never partially overwrites the cache, so the values
//      the audio path sees are always a snapshot the satellite really sent.
//
// What Stale changes, and why it is asymmetric:
//
//   - Sliders HOLD their last-known-good position. A fader is a position; the
//      physical control has not moved just because the wire went quiet, and
//      zeroing it would slam master volume or cutoff to nothing on a dropped
//      transaction — the exact garbage this layer exists to stop.
//   - Buttons RELEASE to 0 (the default; see Options::holdButtonsWhileStale).
//      A button is momentary. A held bitmap frozen by a dead link is a stuck
//      key: it latches parameter recording, pins Shift, keeps a transport
//      chord armed. Releasing is the safe failure, holding is not.
//
// Recovery re-publishes even when SEQ has not moved, because consumers were
// told the buttons were up while the link was Stale and must be re-synced to
// what the satellite actually reads.
//
// Nothing here blocks, retries, sleeps, or allocates: it is a few integer
// compares over fixed storage. Recovery is driven entirely by the next poll
// that happens to succeed, so the audio core never waits on I2C — it never
// learns there was a problem at all.
//
// Pure C++ (milliseconds arrive as arguments), so the host suite drives it
// without hardware: tests/unit/test_satellite_link.cpp.

#ifndef ALCHEMY_UI_SATELLITE_LINK_H
#define ALCHEMY_UI_SATELLITE_LINK_H

#include <cstdint>

#include "AlchemyProto.h"

namespace alchemy {

class SatelliteLink {
 public:
  enum class State : std::uint8_t {
    Init,   // nothing verified yet since begin(); the cache is all zeroes
    Fresh,  // a verified packet arrived inside the timeout
    Stale,  // the timeout elapsed; still serving last-known-good
  };

  struct Options {
    // A tile is polled every few milliseconds, so this tolerates a run of
    // missed polls without tripping on one unlucky NACK. Well under the
    // ~100 ms at which a human notices a control stopped responding.
    std::uint32_t timeoutMs = 100;
    // Off by default: a momentary control must not stay down on a dead link.
    bool holdButtonsWhileStale = false;
  };

  void begin(const Options& options, std::uint32_t nowMilliseconds) {
    opt_ = options;
    cache_ = StatePacket{};
    state_ = State::Init;
    hasGood_ = false;
    resyncPending_ = false;
    lastGoodMilliseconds_ = nowMilliseconds;
    goodPackets_ = 0;
    duplicatePackets_ = 0;
    rejectedReads_ = 0;
    staleEvents_ = 0;
  }

  /**
   * Accept a packet whose checksum already passed.
   *
   * @return true when the caller should publish this state: SEQ moved, this
   *         is the first packet since begin(), or the link is recovering from
   *         Stale and consumers need re-syncing. Always false while Stale —
   *         a link the timeout does not trust publishes nothing.
   */
  bool onPacket(const StatePacket& packet, std::uint32_t nowMilliseconds) {
    const bool first = !hasGood_;
    const bool seqMoved = seqChanged(packet.seq, cache_.seq);
    // HEARTBEAT toggles on every one of the satellite's sample sweeps, whether
    // or not anything changed. Together with SEQ it is the only evidence that
    // the satellite is still *sampling*, as opposed to merely still answering:
    // a tile whose publish path has stalled keeps serving its last frame,
    // checksum and all, forever. Refreshing the timeout on "a packet arrived"
    // would call that healthy and hand the audio path a frozen snapshot.
    const bool heartbeatToggled =
        ((packet.status ^ cache_.status) & kStatusHeartbeat) != 0;
    const bool sweeping = first || seqMoved || heartbeatToggled;

    if (seqMoved || first) {
      cache_ = packet;
    } else {
      // Same snapshot, re-read. Keep the cached values byte-for-byte and take
      // only the flags: HEARTBEAT toggles every sweep and a fault can raise
      // without SEQ moving.
      cache_.status = packet.status;
      ++duplicatePackets_;
    }

    hasGood_ = true;
    ++goodPackets_;
    if (sweeping) {
      lastGoodMilliseconds_ = nowMilliseconds;
    }
    evaluate(nowMilliseconds);

    // A frozen tile answers every poll and never gets here: it cannot refresh
    // the timeout, so it ages into Stale and stays there.
    if (state_ != State::Fresh) return false;

    const bool republish = first || seqMoved || resyncPending_;
    if (republish) resyncPending_ = false;
    return republish;
  }

  /**
   * A read failed: NACK, short read, or a checksum that did not verify. The
   * cache is untouched — a corrupt frame publishes nothing. Only the timeout
   * decides when the cached values stop being trusted.
   */
  void onFailure(std::uint32_t nowMilliseconds) {
    ++rejectedReads_;
    evaluate(nowMilliseconds);
  }

  /**
   * Age the link. Call every control-loop pass, including passes where this
   * satellite was not the one polled, or a link whose satellite fell off the
   * bus never times out.
   */
  void tick(std::uint32_t nowMilliseconds) { evaluate(nowMilliseconds); }

  [[nodiscard]] State state() const { return state_; }
  [[nodiscard]] bool fresh() const { return state_ == State::Fresh; }
  [[nodiscard]] bool stale() const { return state_ == State::Stale; }

  /**
   * Button level bitmap. Zero while Stale unless holdButtonsWhileStale, and
   * zero before the first packet: an un-heard-from satellite holds nothing.
   */
  [[nodiscard]] std::uint8_t buttons() const {
    if (state_ == State::Fresh || opt_.holdButtonsWhileStale) return cache_.buttons;
    return 0;
  }

  /** Last-known-good slider position, held across a stale link. */
  [[nodiscard]] std::uint16_t slider(std::uint8_t channel) const {
    if (channel >= kPacketSlidersPerTile) return 0;
    return cache_.sliders[channel];
  }

  /** STATUS flags from the most recent verified packet. */
  [[nodiscard]] std::uint8_t status() const { return cache_.status; }

  /** The satellite reported a fault of its own in its last verified packet. */
  [[nodiscard]] bool localFault() const { return packetLocalFault(cache_); }

  /** The whole cached snapshot, for diagnostics pages and scan reports. */
  [[nodiscard]] const StatePacket& lastKnownGood() const { return cache_; }

  /** Age of the last verified packet; 0 before the first one arrives. */
  [[nodiscard]] std::uint32_t millisecondsSinceGood(std::uint32_t nowMilliseconds) const {
    return hasGood_ ? nowMilliseconds - lastGoodMilliseconds_ : 0;
  }

  [[nodiscard]] std::uint32_t goodPackets() const { return goodPackets_; }
  [[nodiscard]] std::uint32_t duplicatePackets() const { return duplicatePackets_; }
  [[nodiscard]] std::uint32_t rejectedReads() const { return rejectedReads_; }
  /** Times this link has crossed from Fresh into Stale since begin(). */
  [[nodiscard]] std::uint32_t staleEvents() const { return staleEvents_; }

 private:
  void evaluate(std::uint32_t nowMilliseconds) {
    if (!hasGood_) {
      state_ = State::Init;
      return;
    }
    const bool expired =
        (nowMilliseconds - lastGoodMilliseconds_) >= opt_.timeoutMs;
    if (!expired) {
      state_ = State::Fresh;
      return;
    }
    if (state_ != State::Stale) {
      ++staleEvents_;
      // Whatever the satellite reads when it comes back has to be published
      // even if its SEQ never moved, because consumers were shown released
      // buttons for the whole outage.
      resyncPending_ = true;
    }
    state_ = State::Stale;
  }

  Options opt_{};
  StatePacket cache_{};
  State state_ = State::Init;
  bool hasGood_ = false;
  bool resyncPending_ = false;
  std::uint32_t lastGoodMilliseconds_ = 0;
  std::uint32_t goodPackets_ = 0;
  std::uint32_t duplicatePackets_ = 0;
  std::uint32_t rejectedReads_ = 0;
  std::uint32_t staleEvents_ = 0;
};

}  // namespace alchemy

#endif  // ALCHEMY_UI_SATELLITE_LINK_H
