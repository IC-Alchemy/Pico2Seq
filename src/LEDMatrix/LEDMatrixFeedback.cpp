#include "LEDMatrixFeedback.h"
#include "../pico2seq-core/arpeggiator/Arpeggiator.h"
#include "../pico2seq-core/scales/scales.h"
#include <algorithm>
#include <Arduino.h>
#include <FastLED.h>
#include <cmath>

#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../app/SequencerView.h"
#include "../ui/ButtonManager.h"
#include "../ui/ControlSurfaceLogic.h"
#include "../ui/SettingsPads.h"
#include "../app/AppState.h"
#include "../voice/VoiceSystem.h"
#include "../voice/VoiceManager.h"
#include "../ui/UIEventHandler.h"
#include "../utils/Debug.h"
#include "../voice/VoicePresets.h"
#include "LEDConstants.h"
#include "ledMatrix.h"

/**
 * @brief LED Matrix Feedback Implementation
 *
 * Provides comprehensive visual feedback for sequencer operations including
 * step gate visualization, parameter editing, settings navigation, and
 * animated effects. Uses smoothed color blending for professional appearance.
 */

// LED matrix layout constants
static constexpr uint8_t SEQ_STEPS = 16;

// The host-tested layout helper must agree with the hardware constants.
static_assert(LEDConstants::MATRIX_WIDTH == ControlSurface::LedLayout::kWidth);
static_assert(LEDConstants::MATRIX_HEIGHT ==
              ControlSurface::LedLayout::kBandCount *
                  ControlSurface::LedLayout::kRowsPerBand);
static_assert(LEDConstants::MATRIX_TOTAL_LEDS ==
              ControlSurface::LedLayout::kLedCount);
static_assert(LEDConstants::BOTTOM_HALF_OFFSET ==
              ControlSurface::LedLayout::kStepsPerBand);
// Smoothed color buffer for smooth LED transitions
CRGB smoothedTargetColorBuffer[LEDConstants::MATRIX_TOTAL_LEDS];

// Color blending constants from LEDConstants
static constexpr uint8_t TARGET_SMOOTHING_BLEND_AMOUNT =
    LEDConstants::TARGET_SMOOTHING_BLEND_AMOUNT;

// Define common colors as constants for readability and maintainability
// These will be populated from the activeThemeColors pointer
CRGB current_COLOR_GATE_ON_V1;
CRGB current_COLOR_GATE_OFF_V1;
CRGB current_COLOR_PLAYHEAD_ACCENT;
CRGB current_COLOR_GATE_ON_V2;
CRGB current_COLOR_GATE_OFF_V2;
CRGB current_COLOR_IDLE_BREATHING_BLUE;
CRGB current_COLOR_EDIT_MODE_DIM_BLUE_V1;
CRGB current_COLOR_EDIT_MODE_DIM_BLUE_V2;
CRGB current_COLOR_MOD_NOTE_ACTIVE;
CRGB current_COLOR_MOD_NOTE_INACTIVE;
CRGB current_COLOR_MOD_VELOCITY_ACTIVE;
CRGB current_COLOR_MOD_VELOCITY_INACTIVE;
CRGB current_COLOR_MOD_FILTER_ACTIVE;
CRGB current_COLOR_MOD_FILTER_INACTIVE;
CRGB current_COLOR_MOD_DECAY_ACTIVE;
CRGB current_COLOR_MOD_DECAY_INACTIVE;
CRGB current_COLOR_MOD_OCTAVE_ACTIVE;
CRGB current_COLOR_MOD_OCTAVE_INACTIVE;
CRGB current_COLOR_DEFAULT_ACTIVE;
CRGB current_COLOR_DEFAULT_INACTIVE;
CRGB current_COLOR_MOD_PARAM_MODE_ACTIVE;
CRGB current_COLOR_MOD_PARAM_MODE_INACTIVE;
CRGB current_COLOR_MOD_GATE_MODE_ACTIVE;
CRGB current_COLOR_MOD_GATE_MODE_INACTIVE;
CRGB current_COLOR_RANDOMIZE_FLASH;
CRGB current_COLOR_RANDOMIZE_IDLE;

