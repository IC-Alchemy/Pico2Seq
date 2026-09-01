// ButtonMap.h — the logical buttons of the Pico2Seq Alchemy control surface
// ---------------------------------------------------------------------------
// The physical rig is two tiles on one bank (see docs/superpowers/specs/
// 2026-09-01-alchemy-tile-control-surface-design.md):
//
//   slot 0  SliderModule (TYPE 0x01, 0x08-0x0A)  — 4 faders + 4 buttons,
//           whose buttons are the direct Voice 1..4 selects.
//   slot 1  ButtonModule8 (TYPE 0x02, 0x0B-0x0D) — 8 buttons carrying the
//           seven parameter buttons plus Shift (Param mode) or the six
//           utility functions plus Shift (Utility mode). Which function set
//           is live is a bridge decision (GP7 mode switch), not a property
//           of this table: both mode variants of a physical button map to
//           the same (slot, bit), so TileButton edge state is shared.
//
// This header is the single place that decides which physical button is
// which logical control, so a rewired panel is a table edit, not a logic
// hunt. Slot order is scan order (see AlchemyTiles): the slider tile always
// claims slot 0 when present, button tiles fill the following slots.

#ifndef ALCHEMY_UI_BUTTON_MAP_H
#define ALCHEMY_UI_BUTTON_MAP_H

#include <cstdint>

#include "AlchemyTiles.h"

enum class Btn : std::uint8_t {
  // ButtonModule8, Param mode (bits 0-7 in PCB order)
  Note = 0, Velocity, Filter, Attack, Decay, Octave, Slide,
  // Shift is bit 7 in both modes — one physical button, one enum value.
  Shift,
  // SliderModule buttons (bits 0-3), direct voice select in BOTH modes.
  Voice1, Voice2, Voice3, Voice4,
  // ButtonModule8, Utility mode (bits 0-6; same physical buttons as above)
  Play, DelayToggle, ScaleCycle, SwingCycle, ThemeCycle, EncoderCycle,
  Randomize,
  Count
};

inline constexpr std::uint8_t kBtnCount = static_cast<std::uint8_t>(Btn::Count);

/** Where each logical button lives: tile slot index + bit within the tile. */
struct ButtonMapping {
  std::uint8_t slot;
  std::uint8_t bit;
};

inline constexpr ButtonMapping kButtonMap[kBtnCount] = {
    // ButtonModule8 (slot 1): parameter set and utility set share bits 0-6;
    // bit 7 is Shift for both sets and appears once in the enum.
    {1, 0}, {1, 1}, {1, 2}, {1, 3},            // Note Velocity Filter Attack
    {1, 4}, {1, 5}, {1, 6},                    // Decay Octave Slide
    {1, 7},                                    // Shift
    // SliderModule (slot 0): Voice 1..4 direct selects.
    {0, 0}, {0, 1}, {0, 2}, {0, 3},            // Voice1..Voice4
    // ButtonModule8 (slot 1) utility set, bits 0-6.
    {1, 0}, {1, 1}, {1, 2}, {1, 3},            // Play DelayToggle Scale Swing
    {1, 4}, {1, 5}, {1, 6},                    // Theme EncoderCycle Randomize
};

/** Short display name (OLED-width) for each logical button. */
inline const char* btnName(Btn button) {
  switch (button) {
    case Btn::Note: return "NOTE";
    case Btn::Velocity: return "VEL";
    case Btn::Filter: return "FILT";
    case Btn::Attack: return "ATT";
    case Btn::Decay: return "DEC";
    case Btn::Octave: return "OCT";
    case Btn::Slide: return "SLIDE";
    case Btn::Shift: return "SHFT";
    case Btn::Voice1: return "V1";
    case Btn::Voice2: return "V2";
    case Btn::Voice3: return "V3";
    case Btn::Voice4: return "V4";
    case Btn::Play: return "PLAY";
    case Btn::DelayToggle: return "DLY";
    case Btn::ScaleCycle: return "SCALE";
    case Btn::SwingCycle: return "SWNG";
    case Btn::ThemeCycle: return "THME";
    case Btn::EncoderCycle: return "ENC";
    case Btn::Randomize: return "RAND";
    default: return "??";
  }
}

#endif  // ALCHEMY_UI_BUTTON_MAP_H
