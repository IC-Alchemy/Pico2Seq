#ifndef ALCHEMY_CONTROL_BRIDGE_H
#define ALCHEMY_CONTROL_BRIDGE_H

#include <Arduino.h>
#include <Wire.h>

#include "../AlchemyUI/src/AlchemyPanel.h"
#include "ControlSurfaceLogic.h"
#include "UIState.h"

class Sequencer;
class SequencerView;

/**
 * @brief Glue between the Alchemy tile panel and the existing firmware UI.
 *
 * Owns the AlchemyPanel and translates raw tile edges/fader moves into calls
 * to the same handler code the matrix buttons used to run (ButtonHandlers,
 * UIEventHandler entry points). Not unit-tested — all decisions worth testing
 * live in ControlSurfaceLogic; everything here is hardware-bound translation.
 *
 * Call begin() from setup1() (after Wire1 pins/clock are configured) and
 * update() from the 1 ms control slice of loop1(), alongside Matrix_scan().
 * One update() pass never blocks longer than one tile transaction (~1.9 ms
 * at 100 kHz) because AlchemyTiles paces tiles round-robin.
 *
 * Semantics implemented here (see docs/superpowers/specs/
 * 2026-09-01-alchemy-tile-control-surface-design.md):
 *   - GP7 mode strap, software-debounced, drives the Param/Utility tile
 *     function sets; a flip clears holds/latches, flashes a control LED and
 *     raises the OLED banner flag.
 *   - SliderModule buttons: Voice1..4 direct select in both modes; with
 *     Shift held they become transport chords (Play/Stop, Randomize,
 *     Scale, Voice editor). Shift + hold Voice 4 toggles Arpeggiator mode
 *     instead of opening the editor, so a tap and a hold never fire together.
 *   - ButtonModule8: parameter set (Note..Slide) or utility set (Play,
 *     Delay, Scale, Swing, Theme, Encoder, Randomize) per mode; Shift is
 *     bit 7 in both. In Utility mode, Shift + Randomize clears the selected
 *     voice's whole pattern (tap) or every voice's pattern (hold).
 *   - Arpeggiator mode (docs/arpeggiator.md) replaces the step-shaped sets:
 *     the Param panel is the six arp patterns plus Latch, and the Utility
 *     panel keeps Play/Session/Scale/Theme while Octave range, Re-sync and
 *     Randomize-chord take the step-only slots.
 *   - Faders: step-parameter recording in Param mode (same recording path
 *     as the lidar), tempo/delay-mix/master-volume/gate-length otherwise;
 *     in Arpeggiator mode the same four faders set arp range, gate, swing
 *     and filter (ControlSurface::FaderMap::arpAssignmentFor).
 */
class AlchemyControlBridge
{
public:
  /**
   * Scan the tile bus and claim tiles. The GP7 strap is read to seed the
   * starting mode; call after pinMode(INPUT_PULLUP) on the strap pin.
   * @param modeSwitchPin GP-pin number of the mode strap (PIN_ALCHEMY_MODE_SWITCH).
   */
  void setModeSwitchPin(uint8_t modeSwitchPin) { modeSwitchPin_ = modeSwitchPin; }
  void begin(TwoWire &bankA, TwoWire *bankB, uint32_t nowMs);

  /**
   * Poll tiles and translate edges into UI actions.
   * @param sequencers Fixed voice-order view of the 4 voice sequencers.
   */
  void update(uint32_t nowMs, UIState &uiState,
              const SequencerView &sequencers);

  /** Read-only driver access, for boot scan reports and diagnostics pages. */
  [[nodiscard]] const AlchemyTiles &tiles() const { return panel_.tiles(); }

private:
  // One edge-tracker per physical button we watch: TileButton edge flags
  // stay asserted until the tile's next poll, and this bridge runs faster
  // than the polls, so actions fire on rising edges of the held level
  // instead. (Long-press needs are met with heldMilliseconds() + local
  // flags, which also survives the TileButton "spent" tap suppression.)
  struct ButtonEdges
  {
    /** Sample the level; true when it changed this pass. */
    bool take(const TileButton &b)
    {
      pressEdge = b.held() && !prevHeld_;
      releaseEdge = !b.held() && prevHeld_;
      prevHeld_ = b.held();
      return pressEdge || releaseEdge;
    }
    bool prevHeld_ = false;
    bool pressEdge = false;
    bool releaseEdge = false;
  };