// Voice-gate palette design principles (applied to every theme below):
// - Voices are CATEGORICAL data (4 groups): each voice gets a distinct hue,
//   within-pair voices (V1/V2, V3/V4 — the only ones ever shown together on
//   the two matrix bands) sit ~40+ degrees apart in hue, following the
//   categorical-palette guideline of maximum hue distance at equal lightness.
// - Hues are anchored in the empirically colorblind-safe Okabe-Ito / Paul Tol
//   categorical sets (blue vs orange, green vs purple/magenta), graded toward
//   each theme's character. No voice pair relies on red-vs-green alone.
// - Gate state is SEQUENTIAL data: a theme stores only each voice's hue, and
//   gate on/off is that hue at two brightness levels (the GATE_ON_* /
//   GATE_OFF_DIVISOR rule below), so state reads as brightness and identity
//   reads as hue — a redundant encoding that survives grayscale and all
//   common color-vision deficiencies.
// - Monochromatic themes (BLUE/GREEN) and warm-family themes (VOLCANIC/EMBER)
//   instead use a monotonic lightness ramp as the redundant channel, per the
//   sequential-palette rule (must order correctly in grayscale).
// - On-states are lightness-balanced so no voice dominates, and the gate-on
//   gain (below) lifts them for legibility on the near-black LED background
//   (dark-mode practice); reds are boosted slightly to compensate protan
//   red-darkening.
constexpr LEDThemeColors ALL_THEMES[] = {
    {LEDTheme::DEFAULT,
     // DEFAULT - Okabe-Ito categorical quartet: sky / orange / bluish-green /
     // reddish-purple. The reference-standard colorblind-safe voice set.
     {CRGB(60, 170, 235), CRGB(235, 160, 20), CRGB(0, 190, 140),
      CRGB(215, 130, 175)},
     CRGB(0, 44, 54),
     CRGB(0, 0, 94),
     CRGB(0, 0, 12),
     CRGB(0, 0, 12),
     CRGB(128, 94, 0),
     CRGB(32, 24, 0),
     CRGB(94, 0, 94),
     CRGB(12, 0, 12),
     CRGB(0, 94, 188),
     CRGB(0, 24, 48),
     CRGB(188, 64, 0),
     CRGB(48, 16, 0),
     CRGB(128, 0, 0),
     CRGB(32, 0, 0),
     CRGB(0, 128, 64),
     CRGB(0, 32, 16),
     CRGB(188, 0, 188),
     CRGB(48, 0, 48),
     CRGB(64, 64, 128),
     CRGB(16, 16, 32),
     CRGB(128, 64, 0),
     CRGB(32, 16, 0),
     CRGB(94, 0, 64),
     CRGB(24, 0, 16),
     CRGB(64, 94, 94),
     CRGB(16, 24, 24)},
    {LEDTheme::OCEANIC,
     // OCEANIC - deep-sea blue / sunlit sand / seafoam / pale ice. Warm sand
     // accents give V1/V2 a CVD-safe cool-vs-warm split; V3/V4 separate by
     // lightness (seafoam vs near-white ice) as redundant encoding.
     {CRGB(30, 120, 235), CRGB(235, 170, 60), CRGB(20, 200, 150),
      CRGB(150, 230, 240)},
     CRGB(0, 38, 48),
     CRGB(0, 48, 144),
     CRGB(0, 5, 17),
     CRGB(0, 12, 17),
     CRGB(0, 144, 188),
     CRGB(0, 15, 22),
     CRGB(64, 144, 188),
     CRGB(13, 29, 38),
     CRGB(94, 0, 188),
     CRGB(11, 0, 24),
     CRGB(188, 144, 0),
     CRGB(38, 29, 0),
     CRGB(144, 188, 94),
     CRGB(29, 38, 19),
     CRGB(188, 0, 94),
     CRGB(17, 0, 11),
     CRGB(144, 0, 188),
     CRGB(29, 0, 38),
     CRGB(48, 144, 144),
     CRGB(10, 29, 29),
     CRGB(0, 166, 188),
     CRGB(0, 33, 38),
     CRGB(144, 0, 188),
     CRGB(15, 0, 22),
     CRGB(0, 188, 166),
     CRGB(0, 22, 15)},
    {LEDTheme::VOLCANIC,
        // VOLCANIC - crimson (protan-boosted) / gold / tangerine / magma-pink.
        // Warm-family ramp with monotonic lightness as redundant channel plus
        // a pink outlier anchor; reds lifted to offset protan red-darkening.
        {CRGB(240, 70, 60), CRGB(250, 175, 45), CRGB(255, 150, 40),
         CRGB(255, 80, 160)},
        CRGB(62, 22, 4),     // playheadAccent - dark lava accent
        CRGB(50, 20, 8),     // idleBreathingBlue - warm ember glow
        CRGB(12, 6, 4),      // editModeDimBlueV1 - very dark warm slate
        CRGB(14, 8, 5),      // editModeDimBlueV2
        CRGB(230, 150, 90),  // modNoteActive - warm beige-orange
        CRGB(30, 18, 12),    // modNoteInactive
        CRGB(240, 180, 120), // modVelocityActive - pale amber
        CRGB(32, 22, 16),    // modVelocityInactive
        CRGB(200, 90, 60),   // modFilterActive - muted terracotta red
        CRGB(28, 12, 8),     // modFilterInactive
        CRGB(255, 200, 120), // modDecayActive - bright amber
        CRGB(32, 24, 14),    // modDecayInactive
        CRGB(190, 120, 70),  // modAttackActive - muted copper
        CRGB(26, 16, 10),    // modAttackInactive
        CRGB(255, 120, 60),  // modOctaveActive - hot orange accent
        CRGB(30, 12, 6),     // modOctaveInactive
        CRGB(255, 160, 90),  // modSlideActive - warm slide accent
        CRGB(30, 18, 12),    // modSlideInactive
        CRGB(240, 220, 200), // defaultActive - warm light gray
        CRGB(16, 10, 8),     // defaultInactive - near-black
        CRGB(255, 170, 100), // modParamModeActive - warm pale
        CRGB(28, 18, 12),    // modParamModeInactive
        CRGB(255, 140, 60),  // modGateModeActive - bright ember highlight
        CRGB(26, 14, 8),     // modGateModeInactive
        CRGB(255, 220, 150), // randomizeFlash - bright warm flash
        CRGB(24, 14, 10)     // randomizeIdle - dark subtle tone
    },
    {LEDTheme::FOREST,
        // FOREST - leaf / bark-amber / glacial-lake blue / dry-grass gold.
        // Okabe-style green-vs-orange and blue-vs-yellow splits; no
        // green-vs-green pair is ever shown together.
        {CRGB(60, 195, 80), CRGB(225, 150, 55), CRGB(50, 160, 210),
         CRGB(200, 185, 70)},
        CRGB(12, 55, 20),    // playheadAccent - deep forest accent
        CRGB(16, 36, 18),    // idleBreathingBlue - deep moss breathing
        CRGB(6, 12, 7),      // editModeDimBlueV1 - dark green slate
        CRGB(8, 14, 9),      // editModeDimBlueV2
        CRGB(160, 220, 140), // modNoteActive - pale green
        CRGB(20, 30, 18),    // modNoteInactive
        CRGB(190, 230, 160), // modVelocityActive - soft mint
        CRGB(24, 32, 22),    // modVelocityInactive
        CRGB(110, 180, 120), // modFilterActive - muted green-teal
        CRGB(14, 24, 16),    // modFilterInactive
        CRGB(210, 190, 120), // modDecayActive - dry-grass warm contrast
        CRGB(28, 26, 16),    // modDecayInactive
        CRGB(140, 190, 110), // modAttackActive - sage
        CRGB(18, 26, 14),    // modAttackInactive
        CRGB(200, 150, 90),  // modOctaveActive - warm bark accent
        CRGB(28, 20, 12),    // modOctaveInactive
        CRGB(150, 220, 170), // modSlideActive - minty slide accent
        CRGB(18, 28, 22),    // modSlideInactive
        CRGB(220, 240, 210), // defaultActive - off-white green tint
        CRGB(10, 14, 10),    // defaultInactive - near-black
        CRGB(170, 230, 150), // modParamModeActive - bright leaf
        CRGB(20, 30, 20),    // modParamModeInactive
        CRGB(190, 210, 120), // modGateModeActive - lichen highlight
        CRGB(24, 28, 14),    // modGateModeInactive
        CRGB(230, 250, 180), // randomizeFlash - pale flash
        CRGB(14, 20, 12)     // randomizeIdle - dark subtle tone
    },
    {LEDTheme::NEON,
        // NEON - Tol-bright-style primaries: cyan / magenta / lime / violet.
        // All pairs 90+ degrees apart; lime moderated so it doesn't dominate.
        {CRGB(0, 225, 255), CRGB(255, 60, 220), CRGB(170, 235, 45),
         CRGB(165, 130, 255)},
        CRGB(0, 55, 65),     // playheadAccent - deep cyan accent
        CRGB(0, 30, 60),     // idleBreathingBlue - neon blue breathing
        CRGB(0, 10, 16),     // editModeDimBlueV1 - dark cyan slate
        CRGB(10, 0, 12),     // editModeDimBlueV2
        CRGB(120, 255, 255), // modNoteActive - pale cyan
        CRGB(16, 30, 30),    // modNoteInactive
        CRGB(180, 255, 255), // modVelocityActive - ice cyan
        CRGB(20, 32, 32),    // modVelocityInactive
        CRGB(90, 120, 255),  // modFilterActive - electric indigo
        CRGB(12, 16, 34),    // modFilterInactive
        CRGB(255, 220, 60),  // modDecayActive - neon yellow contrast
        CRGB(32, 28, 8),     // modDecayInactive
        CRGB(140, 255, 120), // modAttackActive - neon green
        CRGB(18, 32, 16),    // modAttackInactive
        CRGB(255, 60, 255),  // modOctaveActive - magenta accent
        CRGB(32, 8, 32),     // modOctaveInactive
        CRGB(0, 255, 200),   // modSlideActive - spring neon slide
        CRGB(10, 30, 24),    // modSlideInactive
        CRGB(230, 230, 255), // defaultActive - pale violet-white
        CRGB(12, 12, 18),    // defaultInactive - near-black
        CRGB(80, 255, 180),  // modParamModeActive - neon mint
        CRGB(10, 30, 22),    // modParamModeInactive
        CRGB(255, 120, 220), // modGateModeActive - pink neon highlight
        CRGB(30, 12, 26),    // modGateModeInactive
        CRGB(255, 255, 255), // randomizeFlash - white flash
        CRGB(14, 14, 20)     // randomizeIdle - dark subtle tone
    },
    {LEDTheme::MODERN,
        // MODERN - dusty blue / clay / sage / rosewood. Muted chroma for the
        // refined look, but pairs sit ~140+ degrees apart so muting never
        // costs distinguishability; lightness equalized across voices.
        {CRGB(110, 170, 215), CRGB(215, 150, 110), CRGB(95, 180, 125),
         CRGB(225, 125, 180)},
        CRGB(20, 55, 54),    // playheadAccent - muted teal accent
        CRGB(60, 84, 110),   // idleBreathingBlue - slate blue for breathing
        CRGB(12, 16, 20),    // editModeDimBlueV1 - dim slate
        CRGB(18, 22, 26),    // editModeDimBlueV2 - slightly lighter slate
        CRGB(200, 180, 160), // modNoteActive - soft warm note color
        CRGB(70, 60, 56),    // modNoteInactive - desaturated
        CRGB(180, 200, 220), // modVelocityActive - pale cyan
        CRGB(64, 72, 80),    // modVelocityInactive
        CRGB(140, 120, 160), // modFilterActive - muted mauve
        CRGB(48, 36, 48),    // modFilterInactive
        CRGB(220, 200, 140), // modDecayActive - soft amber
        CRGB(64, 54, 36),    // modDecayInactive
        CRGB(140, 160, 120), // modAttackActive - sage
        CRGB(48, 56, 40),    // modAttackInactive
        CRGB(220, 140, 180), // modOctaveActive - soft pink accent
        CRGB(56, 28, 36),    // modOctaveInactive
        CRGB(160, 200, 200), // modSlideActive - muted cyan-tint slide accent
        CRGB(48, 64, 64),    // modSlideInactive
        CRGB(200, 200, 200), // defaultActive - light gray for active defaults
        CRGB(36, 36, 40),    // defaultInactive - near-black for inactive
        CRGB(180, 220, 200), // modParamModeActive - pale green
        CRGB(40, 48, 44),    // modParamModeInactive
        CRGB(240, 200, 160), // modGateModeActive - warm highlight
        CRGB(56, 48, 40),    // modGateModeInactive
        CRGB(255, 210, 170), // randomizeFlash - bright warm flash
        CRGB(40, 44, 46)     // randomizeIdle - subtle gray idle tone
    },
    // DARK_NOCTIS - midnight blue / lantern amber / deep violet / moonlight.
    // One warm accent (the lantern) gives V1/V2 a CVD-safe split; V3/V4 pair
    // violet against bright moon-silver, distinct in hue AND lightness.
    {LEDTheme::DARK_NOCTIS,
        {CRGB(50, 120, 210), CRGB(220, 150, 50), CRGB(150, 110, 225),
         CRGB(190, 215, 230)},
        CRGB(18, 52, 85),    // playheadAccent - deep navy accent
        CRGB(18, 30, 50),    // idleBreathingBlue - muted navy
        CRGB(8, 10, 14),     // editModeDimBlueV1 - very dark slate
        CRGB(10, 14, 18),    // editModeDimBlueV2
        CRGB(100, 140, 160), // modNoteActive - cool desaturated teal
        CRGB(24, 28, 30),    // modNoteInactive
        CRGB(140, 160, 180), // modVelocityActive - pale steel blue
        CRGB(30, 34, 36),    // modVelocityInactive
        CRGB(120, 100, 140), // modFilterActive - muted indigo
        CRGB(24, 18, 24),    // modFilterInactive
        CRGB(160, 120, 90),  // modDecayActive - muted warm contrast
        CRGB(28, 26, 22),    // modDecayInactive
        CRGB(120, 150, 110), // modAttackActive - subdued sage
        CRGB(22, 26, 20),    // modAttackInactive
        CRGB(180, 110, 160), // modOctaveActive - muted magenta accent
        CRGB(20, 12, 16),    // modOctaveInactive
        CRGB(100, 160, 170), // modSlideActive - cool cyan slide
        CRGB(18, 26, 28),    // modSlideInactive
        CRGB(200, 200, 200), // defaultActive - light gray
        CRGB(14, 14, 16),    // defaultInactive - near black
        CRGB(120, 200, 170), // modParamModeActive - soft aqua-green
        CRGB(16, 18, 18),    // modParamModeInactive
        CRGB(160, 140, 110), // modGateModeActive - muted warm highlight
        CRGB(18, 16, 14),    // modGateModeInactive
        CRGB(220, 200, 180), // randomizeFlash - soft warm flash
        CRGB(12, 12, 14)     // randomizeIdle - dark subtle tone
    },
    {LEDTheme::DARK_EMBER,
        // DARK_EMBER - ember-red (protan-boosted) / gold / copper-rose /
        // pale flame. Monotonic lightness ramp carries identity for CVD
        // viewers; reds lifted to offset protan red-darkening.
        {CRGB(225, 65, 50), CRGB(250, 185, 70), CRGB(225, 110, 110),
         CRGB(255, 215, 150)},
        CRGB(66, 26, 8), // playheadAccent - warm ember accent (was navy copy-paste)
        CRGB(28, 22,
             20), // idleBreathingBlue - warm slate for breathing (amber-tinted)
        CRGB(10, 8, 8),      // editModeDimBlueV1 - very dark warm slate
        CRGB(12, 10, 10),    // editModeDimBlueV2
        CRGB(220, 160, 120), // modNoteActive - warm beige
        CRGB(28, 24, 20),    // modNoteInactive
        CRGB(200, 160, 140), // modVelocityActive - soft warm gray
        CRGB(30, 26, 24),    // modVelocityInactive
        CRGB(180, 120, 100), // modFilterActive - muted terracotta
        CRGB(24, 18, 16),    // modFilterInactive
        CRGB(255, 200, 150), // modDecayActive - bright amber
        CRGB(28, 20, 16),    // modDecayInactive
        CRGB(160, 180, 140), // modAttackActive - muted olive
        CRGB(22, 20, 18),    // modAttackInactive
        CRGB(220, 140, 160), // modOctaveActive - soft rose ember
        CRGB(20, 12, 12),    // modOctaveInactive
        CRGB(200, 160, 140), // modSlideActive - warm slide accent
        CRGB(18, 16, 14),    // modSlideInactive
        CRGB(230, 220, 200), // defaultActive - light warm gray
        CRGB(14, 12, 12),    // defaultInactive - near black
        CRGB(255, 200, 170), // modParamModeActive - warm pale
        CRGB(16, 14, 12),    // modParamModeInactive
        CRGB(255, 180, 90),  // modGateModeActive - bright ember highlight
        CRGB(18, 14, 12),    // modGateModeInactive
        CRGB(255, 210, 140), // randomizeFlash - bright warm flash
        CRGB(10, 8, 8)       // randomizeIdle - very dark idle tone
    },

    {LEDTheme::BLUE,
        // BLUE theme - monochrome ramp done right: monotonic lightness
        // (grayscale-correct ordering) with a deep-to-ice run plus an indigo
        // endpoint for hue assist. Lightness, not hue, carries identity here.
        {CRGB(25, 80, 210), CRGB(55, 160, 255), CRGB(95, 225, 255),
         CRGB(105, 95, 255)},
        CRGB(18, 60, 105),   // playheadAccent - strong blue accent
        CRGB(16, 36, 80),    // idleBreathingBlue - deep ocean blue
        CRGB(8, 10, 14),     // editModeDimBlueV1 - very dark slate
        CRGB(12, 16, 20),    // editModeDimBlueV2
        CRGB(140, 190, 220), // modNoteActive - pale blue
        CRGB(20, 24, 28),    // modNoteInactive
        CRGB(180, 210, 230), // modVelocityActive - light cyan
        CRGB(24, 28, 32),    // modVelocityInactive
        CRGB(120, 140, 200), // modFilterActive - muted indigo
        CRGB(20, 18, 24),    // modFilterInactive
        CRGB(200, 160, 120), // modDecayActive - warm contrast (subtle)
        CRGB(22, 20, 16),    // modDecayInactive
        CRGB(120, 180, 140), // modAttackActive - cool sage
        CRGB(18, 22, 16),    // modAttackInactive
        CRGB(220, 140, 200), // modOctaveActive - soft magenta accent
        CRGB(20, 12, 16),    // modOctaveInactive
        CRGB(160, 210, 230), // modSlideActive - cyan slide accent
        CRGB(18, 24, 26),    // modSlideInactive
        CRGB(220, 230, 240), // defaultActive - light gray-blue
        CRGB(14, 14, 18),    // defaultInactive - near black
        CRGB(120, 200, 240), // modParamModeActive - bright aqua
        CRGB(16, 18, 18),    // modParamModeInactive
        CRGB(160, 200, 240), // modGateModeActive - cool highlight
        CRGB(18, 16, 14),    // modGateModeInactive
        CRGB(255, 240, 220), // randomizeFlash - bright neutral flash
        CRGB(12, 12, 14)     // randomizeIdle - dark subtle tone
    },
    {LEDTheme::GREEN,
        // GREEN theme - monochrome ramp: deep / bright / mint / lime with
        // monotonic lightness (grayscale-correct). Same sequential-encoding
        // treatment as BLUE.
        {CRGB(25, 155, 70), CRGB(55, 215, 105), CRGB(115, 250, 175),
         CRGB(175, 240, 85)},
        CRGB(12, 68, 38),    // playheadAccent - strong forest accent
        CRGB(18, 44, 28),    // idleBreathingBlue - deep forest for breathing
        CRGB(8, 12, 10),     // editModeDimBlueV1 - very dark green slate
        CRGB(12, 16, 14),    // editModeDimBlueV2
        CRGB(200, 240, 200), // modNoteActive - pale green
        CRGB(22, 26, 22),    // modNoteInactive
        CRGB(180, 230, 200), // modVelocityActive - soft mint
        CRGB(24, 30, 26),    // modVelocityInactive
        CRGB(140, 180, 160), // modFilterActive - muted green-teal
        CRGB(20, 18, 20),    // modFilterInactive
        CRGB(200, 180, 140), // modDecayActive - subtle warm contrast
        CRGB(22, 20, 18),    // modDecayInactive
        CRGB(140, 200, 120), // modAttackActive - bright sage
        CRGB(18, 20, 16),    // modAttackInactive
        CRGB(220, 180, 200), // modOctaveActive - soft rose accent
        CRGB(20, 12, 12),    // modOctaveInactive
        CRGB(160, 220, 180), // modSlideActive - minty slide accent
        CRGB(18, 20, 18),    // modSlideInactive
        CRGB(240, 250, 240), // defaultActive - off-white for active defaults
        CRGB(12, 14, 12),    // defaultInactive - near black
        CRGB(160, 240, 200), // modParamModeActive - bright mint
        CRGB(16, 14, 14),    // modParamModeInactive
        CRGB(200, 220, 160), // modGateModeActive - soft highlight
        CRGB(16, 14, 12),    // modGateModeInactive
        CRGB(255, 250, 200), // randomizeFlash - warm flash
        CRGB(10, 12, 10)     // randomizeIdle - very dark idle tone
    }};

