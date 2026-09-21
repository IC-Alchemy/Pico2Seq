#pragma once

#include "display_link.h"
#include <stdint.h>

#include "../ui/UIState.h"
#include "../app/SequencerView.h"

class VoiceManager;

/**
 * @brief Hub-side link to the round display panel (PY32 + GC9A01 at 0x3E).
 *
 * Serializes the winning UI page — the same priority chain the OLED uses —
 * into rdisplay::PAGE_FRAME packets and pushes them over the main Wire bus.
 * The Pico decides WHAT is on the panel, the PY32 decides HOW it is drawn;
 * this class never sends pixels, only the fixed-size state packets defined
 * in display_link.h.
 *
 * Design: docs/superpowers/plans/2026-09-20-round-display-link-option-b.md
 * (§3 protocol, §4 wiring, §6 page table). The page chain mirrors
 * src/OLED/oled.cpp:243-506 gate order exactly.
 *
 * Send discipline (tile-proven patterns, plan §2):
 *   - a frame goes out only when the serialized bytes differ from the
 *     last-sent shadow, plus a 2 Hz heartbeat re-sending the identical
 *     bytes with the same 4-bit SEQ (idempotent on the panel);
 *   - SEQ advances only when the content changed, never on a heartbeat;
 *   - a failed send leaves the shadow dirty, so the next 40 ms display tick
 *     retries — never an in-line retry;
 *   - with the panel absent, begin() fails and update() re-probes at most
 *     every 1000 ms (hot-plug support) and sends nothing meanwhile.
 */
class RoundDisplayLink
{
public:
  /** Probe WHOAMI at rdisplay::kDisplayAddress; false (panel absent) is a
   * normal, non-fatal outcome — update() keeps probing. */
  bool begin();

  /** Build the winning page's frame and send it if it differs from the
   * panel's shadow or a heartbeat is due. Safe to call when absent. */
  void update(const UIState &uiState, const SequencerView &sequencers,
              VoiceManager *voiceManager);

  /** Invalidate the shadow: the next update() rebuilds and re-sends the
   * frame (same SEQ — an identical re-send is idempotent on the panel). */
  void clear();

  /** True once begin() (or a later re-probe) saw the panel. */
  bool isInitialized() const { return present_; }

  /** Debug: register-pointer reads of WHOAMI/TYPE_ID/PROTO_VER. True when
   * the panel answered with the expected magic and type. */
  bool probeIdentity(uint8_t &typeId, uint8_t &protoVer);

  uint32_t sentFrames() const { return sentFrames_; }
  uint32_t sendFailures() const { return sendFailures_; }

private:
  bool probe();
  bool sendFrame(const uint8_t *bytes, size_t len, uint8_t seq);

  bool present_ = false;
  uint32_t lastProbeMs_ = 0;

  // Last-sent frame bytes with the SEQ byte (offset 1) zeroed: the shadow
  // compares page content only, so a heartbeat with a different SEQ byte
  // still counts as "unchanged".
  uint8_t lastSent_[rdisplay::kMaxFrameBytes] = {0};
  size_t lastSentLen_ = 0;
  uint8_t lastSeq_ = 0;
  bool dirty_ = true; // nothing sent yet — first frame always goes out
  uint32_t lastSendMs_ = 0;

  uint32_t sentFrames_ = 0;
  uint32_t sendFailures_ = 0;
};
