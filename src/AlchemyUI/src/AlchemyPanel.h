// AlchemyPanel.h — the tiles, composed into the Pico2Seq control surface
// ---------------------------------------------------------------------------
// A thin header-only layer: one AlchemyTiles driver plus the ButtonMap table,
// addressed by logical name. Sketches say panel.button(Btn::Play).pressEdge()
// and never think about slots, buses, or strap offsets again.
//
//   AlchemyPanel panel;
//   Wire.setClock(400000);            // sketch owns the shared bus
//   panel.begin(Wire, /*bankB=*/&Wire1, millis());
//   panel.update(millis());           // each control-loop pass
//   if (panel.button(Btn::Play).pressEdge()) ...
//   const float tempo = panel.fader(0);
//
// present(Btn) reports whether the tile under a button answered the last
// scan, so an OLED can grey out controls whose tile is missing instead of
// leaving dead buttons unexplained.

#ifndef ALCHEMY_UI_PANEL_H
#define ALCHEMY_UI_PANEL_H

#include "AlchemyTiles.h"
#include "ButtonMap.h"

class AlchemyPanel {
 public:
  void begin(TwoWire& bankA, TwoWire* bankB, std::uint32_t now) {
    tiles_.begin(bankA, bankB, now);
  }

  /** Poll due tiles; call each control-loop pass (see AlchemyTiles::update). */
  void update(std::uint32_t now) { tiles_.update(now); }

  /** Long-press threshold for every panel button. */
  void setHoldMilliseconds(std::uint32_t ms) { tiles_.setHoldMilliseconds(ms); }

  /** Edge state for one logical button. */
  TileButton& button(Btn b) {
    const ButtonMapping& m = kButtonMap[static_cast<std::uint8_t>(b)];
    return tiles_.button(m.slot, m.bit);
  }

  /** True when the tile providing this button answered the scan. */
  [[nodiscard]] bool present(Btn b) const {
    const ButtonMapping& m = kButtonMap[static_cast<std::uint8_t>(b)];
    return tiles_.info(m.slot).present;
  }

  /** Fader 0..3 as 0..1 (tempo/cutoff/resonance/balance in the sketches). */
  [[nodiscard]] float fader(std::uint8_t channel) const {
    return tiles_.fader(channel);
  }

  /** Raw driver, for diagnostics pages and scan reports. */
  AlchemyTiles& tiles() { return tiles_; }
  [[nodiscard]] const AlchemyTiles& tiles() const { return tiles_; }

 private:
  AlchemyTiles tiles_;
};

#endif  // ALCHEMY_UI_PANEL_H