static_assert(sizeof(ALL_THEMES) / sizeof(ALL_THEMES[0]) ==
                  static_cast<int>(LEDTheme::COUNT),
              "Every LED theme needs one palette entry");

// The table must sit at its own enum indices: LEDTheme indices are what the
// theme cycler, the saved settings and the docs all use, so an entry in the
// wrong slot shows one theme's colors under another theme's name.
static constexpr bool themeTableMatchesEnumOrder() {
  for (int i = 0; i < static_cast<int>(LEDTheme::COUNT); ++i) {
    if (ALL_THEMES[i].theme != static_cast<LEDTheme>(i)) {
      return false;
    }
  }
  return true;
}

static_assert(themeTableMatchesEnumOrder(),
              "ALL_THEMES must list themes in LEDTheme order");

static const LEDThemeColors *activeThemeColors =
    &ALL_THEMES[static_cast<int>(LEDTheme::DEFAULT)];

// Gate-state rendering rule. A theme stores one hue per voice (gateOn in
// ALL_THEMES above); the two gate states are that hue at two brightnesses,
// always scaling all three channels by one factor so the hue — and with it the
// voice identity — survives exactly:
//  - on: lifted by GATE_ON_GAIN, or as far as the hue can go without a channel
//    clipping. A hue whose brightest channel already sits at full scale cannot
//    get brighter without losing saturation, so it stays where it is.
//  - off: the same hue at 1/GATE_OFF_DIVISOR, dark enough that the gate
//    pattern reads at a glance while an off step still shows its voice.
static constexpr uint8_t GATE_ON_GAIN_NUM = 6;   // 1.2x
static constexpr uint8_t GATE_ON_GAIN_DEN = 5;
static constexpr uint8_t GATE_OFF_DIVISOR = 16;  // 1/16 of the hue

