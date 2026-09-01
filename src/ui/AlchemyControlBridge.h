#ifndef ALCHEMY_CONTROL_BRIDGE_H
#define ALCHEMY_CONTROL_BRIDGE_H

#include <Arduino.h>
#include <Wire.h>

#include "../AlchemyUI/src/AlchemyPanel.h"
#include "ControlSurfaceLogic.h"
#include "UIState.h"

class Sequencer;
class MidiNoteManager;

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
 * One update() pass never blocks longer than one tile transaction (~470 us
 * at 400 kHz) because AlchemyTiles paces tiles round-robin.
 *
 * Semantics implemented here (see docs/superpowers/specs/
 * 2026-09-01-alchemy-tile-control-surface-design.md):
 *   - GP7 mode strap, software-debounced, drives the Param/Utility tile
 *     function sets; a flip clears holds/latches, flashes a control LED and
 *     raises the OLED banner flag.
 *   - SliderModule buttons: Voice1..4 direct select in both modes; with
 *     Shift held they become transport chords (Play/Stop, Randomize,
 *     Scale, Delay toggle).
 *   - ButtonModule8: parameter set (Note..Slide) or utility set (Play,
 *     Delay, Scale, Swing, Theme, Encoder, Randomize) per mode; Shift is
 *     bit 7 in both.
 *   - Faders: step-parameter recording in Param mode (same recording path
 *     as the lidar), tempo/swing/delay-mix/gate-length in Utility mode.
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
   * @param sequencers Array of the 4 voice sequencers (voice index order).
   */
  void update(uint32_t nowMs, UIState &uiState,
              Sequencer *const *sequencers, size_t sequencerCount,
              MidiNoteManager &midiNoteManager);

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

  void handleModeStrap(uint32_t nowMs, UIState &uiState);
  void onModeFlip(uint32_t nowMs, UIState &uiState);
  void handleVoiceButtons(UIState &uiState, MidiNoteManager &midiNoteManager,
                          Sequencer *const *sequencers, size_t sequencerCount);
  void handleParamButtons(UIState &uiState);
  void handleUtilityButtons(uint32_t nowMs, UIState &uiState);
  void handleFaders(UIState &uiState, Sequencer *const *sequencers,
                    size_t sequencerCount);

  AlchemyPanel panel_;
  ControlSurface::ModeStabilizer mode_;
  ControlSurface::ShiftLatch latch_;
  ControlSurface::FaderMap faders_;

  // Slot/bit geometry of the 2-tile rig (see AlchemyUI ButtonMap.h).
  static constexpr uint8_t kSliderSlot = 0;
  static constexpr uint8_t kButtonTileSlot = 1;
  static constexpr uint8_t kButtonBits = 8;

  ButtonEdges buttonEdges_[2][kButtonBits]; // [slot][bit]
  bool playSettingsOpenedThisPress_ = false;
  uint8_t modeSwitchPin_ = 7; // GP7 default; setup1 sets PIN_ALCHEMY_MODE_SWITCH
};

#endif // ALCHEMY_CONTROL_BRIDGE_H
