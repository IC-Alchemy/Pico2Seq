#ifndef LEDMATRIX_FEEDBACK_H
#define LEDMATRIX_FEEDBACK_H

#include "ledMatrix.h"
#include "LEDConstants.h"
#include "../voice/VoiceManager.h"

// Forward declarations to break circular dependencies
class Sequencer;
class SequencerView;
struct UIState;

// LEDMatrixFeedback.h — what the 8x4 WS2812B stage mirror shows (Core 0).
// Player view: sounding step blooms white, gated steps glow in voice hue, rests
// stay dark; settings/param pages reuse the same pads. All fades are
// wall-clock (frameBlend), never frame-counted, so tempo and loop load can't
// change the look.

// Stage palettes. The player picks by feel; keep every theme's four voice hues
// distinguishable (hue + lightness, never red-vs-green alone).
enum class LEDTheme
{
  DEFAULT = 0, // Standard blue/green theme
  OCEANIC,     // Blue/cyan ocean theme
  VOLCANIC,    // Red/orange fire theme
  FOREST,      // Green/brown nature theme
  NEON,        // Bright cyan/magenta theme
  MODERN,      // New modern muted theme with high legibility
  DARK_NOCTIS, // Dark theme with cool blue accents
  DARK_EMBER,  // Dark theme with warm amber accents
  BLUE,        // High-contrast blue theme
  GREEN,       // High-contrast green theme
  COUNT        // last: theme count (saved settings index by this order)
};

static constexpr uint8_t LED_THEME_VOICE_COUNT = 4;

// One palette: voice hues, playhead accent, edit/settings colors.
struct LEDThemeColors
{
  // Which LEDTheme this entry defines. The theme cycler and the saved settings
  // address palettes by index, so this field exists to be checked against the
  // entry's position in ALL_THEMES at compile time (see the static_assert below
  // the table): a table in a different order would show one theme's colors
  // under another theme's name.
  LEDTheme theme;

  // Voice gate colors: gateOn is each voice's identity hue — the only gate
  // color a theme stores. Gate state is applied as brightness of that hue
  // (the GATE_ON_* / GATE_OFF_DIVISOR rule in LEDMatrixFeedback.cpp), so an
  // off step still reads as its voice, just clearly "off". Gate state is
  // therefore brightness, voice identity is hue, and the two never fight.
  CRGB gateOn[LED_THEME_VOICE_COUNT];

  // Playhead and accent colors
  CRGB playheadAccent;    // Current step playhead highlight
  CRGB idleBreathingBlue; // Breathing animation base color

  // Edit mode colors
  CRGB editModeDimBlueV1; // Voice 1 parameter edit mode
  CRGB editModeDimBlueV2; // Voice 2 parameter edit mode

  // Parameter button colors (active/inactive pairs)
  CRGB modNoteActive;       // Note parameter active
  CRGB modNoteInactive;     // Note parameter inactive
  CRGB modVelocityActive;   // Velocity parameter active
  CRGB modVelocityInactive; // Velocity parameter inactive
  CRGB modFilterActive;     // Filter parameter active
  CRGB modFilterInactive;   // Filter parameter inactive
  CRGB modDecayActive;      // Decay parameter active
  CRGB modDecayInactive;    // Decay parameter inactive
  CRGB modAttackActive;     // Attack parameter active
  CRGB modAttackInactive;   // Attack parameter inactive
  CRGB modOctaveActive;     // Octave parameter active
  CRGB modOctaveInactive;   // Octave parameter inactive
  CRGB modSlideActive;      // Slide parameter active
  CRGB modSlideInactive;    // Slide parameter inactive

  // Default and mode colors
  CRGB defaultActive;        // Default active state
  CRGB defaultInactive;      // Default inactive state
  CRGB modParamModeActive;   // Parameter mode active
  CRGB modParamModeInactive; // Parameter mode inactive
  CRGB modGateModeActive;    // Gate mode active
  CRGB modGateModeInactive;  // Gate mode inactive

  // Special effect colors
  CRGB randomizeFlash; // Randomize button flash
  CRGB randomizeIdle;  // Randomize button idle
};

// Call once at boot: blacks the smooth buffers and builds the gamma table.
void setupLEDMatrixFeedback();

// Per-frame render into ledMatrix. Call from the Core 0 loop only; mm is
// legacy (unused) — the hand readout lives on the OLED now.
void updateStepLEDs(
    LEDMatrix &ledMatrix,
    const SequencerView &sequencers,
    const UIState &uiState,
    int mm);

// Preset pick / settings-pad pages: pad N mirrors preset/pad N.
void updateSettingsModeLEDs(LEDMatrix &ledMatrix, const UIState &uiState);

// Settings-pad values as brightness; called from the settings page above.
void updateVoiceParameterLEDs(LEDMatrix &ledMatrix, const UIState &uiState);

// Legacy no-op (needs a matrix ref); kept so old call sites still compile.
void setStepLedColor(uint8_t step, uint8_t r, uint8_t g, uint8_t b);

// Switch palette live; takes effect on the next frame.
void setLEDTheme(LEDTheme theme);

// Active palette for renderers that need raw colors.
const LEDThemeColors *getActiveThemeColors();

#endif // LEDMATRIX_FEEDBACK_H