// One channel of a gate-state scale. Rounds to the nearest step so the dim
// off-state levels keep the hue that truncation would distort.
static uint8_t scaleGateChannel(uint8_t channel, uint32_t numerator,
                                uint32_t denominator) {
  return static_cast<uint8_t>((static_cast<uint32_t>(channel) * numerator +
                               denominator / 2) /
                              denominator);
}

// Scales all three channels by numerator/denominator. Callers pass a numerator
// no larger than the hue's own peak, so no channel can clip.
static CRGB scaleGateHue(const CRGB &hue, uint32_t numerator,
                         uint32_t denominator) {
  return CRGB(scaleGateChannel(hue.r, numerator, denominator),
              scaleGateChannel(hue.g, numerator, denominator),
              scaleGateChannel(hue.b, numerator, denominator));
}

static CRGB getVoiceGateColor(const LEDThemeColors &themeColors,
                              uint8_t voiceIndex, bool gateActive) {
  const uint8_t clampedVoiceIndex =
      voiceIndex < LED_THEME_VOICE_COUNT ? voiceIndex : 0;
  const CRGB &hue = themeColors.gateOn[clampedVoiceIndex];
  if (!gateActive) {
    return scaleGateHue(hue, 1, GATE_OFF_DIVISOR);
  }

  const uint8_t peak =
      std::max<uint8_t>(hue.r, std::max<uint8_t>(hue.g, hue.b));
  if (peak == 0) {
    return hue;
  }
  const uint32_t liftedPeak =
      static_cast<uint32_t>(peak) * GATE_ON_GAIN_NUM / GATE_ON_GAIN_DEN;
  return scaleGateHue(hue, std::min<uint32_t>(liftedPeak, 255), peak);
}

