// ButtonMap.h — the 20 logical buttons of the Pico2Seq control surface
// ---------------------------------------------------------------------------
// The physical rig is up to five tiles (one Slider 4x + Button 4x, four
// Button 4x), each contributing four buttons. This header is the single
// place that decides which physical button is which logical control, so a
// rewired panel is a table edit, not a logic hunt.
//
// Default layout, chosen so a single-bank rig loses only step pads:
//   slot 1..3 are the three Button tiles on bank A (0x0B..0x0D) and carry
//   every core function — parameter record buttons, transport, voice, scale,
//   shuffle, random, page. Slot 4 is the fourth Button tile, which needs
//   bank B (the registry gives each type three offsets per bank) and carries
//   STEP1-4; the slider tile's own four buttons carry STEP5-8, so losing
//   bank B still leaves four pads plus PAGE paging over the rest.
//
// Slot order is scan order (see AlchemyTiles): 0 = slider tile, 1..4 =
// button tiles by bus then address.

#ifndef ALCHEMY_UI_BUTTON_MAP_H
#define ALCHEMY_UI_BUTTON_MAP_H

#include <cstdint>

#include "AlchemyTiles.h"

enum class Btn : std::uint8_t {
  Step1 = 0, Step2, Step3, Step4, Step5, Step6, Step7, Step8,
  Note, Velocity, Filter, Attack, Decay, Octave,
  Play, Voice, Scale, Shuffle, Random, Page,
  Count
};

inline constexpr std::uint8_t kBtnCount = static_cast<std::uint8_t>(Btn::Count);

/** First step-pad button; the eight pads are contiguous from here. */
inline constexpr std::uint8_t kFirstStepButton = static_cast<std::uint8_t>(Btn::Step1);
inline constexpr std::uint8_t kStepButtonCount = 8;

/** Where each logical button lives: tile slot index + bit within the tile. */
struct ButtonMapping {
  std::uint8_t slot;
  std::uint8_t bit;
};

inline constexpr ButtonMapping kButtonMap[kBtnCount] = {
    // steps: tile on bank B, then the slider tile's own buttons
    {4, 0}, {4, 1}, {4, 2}, {4, 3},        // Step1..Step4
    {0, 0}, {0, 1}, {0, 2}, {0, 3},        // Step5..Step8 (slider tile)
    // parameters + transport + globals: the three bank-A button tiles
    {1, 0}, {1, 1}, {1, 2}, {1, 3},        // Note Velocity Filter Attack
    {2, 0}, {2, 1}, {2, 2}, {2, 3},        // Decay Octave Play Voice
    {3, 0}, {3, 1}, {3, 2}, {3, 3},        // Scale Shuffle Random Page
};

/** Short display name (OLED-width) for each logical button. */
inline const char* btnName(Btn button) {
  switch (button) {
    case Btn::Step1: return "ST1";
    case Btn::Step2: return "ST2";
    case Btn::Step3: return "ST3";
    case Btn::Step4: return "ST4";
    case Btn::Step5: return "ST5";
    case Btn::Step6: return "ST6";
    case Btn::Step7: return "ST7";
    case Btn::Step8: return "ST8";
    case Btn::Note: return "NOTE";
    case Btn::Velocity: return "VEL";
    case Btn::Filter: return "FILT";
    case Btn::Attack: return "ATT";
    case Btn::Decay: return "DEC";
    case Btn::Octave: return "OCT";
    case Btn::Play: return "PLAY";
    case Btn::Voice: return "VOICE";
    case Btn::Scale: return "SCALE";
    case Btn::Shuffle: return "SHUF";
    case Btn::Random: return "RAND";
    case Btn::Page: return "PAGE";
    default: return "??";
  }
}

#endif  // ALCHEMY_UI_BUTTON_MAP_H