  /**
   * One button's sampled state this pass. The transport and session handlers are
   * shared by both utility panels, so they take this instead of a parameter list
   * and a reference to the driver's button array.
   */
  struct ButtonState
  {
    bool pressEdge = false;
    bool releaseEdge = false;
    bool held = false;
    uint32_t heldMs = 0;
  };

  /**
   * Re-resolve which driver slot holds the slider tile and which holds the
   * button tile. Slot order is scan order, so a tile that did not answer at
   * begin() shifts every slot after it — asking the driver by TYPE_ID keeps
   * the mapping right when only one of the two tiles is plugged in.
   */
  void resolveSlots();

  /**
   * Edge state for a button on a resolved slot. A slot of -1 (its tile did
   * not answer) yields a never-pressed stand-in rather than indexing the
   * driver's array out of range.
   */
  const TileButton &buttonAt(int slot, uint8_t bit);

  void handleModeStrap(uint32_t nowMs, UIState &uiState);
  void onModeFlip(uint32_t nowMs, UIState &uiState);
  void handleVoiceButtons(uint32_t nowMs, UIState &uiState);
  void handleParamButtons(UIState &uiState);
  void handleUtilityButtons(uint32_t nowMs, UIState &uiState,
                            const SequencerView &sequencers);
  // Arpeggiator mode's two panels: patterns/Latch on the Param side, and the
  // utility set with the step-only slots replaced on the Utility side.
  void handleArpPatternButtons(UIState &uiState);
  void handleArpUtilityButtons(uint32_t nowMs, UIState &uiState);
  // Transport (Play/Stop, tap vs. settings hold) and Session (save, load on
  // hold) are mode-independent; both utility panels call these so the two modes
  // cannot drift on the two buttons that must never change meaning.
  void handleTransportButton(const ButtonState &button, UIState &uiState);
  void handleSessionButton(const ButtonState &button);
  void handleFaders(UIState &uiState, const SequencerView &sequencers);

  AlchemyPanel panel_;
  ControlSurface::ModeStabilizer mode_;
  ControlSurface::ShiftLatch latch_;
  ControlSurface::FaderMap faders_;

  // Slot/bit geometry of the 2-tile rig (see AlchemyUI ButtonMap.h). The
  // constants are the nominal layout of a fully-populated rig; the live slot
  // indices come from resolveSlots() so a rig missing one tile still works.
  static constexpr int kSliderSlotDefault = 0;
  static constexpr int kButtonTileSlotDefault = 1;
  static constexpr uint8_t kButtonBits = 8;

  // Edge trackers are indexed by role (0 = slider tile, 1 = button tile), not
  // by driver slot, so a slot change never scrambles the stored levels.
  enum Role : uint8_t { kSliderRole = 0, kButtonRole = 1, kRoleCount = 2 };

  int sliderSlot_ = kSliderSlotDefault;
  int buttonSlot_ = kButtonTileSlotDefault;

  ButtonEdges buttonEdges_[kRoleCount][kButtonBits]; // [role][bit]
  bool playSettingsOpenedThisPress_ = false;
  bool saveLoadLatch_ = false; // session button: hold consumed, release suppressed
  // Voice 4 + Shift: a tap opens the voice editor, a hold toggles Arpeggiator
  // mode, so the press defers its action to release/hold (like Play below).
  bool editorHoldArmed_ = false;
  bool editorHoldFired_ = false;
  // Randomize button Shift chord state: the press edge latched whether this
  // press is a clear chord (Shift held at press), and clearAllLatch_ consumes
  // the hold so the release cannot also clear a single voice.
  bool clearChordThisPress_ = false;
  bool clearAllLatch_ = false;
  // Shift edges re-arm tempo/feedback, mix/time and volume/macro.
  bool shiftWasHeld_ = false;
  uint8_t modeSwitchPin_ = 7; // GP7 default; setup1 sets PIN_ALCHEMY_MODE_SWITCH
  uint8_t lastVoiceIndex_ = 0;
  int lastStepForEdit_ = -1;
};

#endif // ALCHEMY_CONTROL_BRIDGE_H