void setLEDTheme(LEDTheme theme) {
  if (static_cast<int>(theme) < static_cast<int>(LEDTheme::COUNT)) {
    activeThemeColors = &ALL_THEMES[static_cast<int>(theme)];
  }
}

const LEDThemeColors *getActiveThemeColors() { return activeThemeColors; }

DEFINE_GRADIENT_PALETTE(parameterPalette){
    0,   0,   0,   255, // Blue
    85,  0,   255, 0,   // Green
    170, 255, 0,   0,   // Red
    255, 0,   0,   255  // Back to blue
};
CRGBPalette16 parameterColors = parameterPalette;

CRGB getParameterColor(ParamId param, uint8_t intensity) {
  uint8_t paletteIndex =
      map(static_cast<int>(param), 0, static_cast<int>(ParamId::Count), 0, 255);
  return ColorFromPalette(parameterColors, paletteIndex, intensity);
}

void addPolyrhythmicOverlay(
    LEDMatrix &ledMatrix, const Sequencer &sequencer, uint8_t band,
    uint8_t overlayIntensity = LEDConstants::POLYRHYTHM_INTENSITY) {
  // Only add overlay if sequencer is actively running
  if (!sequencer.isRunning()) {
    return;
  }

  // Parameter overlay configuration for polyrhythmic visualization
  struct PolyrhythmicParameterOverlay {
    ParamId parameterID;
    CRGB overlayColor;
  };

  const PolyrhythmicParameterOverlay
      overlayParameters[LEDConstants::POLYRHYTHM_PARAM_COUNT] = {
          {ParamId::Note, LEDColors::POLYRHYTHM_NOTE},
          {ParamId::Velocity, LEDColors::POLYRHYTHM_VELOCITY},
          {ParamId::Filter, LEDColors::POLYRHYTHM_FILTER}};

  // Apply overlay for each parameter type
  for (size_t paramIndex = 0; paramIndex < LEDConstants::POLYRHYTHM_PARAM_COUNT;
       ++paramIndex) {
    const ParamId currentParameter = overlayParameters[paramIndex].parameterID;
    const uint8_t currentParameterStep =
        sequencer.getCurrentStepForParameter(currentParameter);
    const uint8_t parameterStepCount =
        sequencer.getParameterStepCount(currentParameter);

    // Only apply overlay if parameter is within valid bounds
    if (currentParameterStep < LEDConstants::MAX_STEP_BUTTONS &&
        parameterStepCount > 1 &&
        parameterStepCount <= LEDConstants::MAX_STEP_BUTTONS) {

      // Calculate LED matrix position
      const int ledLinearIndex =
          ControlSurface::LedLayout::linearIndex(band, currentParameterStep);
      if (ledLinearIndex < 0) {
        continue;
      }
      CRGB currentLEDColor = ledMatrix.getLeds()[ledLinearIndex];

      // Blend overlay color with existing LED color
      currentLEDColor += overlayParameters[paramIndex].overlayColor;

      ledMatrix.setLED(ControlSurface::LedLayout::x(currentParameterStep),
                       ControlSurface::LedLayout::y(band, currentParameterStep),
                       currentLEDColor);
    }
  }
}

float ease(float x) { return x < 0.5 ? 2 * x * x : 1 - pow(-2 * x + 2, 2) / 2; }

float smoothBreathing(uint32_t timeMs) {
  // Calculate smooth breathing animation value using easing function
  const float normalizedTime =
      static_cast<float>(timeMs % LEDConstants::BREATHING_CYCLE_MS) /
      static_cast<float>(LEDConstants::BREATHING_CYCLE_MS);
  return ease(0.5f * (1.0f + sin(2.0f * PI * normalizedTime)));
}

void setStepLedColor(uint8_t stepIndex, uint8_t redValue, uint8_t greenValue,
                     uint8_t blueValue) {
  // Legacy function for setting individual step LED colors
  // Note: This function requires a LEDMatrix reference to work properly
  // Consider using the main LED update functions instead
}

void setupLEDMatrixFeedback() {
  // Initialize smoothed color buffer to black (off state)
  for (int ledIndex = 0; ledIndex < LEDConstants::MATRIX_TOTAL_LEDS;
       ++ledIndex) {
    smoothedTargetColorBuffer[ledIndex] = LEDColors::BLACK;
  }
}

/**
 * @brief Updates LED matrix to show settings mode interface
 *
 * Displays menu options and preset selections using step LEDs:
 * - Preset selection: lights each pad that holds a preset (pad N = preset N)
 *   and pulses the selected voice's current preset
 * - Voice parameter sub-mode: each pad displays its current parameter value
 * - Uses different colors to indicate current selection and available options
 */
void updateSettingsModeLEDs(LEDMatrix &ledMatrix, const UIState &uiState) {
  const LEDThemeColors *activeThemeColors = getActiveThemeColors();

  // Clear all LEDs first
  for (int i = 0; i < LEDMatrix::WIDTH * LEDMatrix::HEIGHT; ++i) {
    ledMatrix.getLeds()[i] = CRGB::Black;
  }

  if (uiState.isPresetSelection()) {
    // Preset selection mode - light every pad that holds a preset
    const uint8_t totalPresets = VoicePresets::getPresetCount();

    // Keep preset selection in the hue assigned to the configured voice.
    CRGB selectedColor =
        getVoiceGateColor(*activeThemeColors, uiState.selectedVoiceIndex, true);
    CRGB availableColor = getVoiceGateColor(*activeThemeColors,
                                            uiState.selectedVoiceIndex, false);

    const uint8_t voiceIndex = uiState.selectedVoiceIndex < UIState::MAX_VOICES
                                   ? uiState.selectedVoiceIndex
                                   : 0;
    const uint8_t currentPresetIndex = uiState.voicePresetIndices[voiceIndex];

    // Pad N holds preset N, so each LED mirrors its pad.
    for (uint8_t pad = 0; pad < VoicePresets::kPresetPadCount; pad++) {
      const int presetIndex =
          VoicePresets::presetIndexForPad(pad, totalPresets);
      if (presetIndex < 0) {
        continue;
      }

      CRGB color;

      // Highlight currently selected preset
      if (presetIndex == currentPresetIndex) {
        // Current preset - bright pulsing
        uint32_t time = millis();
        float pulse = 0.5f + 0.5f * sinf(time * 0.008f);
        color = selectedColor;
        color.nscale8(static_cast<uint8_t>(128 + 127 * pulse));
      } else {
        // Available preset - the voice's gate-off color, i.e. the same dim
        // steady level an off step shows in the step row.
        color = availableColor;
      }

      ledMatrix.setLED(pad % LEDMatrix::WIDTH, pad / LEDMatrix::WIDTH, color);
    }
  } else {
    updateVoiceParameterLEDs(ledMatrix, uiState);
  }
}

void updateVoiceParameterLEDs(LEDMatrix &ledMatrix, const UIState &uiState) {
  if (!uiState.hasVoiceParameterFeedback(millis()))
    return;
  const auto *theme = getActiveThemeColors();
  if (!theme || !voiceManager || uiState.selectedVoiceIndex >= VoiceSystem::MAX_VOICES)
    return;
  const auto *config = voiceManager->getVoiceConfig(
      voiceSystem.getVoiceId(uiState.selectedVoiceIndex));
  if (!config)
    return;

  for (uint8_t pad = 0; pad < SettingsPads::kPadCount; ++pad) {
    CRGB color = CRGB::Black;
    if (SettingsPads::available(pad, *config)) {
      const auto id = SettingsPads::parameter(pad);
      const bool toggle = VoiceEdit::parameter(id).unit == VoiceEdit::Unit::Toggle;
      const float level = SettingsPads::level(pad, *config);
      // Toggles: off is dark, on is solid. Other controls encode their value
      // as brightness; a small floor distinguishes minimum from unavailable.
      color = getVoiceGateColor(*theme, uiState.selectedVoiceIndex, true);
      color.nscale8(toggle ? (level > 0.5f ? 255 : 0)
                          : static_cast<uint8_t>(32 + 223 * level));
    }
    // Raw pad N is LED N, exactly as on the preset page (no -1 offset).
    ledMatrix.setLED(pad % LEDMatrix::WIDTH, pad / LEDMatrix::WIDTH, color);
  }
}

/**
 * @brief Render a voice pair (voices 1/2 or 3/4) into the LED matrix
 *
 * Displays gate states, playhead position, and slide effects for two voices
 * arranged in the two band row-pairs of the 8x4 matrix display.
 *
 * @param ledMatrix Reference to LED matrix for output
 * @param firstVoiceSequencer First voice sequencer (band 0)
 * @param secondVoiceSequencer Second voice sequencer (band 1)
 * @param firstVoiceIndex Index of the first voice in the pair (0 or 2)
 * @param themeColors Pointer to active theme colors
 * @param band Band index (0-based) of the pair's first voice in the matrix
 */
static void renderVoicePair(LEDMatrix &ledMatrix,
                            const Sequencer &firstVoiceSequencer,
                            const Sequencer &secondVoiceSequencer,
                            const LEDThemeColors *themeColors,
                            uint8_t firstVoiceIndex, uint8_t band) {
  // Validate sequencer gate step counts
  const uint8_t firstVoiceGateStepCount =
      firstVoiceSequencer.getParameterStepCount(ParamId::Gate);
  const uint8_t secondVoiceGateStepCount =
      secondVoiceSequencer.getParameterStepCount(ParamId::Gate);

  if (firstVoiceGateStepCount == 0) {
    DBG_WARN("renderVoicePair: First voice has zero gate step count");
    return;
  }
  if (secondVoiceGateStepCount == 0) {
    DBG_WARN("renderVoicePair: Second voice has zero gate step count");
    return;
  }

  // Render each step for both voices in the pair
  for (int stepIndex = 0; stepIndex < LEDConstants::MAX_STEP_BUTTONS;
       ++stepIndex) {
    // === First Voice (band) Processing ===
    const Step &firstVoiceStep = firstVoiceSequencer.getStep(stepIndex);
    const bool isFirstVoicePlayhead =
        (firstVoiceSequencer.getCurrentStepForParameter(ParamId::Gate) ==
             stepIndex &&
         firstVoiceSequencer.isRunning());

    // Determine base color based on gate state
    CRGB firstVoiceColor = getVoiceGateColor(*themeColors, firstVoiceIndex,
                                             firstVoiceStep.isGateActive);

    // Add slide effect if active for this step
    if (firstVoiceSequencer.getStepParameterValue(ParamId::Slide, stepIndex) >
        0) {
      nblend(firstVoiceColor, themeColors->modSlideActive,
             LEDConstants::MEDIUM_BRIGHTNESS);
    }

    // Add playhead accent if this is the current step
    if (isFirstVoicePlayhead) {
      firstVoiceColor += themeColors->playheadAccent;
    }

    // Apply smoothed color blending for the first voice's band
    const int topRowLEDIndex =
        ControlSurface::LedLayout::linearIndex(band, stepIndex);
    nblend(smoothedTargetColorBuffer[topRowLEDIndex], firstVoiceColor,
           TARGET_SMOOTHING_BLEND_AMOUNT);
    nblend(ledMatrix.getLeds()[topRowLEDIndex],
           smoothedTargetColorBuffer[topRowLEDIndex],
           LEDConstants::STANDARD_BLEND_AMOUNT);

    // === Second Voice (other band) Processing ===
    const Step &secondVoiceStep = secondVoiceSequencer.getStep(stepIndex);
    const bool isSecondVoicePlayhead =
        (secondVoiceSequencer.getCurrentStepForParameter(ParamId::Gate) ==
             stepIndex &&
         secondVoiceSequencer.isRunning());

    // Determine base color based on gate state
    CRGB secondVoiceColor = getVoiceGateColor(
        *themeColors, static_cast<uint8_t>(firstVoiceIndex + 1),
        secondVoiceStep.isGateActive);

    // Add slide effect if active for this step
    if (secondVoiceSequencer.getStepParameterValue(ParamId::Slide, stepIndex) >
        0) {
      nblend(secondVoiceColor, themeColors->modSlideActive,
             LEDConstants::MEDIUM_BRIGHTNESS);
    }

    // Add playhead accent if this is the current step
    if (isSecondVoicePlayhead) {
      secondVoiceColor += themeColors->playheadAccent;
    }

    // Apply smoothed color blending for the second voice's band
    const int bottomRowLEDIndex = ControlSurface::LedLayout::linearIndex(
        static_cast<uint8_t>(band + 1), stepIndex);
    nblend(smoothedTargetColorBuffer[bottomRowLEDIndex], secondVoiceColor,
           TARGET_SMOOTHING_BLEND_AMOUNT);
    nblend(ledMatrix.getLeds()[bottomRowLEDIndex],
           smoothedTargetColorBuffer[bottomRowLEDIndex],
           LEDConstants::STANDARD_BLEND_AMOUNT);
  }
}

/**
 * @brief Paint the 8x4 panel as the arp's 32-degree chord map.
 *
 * Arpeggiator mode reuses the panel the touch pads mirror: each LED is one
 * scale degree, so the grid shows the chord, where the scale's octaves fall and
 * which degree is sounding right now. Colour carries the state:
 *   - a finger on the pad: the arp voice's gate-on hue,
 *   - latched with the finger off: the same hue at gate-off brightness,
 *   - sounding: hue pushed toward the theme's playhead accent, brighter the
 *     higher the octave of range and the further the hand is from the sensor,
 *   - free: black, except a dim breathing marker on the scale's roots so the
 *     ladder is navigable in the dark,
 *   - no chord held at all: the whole panel breathes.
 *
 * @param ledMatrix Reference to the LED matrix for output
 * @param uiState Current UI state, which owns the arp engine
 */
static void renderArpPanel(LEDMatrix &ledMatrix, const UIState &uiState) {
  const LEDThemeColors *theme = getActiveThemeColors();
  const Arpeggiator::Engine &arp = uiState.arp;
  const uint8_t voice = uiState.selectedVoiceIndex < VoiceSystem::MAX_VOICES
                            ? uiState.selectedVoiceIndex
                            : 0;
  const size_t scaleIndex = std::min<size_t>(currentScale, SCALES_COUNT - 1);
  const int *row = scale[scaleIndex];
  const uint32_t now = millis();
  const bool noChord = arp.chordCount() == 0;
  const float breathing = smoothBreathing(now);
  // Lidar dynamics: the same hand that sets note velocity brightens the note
  // that is sounding, so the panel shows the gesture that is being heard.
  const uint8_t dynamicScale =
      static_cast<uint8_t>(LEDConstants::MEDIUM_BRIGHTNESS +
                           (arp.dynamics() * (LEDConstants::FULL_BRIGHTNESS -
                                              LEDConstants::MEDIUM_BRIGHTNESS)));

  for (uint8_t pad = 0; pad < Arpeggiator::kPadCount; ++pad) {
    const int ledIndex = Arpeggiator::ledIndexForPad(pad);
    if (ledIndex < 0) continue;

    CRGB target = CRGB::Black;
    if (noChord) {
      // Nothing entered yet: breathe so the panel reads as armed and waiting.
      target = theme->idleBreathingBlue;
      target.nscale8_video(static_cast<uint8_t>(
          LEDColors::BREATHING_MIN_INTENSITY +
          breathing * (LEDColors::BREATHING_MAX_INTENSITY -
                       LEDColors::BREATHING_MIN_INTENSITY)));
    } else if (arp.padInChord(pad)) {
      // Held or latched: the voice's hue, bright under a finger.
      target = getVoiceGateColor(*theme, voice, arp.padHeld(pad));
    } else if (row[pad] % 12 == 0) {
      // The scale's own roots (and octaves of them) as a dim landmark.
      target = theme->idleBreathingBlue;
      target.nscale8_video(static_cast<uint8_t>(LEDColors::BREATHING_MIN_INTENSITY +
                                                breathing * 8.0f));
    }

    if (arp.degreeSounding(pad)) {
      // Sounding now: push the hue toward the playhead accent, harder for the
      // higher octaves of the range so a climbing arp reads as a climb.
      uint8_t octave = 0;
      for (uint8_t slot = 0; slot < Arpeggiator::kMaxSlots; ++slot) {
        uint8_t degree = 0;
        uint8_t slotOctave = 0;
        if (arp.slotSounding(slot, degree, slotOctave) && degree == pad &&
            slotOctave > octave)
          octave = slotOctave;
      }
      const uint8_t accent = static_cast<uint8_t>(
          std::min<int>(200, 120 + (octave * 80) / (Arpeggiator::kMaxOctaves - 1)));
      target = getVoiceGateColor(*theme, voice, true);
      nblend(target, theme->playheadAccent, accent);
      target.nscale8_video(dynamicScale);
    }

    nblend(smoothedTargetColorBuffer[ledIndex], target,
           LEDConstants::TARGET_SMOOTHING_BLEND_AMOUNT);
    nblend(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex],
           LEDConstants::STANDARD_BLEND_AMOUNT);
  }
}

void updateStepLEDs(LEDMatrix &ledMatrix, const SequencerView &sequencers,
                    const UIState &uiState, int mm) {
  // If requested, immediately clear smoothed buffers to force a visual refresh
  if (uiState.resetStepsLightsFlag) {
    for (int i = 0; i < LEDConstants::MATRIX_TOTAL_LEDS; ++i) {
      smoothedTargetColorBuffer[i] = CRGB::Black;
    }
    // One-shot consumption of the flag. UIState is passed as const to
    // renderers, so we clear it here intentionally to prevent continuous
    // clearing every frame.
    const_cast<UIState &>(uiState).resetStepsLightsFlag = false;
  }

  // Handle settings mode LED feedback
  if (uiState.settingsMode) {
    updateSettingsModeLEDs(ledMatrix, uiState);
    return;
  }

  // Handle voice parameter mode LED feedback
  if (uiState.hasVoiceParameterFeedback(millis())) {
    updateVoiceParameterLEDs(ledMatrix, uiState);
    return;
  }

  // Arpeggiator mode owns the panel: it is the chord map, not the step grid.
  if (uiState.arp.active()) {
    renderArpPanel(ledMatrix, uiState);
    return;
  }

  const Sequencer &activeSeq = sequencers.clamped(uiState.selectedVoiceIndex);
  const ParamId heldParamIdForLength = getHeldParameterParamId(uiState);
  bool anyParamForLengthHeld = (heldParamIdForLength != ParamId::Count);
  ParamId activeParamIdForLength =
      anyParamForLengthHeld ? heldParamIdForLength : ParamId::Count;

  // Gate sequence length mode visualization: blink LEDs up to current gate
  // length for selected voice
  if (uiState.gateSeqLengthMode) {
    const uint8_t selBand = ControlSurface::LedLayout::bandOfVoiceInPair(
        uiState.selectedVoiceIndex);
    const CRGB withinColorBase = getVoiceGateColor(
        *getActiveThemeColors(), uiState.selectedVoiceIndex, true);

    // Simple blink state
    static bool blinkState = false;
    static uint32_t lastBlinkMs = 0;
    const uint32_t now = millis();
    if (now - lastBlinkMs > 250) { // ~4 Hz
      blinkState = !blinkState;
      lastBlinkMs = now;
    }

    const uint8_t gateLen = activeSeq.getParameterStepCount(ParamId::Gate);

    // Dim the other band fully to focus on the selected voice
    for (int step = 0; step < LEDConstants::MAX_STEP_BUTTONS; ++step) {
      const int otherIndex = ControlSurface::LedLayout::linearIndex(
          static_cast<uint8_t>(1 - selBand), step);
      nblend(smoothedTargetColorBuffer[otherIndex], CRGB::Black,
             LEDConstants::TARGET_SMOOTHING_BLEND_AMOUNT);
      nblend(ledMatrix.getLeds()[otherIndex],
             smoothedTargetColorBuffer[otherIndex],
             LEDConstants::DIM_BLEND_AMOUNT);
    }

    // Paint selected band with blinking up-to-length visualization
    for (int step = 0; step < LEDConstants::MAX_STEP_BUTTONS; ++step) {
      CRGB target = CRGB::Black;
      if (step < gateLen && gateLen > 1) {
        target = withinColorBase;
        if (blinkState) {
          // Dim on alternate frames for blink
          target.nscale8(60);
        }
      }
      const int ledIndex =
          ControlSurface::LedLayout::linearIndex(selBand, step);
      nblend(smoothedTargetColorBuffer[ledIndex], target,
             LEDConstants::TARGET_SMOOTHING_BLEND_AMOUNT);
      nblend(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex],
             LEDConstants::STANDARD_BLEND_AMOUNT);
    }

    return;
  }

  if (uiState.slideMode) {
    uint8_t slidePlayhead =
        activeSeq.getCurrentStepForParameter(ParamId::Slide);
    uint8_t slideLength = activeSeq.getParameterStepCount(ParamId::Slide);

    for (int step = 0; step < NUMBER_OF_STEP_BUTTONS; step++) {
      uint8_t slideValue =
          activeSeq.getStepParameterValue(ParamId::Slide, step);
      bool isSlideActive = (slideValue > 0);
      bool isPlayhead = (step == slidePlayhead);
      bool isWithinLength = (step < slideLength);

      CRGB color;
      if (isPlayhead && isWithinLength) {
        color = activeThemeColors->modSlideActive;
      } else if (isSlideActive && isWithinLength) {
        color = activeThemeColors->modSlideActive;
        color.nscale8(64);
      } else if (isWithinLength) {
        color = activeThemeColors->modSlideInactive;
        color.nscale8(32);
      } else {
        color = CRGB::Black;
      }

      const int x = ControlSurface::LedLayout::x(step);
      const int y = ControlSurface::LedLayout::y(
          ControlSurface::LedLayout::bandOfVoiceInPair(
              uiState.selectedVoiceIndex),
          step);
      if (x >= 0 && y >= 0) {
        ledMatrix.setLED(x, y, color);
      }
    }
    return;
  }

  bool paramValueEditActive = isAnyParameterButtonHeld(uiState);

  if (paramValueEditActive) {
    uint8_t currentLength =
        activeSeq.getParameterStepCount(activeParamIdForLength);
    uint8_t paramPlayhead =
        activeSeq.getCurrentStepForParameter(activeParamIdForLength);

    // Dim the non-selected band (top or bottom) in the current page
    const uint8_t selBand = ControlSurface::LedLayout::bandOfVoiceInPair(
        uiState.selectedVoiceIndex);
    bool isSecondInPair = selBand == 1;
    for (int step = 0; step < SEQ_STEPS; ++step) {
      int topIndex = ControlSurface::LedLayout::linearIndex(0, step);
      int bottomIndex = ControlSurface::LedLayout::linearIndex(1, step);
      if (!isSecondInPair) {
        // Selected voice is top row; dim bottom
        nblend(smoothedTargetColorBuffer[bottomIndex], CRGB::Black,
               TARGET_SMOOTHING_BLEND_AMOUNT);
        nblend(ledMatrix.getLeds()[bottomIndex],
               smoothedTargetColorBuffer[bottomIndex], 32);
      } else {
        // Selected voice is bottom row; dim top
        nblend(smoothedTargetColorBuffer[topIndex], CRGB::Black,
               TARGET_SMOOTHING_BLEND_AMOUNT);
        nblend(ledMatrix.getLeds()[topIndex],
               smoothedTargetColorBuffer[topIndex], 32);
      }
    }

    // Paint the selected row with parameter length/playhead info
    for (int step = 0; step < SEQ_STEPS; ++step) {
      CRGB targetColor;
      if (step < currentLength) {
        if (step == paramPlayhead && activeSeq.isRunning()) {
          targetColor = getParameterColor(activeParamIdForLength, 180);
        } else {
          // Use V1 tint for top row, V2 tint for bottom row
          targetColor = isSecondInPair ? activeThemeColors->editModeDimBlueV2
                                       : activeThemeColors->editModeDimBlueV1;
        }
      } else {
        targetColor = CRGB::Black;
      }
      int ledIndex = ControlSurface::LedLayout::linearIndex(selBand, step);
      nblend(smoothedTargetColorBuffer[ledIndex], targetColor,
             TARGET_SMOOTHING_BLEND_AMOUNT);
      nblend(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex],
             isSecondInPair ? 122 : 64);
    }

    return;
  }

  if (anyParamForLengthHeld) {
    uint8_t currentLength =
        activeSeq.getParameterStepCount(activeParamIdForLength);
    uint8_t paramPlayhead =
        activeSeq.getCurrentStepForParameter(activeParamIdForLength);

    // Paint only the selected band's within-length area
    const uint8_t selBand = ControlSurface::LedLayout::bandOfVoiceInPair(
        uiState.selectedVoiceIndex);
    bool isSecondInPair = selBand == 1;
    for (int step = 0; step < currentLength; ++step) {
      CRGB targetColor =
          (step == paramPlayhead && activeSeq.isRunning())
              ? getParameterColor(activeParamIdForLength, 180)
              : (isSecondInPair ? activeThemeColors->editModeDimBlueV2
                                : activeThemeColors->editModeDimBlueV1);
      int ledIndex = ControlSurface::LedLayout::linearIndex(selBand, step);
      nblend(smoothedTargetColorBuffer[ledIndex], targetColor,
             TARGET_SMOOTHING_BLEND_AMOUNT);
      nblend(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex],
             isSecondInPair ? 200 : 60);
    }

    // Dim the other band's within-length area
    for (int step = 0; step < currentLength; ++step) {
      int otherIndex = ControlSurface::LedLayout::linearIndex(
          static_cast<uint8_t>(1 - selBand), step);
      nblend(smoothedTargetColorBuffer[otherIndex], CRGB::Black,
             TARGET_SMOOTHING_BLEND_AMOUNT);
      nblend(ledMatrix.getLeds()[otherIndex],
             smoothedTargetColorBuffer[otherIndex], 150);
    }
  } else {
    // Determine which voice pair to display based on selectedVoiceIndex
    const uint8_t firstVoice = (uiState.selectedVoiceIndex < 2) ? 0 : 2;
    const Sequencer &firstSeq = sequencers.clamped(firstVoice);
    const Sequencer &secondSeq = sequencers.clamped(firstVoice + 1);
    const LEDThemeColors *theme = getActiveThemeColors();

    // Clear first to avoid ghosting when switching pages
    for (int i = 0; i < LEDMatrix::WIDTH * LEDMatrix::HEIGHT; ++i) {
      nblend(smoothedTargetColorBuffer[i], CRGB::Black,
             TARGET_SMOOTHING_BLEND_AMOUNT);
      nblend(ledMatrix.getLeds()[i], smoothedTargetColorBuffer[i], 64);
    }

    // Render either voices 1/2 (page 1) or 3/4 (page 2)
    renderVoicePair(ledMatrix, firstSeq, secondSeq, theme, firstVoice, 0);

    // Polyrhythmic overlays for the visible pair only
    addPolyrhythmicOverlay(ledMatrix, firstSeq, 0, 32);
    addPolyrhythmicOverlay(ledMatrix, secondSeq, 1, 32);

    // Highlight selected step if editing
    if (uiState.selectedStepForEdit >= 0 &&
        uiState.selectedStepForEdit < SEQ_STEPS) {
      int ledIndex = ControlSurface::LedLayout::linearIndex(
          ControlSurface::LedLayout::bandOfVoiceInPair(
              uiState.selectedVoiceIndex),
          static_cast<uint8_t>(uiState.selectedStepForEdit));

      static bool blinkState = false;
      static uint32_t lastBlinkTime = 0;
      uint32_t currentTime = millis();
      if (currentTime - lastBlinkTime > 500) {
        blinkState = !blinkState;
        lastBlinkTime = currentTime;
      }

      CRGB highlightColor = blinkState ? CRGB::White : CRGB::Black;
      nblend(smoothedTargetColorBuffer[ledIndex], highlightColor,
             TARGET_SMOOTHING_BLEND_AMOUNT);
      nblend(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex],
             100);
    }
  }
}
