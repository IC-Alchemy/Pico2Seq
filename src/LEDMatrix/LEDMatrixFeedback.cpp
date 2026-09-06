#include <Arduino.h>
#include "LEDMatrixFeedback.h"
#include <FastLED.h>
#include <cmath>

#include "ledMatrix.h"
#include "LEDConstants.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../ui/UIEventHandler.h"
#include "../ui/ControlSurfaceLogic.h"
#include "../ui/ButtonManager.h"
#include "../voice/VoicePresets.h"
#include "../utils/Debug.h"

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
static_assert(LEDConstants::MATRIX_HEIGHT == ControlSurface::LedLayout::kBandCount * ControlSurface::LedLayout::kRowsPerBand);
static_assert(LEDConstants::MATRIX_TOTAL_LEDS == ControlSurface::LedLayout::kLedCount);
static_assert(LEDConstants::BOTTOM_HALF_OFFSET == ControlSurface::LedLayout::kStepsPerBand);
// Smoothed color buffer for smooth LED transitions
CRGB smoothedTargetColorBuffer[LEDConstants::MATRIX_TOTAL_LEDS];

// Color blending constants from LEDConstants
static constexpr uint8_t TARGET_SMOOTHING_BLEND_AMOUNT = LEDConstants::TARGET_SMOOTHING_BLEND_AMOUNT;

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

const LEDThemeColors ALL_THEMES[] = {
    { // DEFAULT - blue-green progression
     {CRGB(0, 148, 188), CRGB(0, 172, 178), CRGB(16, 180, 160), CRGB(32, 188, 132)},
     {CRGB(34, 2, 54), CRGB(40, 2, 58), CRGB(48, 3, 60), CRGB(56, 5, 62)},
     CRGB(0, 44, 54),
     CRGB(0, 0, 94), CRGB(0, 0, 12), CRGB(0, 0, 12), CRGB(128, 94, 0), CRGB(32, 24, 0),
     CRGB(94, 0, 94), CRGB(12, 0, 12), CRGB(0, 94, 188), CRGB(0, 24, 48), CRGB(188, 64, 0),
     CRGB(48, 16, 0), CRGB(128, 0, 0), CRGB(32, 0, 0), CRGB(0, 128, 64), CRGB(0, 32, 16),
     CRGB(188, 0, 188), CRGB(48, 0, 48), CRGB(64, 64, 128), CRGB(16, 16, 32), CRGB(128, 64, 0),
     CRGB(32, 16, 0), CRGB(94, 0, 64), CRGB(24, 0, 16), CRGB(64, 94, 94), CRGB(16, 24, 24)},
    { // OCEANIC - deep blue through seafoam
     {CRGB(0, 112, 188), CRGB(0, 138, 190), CRGB(0, 162, 184), CRGB(0, 176, 148)},
     {CRGB(16, 8, 54), CRGB(22, 8, 60), CRGB(28, 10, 64), CRGB(34, 12, 66)},
     CRGB(0, 38, 48),
     CRGB(0, 48, 144), CRGB(0, 5, 17), CRGB(0, 12, 17), CRGB(0, 144, 188), CRGB(0, 15, 22),
     CRGB(64, 144, 188), CRGB(13, 29, 38), CRGB(94, 0, 188), CRGB(11, 0, 24), CRGB(188, 144, 0),
     CRGB(38, 29, 0), CRGB(144, 188, 94), CRGB(29, 38, 19), CRGB(188, 0, 94), CRGB(17, 0, 11),
     CRGB(144, 0, 188), CRGB(29, 0, 38), CRGB(48, 144, 144), CRGB(10, 29, 29), CRGB(0, 166, 188),
     CRGB(0, 33, 38), CRGB(144, 0, 188), CRGB(15, 0, 22), CRGB(0, 188, 166), CRGB(0, 22, 15)},
    {
        // VOLCANIC theme - red/orange fire on near-black
        {CRGB(220, 65, 20), CRGB(235, 88, 20), CRGB(245, 112, 25), CRGB(255, 138, 35)},
        {CRGB(55, 3, 8), CRGB(60, 5, 8), CRGB(66, 7, 10), CRGB(72, 10, 12)},
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
    {
        // FOREST theme - greens and warm browns on dark moss
        {CRGB(28, 150, 55), CRGB(48, 162, 62), CRGB(38, 172, 82), CRGB(72, 182, 68)},
        {CRGB(36, 14, 3), CRGB(42, 18, 3), CRGB(44, 22, 4), CRGB(48, 26, 5)},
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
    {
        // NEON theme - bright cyan/magenta on dark
        {CRGB(0, 220, 235), CRGB(0, 232, 205), CRGB(0, 225, 165), CRGB(34, 235, 125)},
        {CRGB(45, 0, 65), CRGB(52, 0, 70), CRGB(56, 0, 75), CRGB(60, 4, 76)},
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
    // DARK_NOCTIS theme - deep charcoal with cool blue/cyan accents
    {
        {CRGB(25, 85, 140), CRGB(30, 103, 150), CRGB(35, 118, 158), CRGB(48, 126, 170)},
        {CRGB(28, 5, 46), CRGB(32, 6, 50), CRGB(36, 7, 54), CRGB(40, 9, 56)},
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
    {
        // DARK_EMBER theme - deep charcoal with warm amber ember accents
        {CRGB(200, 100, 40), CRGB(215, 120, 46), CRGB(230, 140, 56), CRGB(245, 160, 70)},
        {CRGB(55, 4, 8), CRGB(62, 5, 9), CRGB(68, 7, 11), CRGB(74, 10, 13)},
        CRGB(40, 24, 18),    // playheadAccent - dark warm accent
        CRGB(28, 22, 20),    // idleBreathingBlue - warm slate for breathing (amber-tinted)
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

    {
        // MODERN theme - muted, high-legibility palette with warm accent
        {CRGB(48, 170, 120), CRGB(56, 178, 138), CRGB(68, 176, 112), CRGB(84, 184, 132)},
        {CRGB(42, 16, 42), CRGB(46, 18, 44), CRGB(50, 20, 45), CRGB(54, 22, 46)},
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
    {
        // BLUE theme - high-contrast cool blues and cyan accents
        {CRGB(30, 105, 185), CRGB(35, 124, 195), CRGB(45, 142, 205), CRGB(64, 154, 212)},
        {CRGB(22, 5, 55), CRGB(28, 6, 62), CRGB(34, 8, 68), CRGB(40, 10, 72)},
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
    {
        // GREEN theme - lush greens with clean high-contrast accents
        {CRGB(35, 145, 75), CRGB(40, 160, 90), CRGB(45, 174, 105), CRGB(55, 184, 120)},
        {CRGB(38, 16, 4), CRGB(44, 20, 5), CRGB(50, 24, 6), CRGB(56, 28, 7)},
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

static_assert(sizeof(ALL_THEMES) / sizeof(ALL_THEMES[0]) == static_cast<int>(LEDTheme::COUNT),
              "Every LED theme needs one palette entry");

static const LEDThemeColors *activeThemeColors = &ALL_THEMES[static_cast<int>(LEDTheme::DEFAULT)];

static const CRGB &getVoiceGateColor(const LEDThemeColors &themeColors, uint8_t voiceIndex, bool gateActive)
{
    const uint8_t clampedVoiceIndex = voiceIndex < LED_THEME_VOICE_COUNT ? voiceIndex : 0;
    return gateActive ? themeColors.gateOn[clampedVoiceIndex] : themeColors.gateOff[clampedVoiceIndex];
}

void setLEDTheme(LEDTheme theme)
{
    if (static_cast<int>(theme) < static_cast<int>(LEDTheme::COUNT))
    {
        activeThemeColors = &ALL_THEMES[static_cast<int>(theme)];
    }
}

const LEDThemeColors *getActiveThemeColors()
{
    return activeThemeColors;
}

DEFINE_GRADIENT_PALETTE(parameterPalette){
    0, 0, 0, 255,   // Blue
    85, 0, 255, 0,  // Green
    170, 255, 0, 0, // Red
    255, 0, 0, 255  // Back to blue
};
CRGBPalette16 parameterColors = parameterPalette;

CRGB getParameterColor(ParamId param, uint8_t intensity)
{
    uint8_t paletteIndex = map(static_cast<int>(param), 0, static_cast<int>(ParamId::Count), 0, 255);
    return ColorFromPalette(parameterColors, paletteIndex, intensity);
}

void addPolyrhythmicOverlay(
    LEDMatrix &ledMatrix,
    const Sequencer &sequencer,
    uint8_t band,
    uint8_t overlayIntensity = LEDConstants::POLYRHYTHM_INTENSITY)
{
    // Only add overlay if sequencer is actively running
    if (!sequencer.isRunning())
    {
        return;
    }

    // Parameter overlay configuration for polyrhythmic visualization
    struct PolyrhythmicParameterOverlay
    {
        ParamId parameterID;
        CRGB overlayColor;
    };

    const PolyrhythmicParameterOverlay overlayParameters[LEDConstants::POLYRHYTHM_PARAM_COUNT] = {
        {ParamId::Note, LEDColors::POLYRHYTHM_NOTE},
        {ParamId::Velocity, LEDColors::POLYRHYTHM_VELOCITY},
        {ParamId::Filter, LEDColors::POLYRHYTHM_FILTER}};

    // Apply overlay for each parameter type
    for (size_t paramIndex = 0; paramIndex < LEDConstants::POLYRHYTHM_PARAM_COUNT; ++paramIndex)
    {
        const ParamId currentParameter = overlayParameters[paramIndex].parameterID;
        const uint8_t currentParameterStep = sequencer.getCurrentStepForParameter(currentParameter);
        const uint8_t parameterStepCount = sequencer.getParameterStepCount(currentParameter);

        // Only apply overlay if parameter is within valid bounds
        if (currentParameterStep < LEDConstants::MAX_STEP_BUTTONS &&
            parameterStepCount > 1 &&
            parameterStepCount <= LEDConstants::MAX_STEP_BUTTONS)
        {

            // Calculate LED matrix position
            const int ledLinearIndex = ControlSurface::LedLayout::linearIndex(band, currentParameterStep);
            if (ledLinearIndex < 0)
            {
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

float ease(float x)
{
    return x < 0.5 ? 2 * x * x : 1 - pow(-2 * x + 2, 2) / 2;
}

float smoothBreathing(uint32_t timeMs)
{
    // Calculate smooth breathing animation value using easing function
    const float normalizedTime = static_cast<float>(timeMs % LEDConstants::BREATHING_CYCLE_MS) /
                                 static_cast<float>(LEDConstants::BREATHING_CYCLE_MS);
    return ease(0.5f * (1.0f + sin(2.0f * PI * normalizedTime)));
}

void setStepLedColor(uint8_t stepIndex, uint8_t redValue, uint8_t greenValue, uint8_t blueValue)
{
    // Legacy function for setting individual step LED colors
    // Note: This function requires a LEDMatrix reference to work properly
    // Consider using the main LED update functions instead
}

void setupLEDMatrixFeedback()
{
    // Initialize smoothed color buffer to black (off state)
    for (int ledIndex = 0; ledIndex < LEDConstants::MATRIX_TOTAL_LEDS; ++ledIndex)
    {
        smoothedTargetColorBuffer[ledIndex] = LEDColors::BLACK;
    }
}

/**
 * @brief Updates LED matrix to show settings mode interface
 *
 * Displays menu options and preset selections using step LEDs:
 * - Main menu: Shows Voice 1 and Voice 2 options (steps 0-1)
 * - Preset selection: Shows available presets (steps 0-5 for 6 presets)
 * - Uses different colors to indicate current selection and available options
 */
void updateSettingsModeLEDs(LEDMatrix &ledMatrix, const UIState &uiState)
{
    const LEDThemeColors *activeThemeColors = getActiveThemeColors();

    // Clear all LEDs first
    for (int i = 0; i < LEDMatrix::WIDTH * LEDMatrix::HEIGHT; ++i)
    {
        ledMatrix.getLeds()[i] = CRGB::Black;
    }

    if (uiState.inPresetSelection)
    {
        // Preset selection mode - show available presets
        const uint8_t totalPresets = VoicePresets::getPresetCount();
        const uint8_t presetCount = (totalPresets < SEQ_STEPS) ? totalPresets : SEQ_STEPS;

        // Keep preset selection in the hue assigned to the configured voice.
        CRGB selectedColor = getVoiceGateColor(*activeThemeColors, uiState.settingsMenuIndex, true);
        CRGB availableColor = getVoiceGateColor(*activeThemeColors, uiState.settingsMenuIndex, false);

        // Show presets in first 6 step positions (0-5)
        for (uint8_t i = 0; i < presetCount && i < SEQ_STEPS; i++)
        {
            CRGB color;

            // Highlight currently selected preset
            const uint8_t voiceIndex = uiState.settingsMenuIndex < UIState::MAX_VOICES
                                           ? uiState.settingsMenuIndex
                                           : 0;
            const uint8_t currentPresetIndex = uiState.voicePresetIndices[voiceIndex];

            if (i == currentPresetIndex)
            {
                // Current preset - bright pulsing
                uint32_t time = millis();
                float pulse = 0.5f + 0.5f * sinf(time * 0.008f);
                color = selectedColor;
                color.nscale8(static_cast<uint8_t>(128 + 127 * pulse));
            }
            else
            {
                // Available preset - dim steady
                color = availableColor;
                color.nscale8(64);
            }

            // Calculate LED position
            int x = i % LEDMatrix::WIDTH;
            int y = i / LEDMatrix::WIDTH;
            ledMatrix.setLED(x, y + 1, color);
        }
    }
    else
    {
        // Main settings menu - show all 4 voice options
        // Show all 4 voice options in first row
        for (int voiceIndex = 0; voiceIndex < 4; voiceIndex++)
        {
            CRGB voiceColor = getVoiceGateColor(*activeThemeColors, static_cast<uint8_t>(voiceIndex),
                                                 uiState.settingsMenuIndex == voiceIndex);

            // Add pulsing effect for selected option
            if (uiState.settingsMenuIndex == voiceIndex)
            {
                uint32_t time = millis();
                float pulse = 0.5f + 0.5f * sinf(time * 0.006f);
                voiceColor.nscale8(static_cast<uint8_t>(128 + 127 * pulse));
            }
            else
            {
                voiceColor.nscale8(96);
            }

            // Set LED for voice option
            ledMatrix.setLED(voiceIndex, 0, voiceColor);
        }
    }
}

void updateVoiceParameterLEDs(LEDMatrix &ledMatrix, const UIState &uiState)
{
    if (!uiState.inVoiceParameterMode)
        return;

    // Get active theme colors
    const LEDThemeColors *activeThemeColors = getActiveThemeColors();
    if (!activeThemeColors)
        return;

    // Clear all LEDs first
    for (int i = 0; i < LEDMatrix::WIDTH * LEDMatrix::HEIGHT; i++)
    {
        ledMatrix.setLED(i % LEDMatrix::WIDTH, i / LEDMatrix::WIDTH, CRGB::Black);
    }

    // Map button index to LED position (buttons 9-24 map to steps 8-23)
    uint8_t ledIndex = uiState.lastVoiceParameterButton - 1;
    if (ledIndex >= LEDMatrix::WIDTH * LEDMatrix::HEIGHT)
        return;

    // Choose color based on voice and parameter type
    CRGB paramColor;

    switch (uiState.lastVoiceParameterButton)
    {
    case 9: // Envelope
        paramColor = uiState.isVoice2Mode ? activeThemeColors->modAttackActive : activeThemeColors->modDecayActive;
        break;
    case 10: // Overdrive
        paramColor = uiState.isVoice2Mode ? activeThemeColors->modFilterActive : activeThemeColors->modVelocityActive;
        break;
    case 11: // (was Wavefolder; button removed with the wavefolder effect)
        paramColor = uiState.isVoice2Mode ? activeThemeColors->modOctaveActive : activeThemeColors->modNoteActive;
        break;
    case 12: // Filter Mode
        paramColor = getVoiceGateColor(*activeThemeColors, uiState.selectedVoiceIndex, true);
        break;
    case 13: // Filter Resonance
        paramColor = uiState.isVoice2Mode ? activeThemeColors->modSlideActive : activeThemeColors->modParamModeActive;
        break;
    default:
        paramColor = uiState.isVoice2Mode ? activeThemeColors->defaultActive : activeThemeColors->defaultInactive;
        break;
    }

    // Create pulsing effect for 3 seconds
    if (millis() - uiState.voiceParameterChangeTime < 3000)
    {
        uint32_t time = millis();
        float pulse = 0.5f + 0.5f * sinf(time * 0.01f); // Faster pulse for voice parameters
        paramColor.nscale8(static_cast<uint8_t>(128 + 127 * pulse));
    }
    else
    {
        paramColor.nscale8(64); // Dim after timeout
    }

    // Set the LED for the voice parameter button
    ledMatrix.setLED(ledIndex % LEDMatrix::WIDTH, ledIndex / LEDMatrix::WIDTH, paramColor);
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
static void renderVoicePair(
    LEDMatrix &ledMatrix,
    const Sequencer &firstVoiceSequencer,
    const Sequencer &secondVoiceSequencer,
    const LEDThemeColors *themeColors,
    uint8_t firstVoiceIndex,
    uint8_t band)
{
    // Validate sequencer gate step counts
    const uint8_t firstVoiceGateStepCount = firstVoiceSequencer.getParameterStepCount(ParamId::Gate);
    const uint8_t secondVoiceGateStepCount = secondVoiceSequencer.getParameterStepCount(ParamId::Gate);

    if (firstVoiceGateStepCount == 0)
    {
        DBG_WARN("renderVoicePair: First voice has zero gate step count");
        return;
    }
    if (secondVoiceGateStepCount == 0)
    {
        DBG_WARN("renderVoicePair: Second voice has zero gate step count");
        return;
    }

    // Render each step for both voices in the pair
    for (int stepIndex = 0; stepIndex < LEDConstants::MAX_STEP_BUTTONS; ++stepIndex)
    {
        // === First Voice (band) Processing ===
        const Step &firstVoiceStep = firstVoiceSequencer.getStep(stepIndex);
        const bool isFirstVoicePlayhead = (firstVoiceSequencer.getCurrentStepForParameter(ParamId::Gate) == stepIndex &&
                                           firstVoiceSequencer.isRunning());

        // Determine base color based on gate state
        CRGB firstVoiceColor = getVoiceGateColor(*themeColors, firstVoiceIndex, firstVoiceStep.isGateActive);

        // Add slide effect if active for this step
        if (firstVoiceSequencer.getStepParameterValue(ParamId::Slide, stepIndex) > 0)
        {
            nblend(firstVoiceColor, themeColors->modSlideActive, LEDConstants::MEDIUM_BRIGHTNESS);
        }

        // Add playhead accent if this is the current step
        if (isFirstVoicePlayhead)
        {
            firstVoiceColor += themeColors->playheadAccent;
        }

        // Apply smoothed color blending for the first voice's band
        const int topRowLEDIndex = ControlSurface::LedLayout::linearIndex(band, stepIndex);
        nblend(smoothedTargetColorBuffer[topRowLEDIndex], firstVoiceColor, TARGET_SMOOTHING_BLEND_AMOUNT);
        nblend(ledMatrix.getLeds()[topRowLEDIndex], smoothedTargetColorBuffer[topRowLEDIndex],
               LEDConstants::STANDARD_BLEND_AMOUNT);

        // === Second Voice (other band) Processing ===
        const Step &secondVoiceStep = secondVoiceSequencer.getStep(stepIndex);
        const bool isSecondVoicePlayhead = (secondVoiceSequencer.getCurrentStepForParameter(ParamId::Gate) == stepIndex &&
                                            secondVoiceSequencer.isRunning());

        // Determine base color based on gate state
        CRGB secondVoiceColor = getVoiceGateColor(
            *themeColors, static_cast<uint8_t>(firstVoiceIndex + 1), secondVoiceStep.isGateActive);

        // Add slide effect if active for this step
        if (secondVoiceSequencer.getStepParameterValue(ParamId::Slide, stepIndex) > 0)
        {
            nblend(secondVoiceColor, themeColors->modSlideActive, LEDConstants::MEDIUM_BRIGHTNESS);
        }

        // Add playhead accent if this is the current step
        if (isSecondVoicePlayhead)
        {
            secondVoiceColor += themeColors->playheadAccent;
        }

        // Apply smoothed color blending for the second voice's band
        const int bottomRowLEDIndex = ControlSurface::LedLayout::linearIndex(
            static_cast<uint8_t>(band + 1), stepIndex);
        nblend(smoothedTargetColorBuffer[bottomRowLEDIndex], secondVoiceColor, TARGET_SMOOTHING_BLEND_AMOUNT);
        nblend(ledMatrix.getLeds()[bottomRowLEDIndex], smoothedTargetColorBuffer[bottomRowLEDIndex],
               LEDConstants::STANDARD_BLEND_AMOUNT);
    }
}

void updateStepLEDs(
    LEDMatrix &ledMatrix,
    const Sequencer &seq1,
    const Sequencer &seq2,
    const Sequencer &seq3,
    const Sequencer &seq4,
    const UIState &uiState,
    int mm)
{
    // If requested, immediately clear smoothed buffers to force a visual refresh
    if (uiState.resetStepsLightsFlag)
    {
        for (int i = 0; i < LEDConstants::MATRIX_TOTAL_LEDS; ++i)
        {
            smoothedTargetColorBuffer[i] = CRGB::Black;
        }
        // One-shot consumption of the flag. UIState is passed as const to renderers,
        // so we clear it here intentionally to prevent continuous clearing every frame.
        const_cast<UIState &>(uiState).resetStepsLightsFlag = false;
    }

    // Handle settings mode LED feedback
    if (uiState.settingsMode)
    {
        updateSettingsModeLEDs(ledMatrix, uiState);
        return;
    }

    // Handle voice parameter mode LED feedback
    if (uiState.inVoiceParameterMode && (millis() - uiState.voiceParameterChangeTime < 3000))
    {
        updateVoiceParameterLEDs(ledMatrix, uiState);
        return;
    }

    const ParamId heldParamIdForLength = getHeldParameterParamId(uiState);
    bool anyParamForLengthHeld = (heldParamIdForLength != ParamId::Count);
    ParamId activeParamIdForLength = anyParamForLengthHeld ? heldParamIdForLength : ParamId::Count;

    // Gate sequence length mode visualization: blink LEDs up to current gate length for selected voice
    if (uiState.gateSeqLengthMode)
    {
        // Select active sequencer by selectedVoiceIndex (0..3)
        const Sequencer *seqPtr = (uiState.selectedVoiceIndex == 0) ? &seq1 : (uiState.selectedVoiceIndex == 1) ? &seq2
                                                                          : (uiState.selectedVoiceIndex == 2)   ? &seq3
                                                                                                                : &seq4;
        const Sequencer &activeSeq = *seqPtr;

        const uint8_t selBand = ControlSurface::LedLayout::bandOfVoiceInPair(uiState.selectedVoiceIndex);
        const CRGB withinColorBase = getVoiceGateColor(*getActiveThemeColors(), uiState.selectedVoiceIndex, true);

        // Simple blink state
        static bool blinkState = false;
        static uint32_t lastBlinkMs = 0;
        const uint32_t now = millis();
        if (now - lastBlinkMs > 250)
        { // ~4 Hz
            blinkState = !blinkState;
            lastBlinkMs = now;
        }

        const uint8_t gateLen = activeSeq.getParameterStepCount(ParamId::Gate);

        // Dim the other band fully to focus on the selected voice
        for (int step = 0; step < LEDConstants::MAX_STEP_BUTTONS; ++step)
        {
            const int otherIndex = ControlSurface::LedLayout::linearIndex(static_cast<uint8_t>(1 - selBand), step);
            nblend(smoothedTargetColorBuffer[otherIndex], CRGB::Black, LEDConstants::TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[otherIndex], smoothedTargetColorBuffer[otherIndex], LEDConstants::DIM_BLEND_AMOUNT);
        }

        // Paint selected band with blinking up-to-length visualization
        for (int step = 0; step < LEDConstants::MAX_STEP_BUTTONS; ++step)
        {
            CRGB target = CRGB::Black;
            if (step < gateLen && gateLen > 1)
            {
                target = withinColorBase;
                if (blinkState)
                {
                    // Dim on alternate frames for blink
                    target.nscale8(60);
                }
            }
            const int ledIndex = ControlSurface::LedLayout::linearIndex(selBand, step);
            nblend(smoothedTargetColorBuffer[ledIndex], target, LEDConstants::TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex], LEDConstants::STANDARD_BLEND_AMOUNT);
        }

        return;
    }

    if (uiState.slideMode)
    {
        // Select sequencer based on selectedVoiceIndex (0..3)
        const Sequencer *seqPtr = (uiState.selectedVoiceIndex == 0) ? &seq1 : (uiState.selectedVoiceIndex == 1) ? &seq2
                                                                          : (uiState.selectedVoiceIndex == 2)   ? &seq3
                                                                                                                : &seq4;
        const Sequencer &activeSeq = *seqPtr;
        uint8_t slidePlayhead = activeSeq.getCurrentStepForParameter(ParamId::Slide);
        uint8_t slideLength = activeSeq.getParameterStepCount(ParamId::Slide);

        for (int step = 0; step < NUMBER_OF_STEP_BUTTONS; step++)
        {
            uint8_t slideValue = activeSeq.getStepParameterValue(ParamId::Slide, step);
            bool isSlideActive = (slideValue > 0);
            bool isPlayhead = (step == slidePlayhead);
            bool isWithinLength = (step < slideLength);

            CRGB color;
            if (isPlayhead && isWithinLength)
            {
                color = activeThemeColors->modSlideActive;
            }
            else if (isSlideActive && isWithinLength)
            {
                color = activeThemeColors->modSlideActive;
                color.nscale8(64);
            }
            else if (isWithinLength)
            {
                color = activeThemeColors->modSlideInactive;
                color.nscale8(32);
            }
            else
            {
                color = CRGB::Black;
            }

            const int x = ControlSurface::LedLayout::x(step);
            const int y = ControlSurface::LedLayout::y(
                ControlSurface::LedLayout::bandOfVoiceInPair(uiState.selectedVoiceIndex), step);
            if (x >= 0 && y >= 0)
            {
                ledMatrix.setLED(x, y, color);
            }
        }
        return;
    }

    bool paramValueEditActive = isAnyParameterButtonHeld(uiState);

    // Helper to fetch by selected voice
    auto &activeSeqRef = (uiState.selectedVoiceIndex == 0) ? seq1 : (uiState.selectedVoiceIndex == 1) ? seq2
                                                                : (uiState.selectedVoiceIndex == 2)   ? seq3
                                                                                                      : seq4;

    if (paramValueEditActive)
    {
        uint8_t currentLength = activeSeqRef.getParameterStepCount(activeParamIdForLength);
        uint8_t paramPlayhead = activeSeqRef.getCurrentStepForParameter(activeParamIdForLength);

        // Dim the non-selected band (top or bottom) in the current page
        const uint8_t selBand = ControlSurface::LedLayout::bandOfVoiceInPair(uiState.selectedVoiceIndex);
        bool isSecondInPair = selBand == 1;
        for (int step = 0; step < SEQ_STEPS; ++step)
        {
            int topIndex = ControlSurface::LedLayout::linearIndex(0, step);
            int bottomIndex = ControlSurface::LedLayout::linearIndex(1, step);
            if (!isSecondInPair)
            {
                // Selected voice is top row; dim bottom
                nblend(smoothedTargetColorBuffer[bottomIndex], CRGB::Black, TARGET_SMOOTHING_BLEND_AMOUNT);
                nblend(ledMatrix.getLeds()[bottomIndex], smoothedTargetColorBuffer[bottomIndex], 32);
            }
            else
            {
                // Selected voice is bottom row; dim top
                nblend(smoothedTargetColorBuffer[topIndex], CRGB::Black, TARGET_SMOOTHING_BLEND_AMOUNT);
                nblend(ledMatrix.getLeds()[topIndex], smoothedTargetColorBuffer[topIndex], 32);
            }
        }

        // Paint the selected row with parameter length/playhead info
        for (int step = 0; step < SEQ_STEPS; ++step)
        {
            CRGB targetColor;
            if (step < currentLength)
            {
                if (step == paramPlayhead && activeSeqRef.isRunning())
                {
                    targetColor = getParameterColor(activeParamIdForLength, 180);
                }
                else
                {
                    // Use V1 tint for top row, V2 tint for bottom row
                    targetColor = isSecondInPair ? activeThemeColors->editModeDimBlueV2
                                                 : activeThemeColors->editModeDimBlueV1;
                }
            }
            else
            {
                targetColor = CRGB::Black;
            }
            int ledIndex = ControlSurface::LedLayout::linearIndex(selBand, step);
            nblend(smoothedTargetColorBuffer[ledIndex], targetColor, TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex], isSecondInPair ? 122 : 64);
        }

        return;
    }

    if (anyParamForLengthHeld)
    {
        uint8_t currentLength = activeSeqRef.getParameterStepCount(activeParamIdForLength);
        uint8_t paramPlayhead = activeSeqRef.getCurrentStepForParameter(activeParamIdForLength);

        // Paint only the selected band's within-length area
        const uint8_t selBand = ControlSurface::LedLayout::bandOfVoiceInPair(uiState.selectedVoiceIndex);
        bool isSecondInPair = selBand == 1;
        for (int step = 0; step < currentLength; ++step)
        {
            CRGB targetColor = (step == paramPlayhead && activeSeqRef.isRunning())
                                   ? getParameterColor(activeParamIdForLength, 180)
                                   : (isSecondInPair ? activeThemeColors->editModeDimBlueV2
                                                     : activeThemeColors->editModeDimBlueV1);
            int ledIndex = ControlSurface::LedLayout::linearIndex(selBand, step);
            nblend(smoothedTargetColorBuffer[ledIndex], targetColor, TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex], isSecondInPair ? 200 : 60);
        }

        // Dim the other band's within-length area
        for (int step = 0; step < currentLength; ++step)
        {
            int otherIndex = ControlSurface::LedLayout::linearIndex(static_cast<uint8_t>(1 - selBand), step);
            nblend(smoothedTargetColorBuffer[otherIndex], CRGB::Black, TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[otherIndex], smoothedTargetColorBuffer[otherIndex], 150);
        }
    }
    else
    {
        // Determine which voice pair to display based on selectedVoiceIndex
        bool showFirstPair = (uiState.selectedVoiceIndex < 2);
        const LEDThemeColors *theme = getActiveThemeColors();

        // Clear first to avoid ghosting when switching pages
        for (int i = 0; i < LEDMatrix::WIDTH * LEDMatrix::HEIGHT; ++i)
        {
            nblend(smoothedTargetColorBuffer[i], CRGB::Black, TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[i], smoothedTargetColorBuffer[i], 64);
        }

        // Render either voices 1/2 (page 1) or 3/4 (page 2)
        if (showFirstPair)
        {
            renderVoicePair(ledMatrix, seq1, seq2, theme, 0, 0);
        }
        else
        {
            renderVoicePair(ledMatrix, seq3, seq4, theme, 2, 0);
        }

        // Polyrhythmic overlays for the visible pair only
        if (showFirstPair)
        {
            addPolyrhythmicOverlay(ledMatrix, seq1, 0, 32);
            addPolyrhythmicOverlay(ledMatrix, seq2, 1, 32);
        }
        else
        {
            addPolyrhythmicOverlay(ledMatrix, seq3, 0, 32);
            addPolyrhythmicOverlay(ledMatrix, seq4, 1, 32);
        }

        // Highlight selected step if editing
        if (uiState.selectedStepForEdit >= 0 && uiState.selectedStepForEdit < SEQ_STEPS)
        {
            int ledIndex = ControlSurface::LedLayout::linearIndex(
                ControlSurface::LedLayout::bandOfVoiceInPair(uiState.selectedVoiceIndex),
                static_cast<uint8_t>(uiState.selectedStepForEdit));

            static bool blinkState = false;
            static uint32_t lastBlinkTime = 0;
            uint32_t currentTime = millis();
            if (currentTime - lastBlinkTime > 500)
            {
                blinkState = !blinkState;
                lastBlinkTime = currentTime;
            }

            CRGB highlightColor = blinkState ? CRGB::White : CRGB::Black;
            nblend(smoothedTargetColorBuffer[ledIndex], highlightColor, TARGET_SMOOTHING_BLEND_AMOUNT);
            nblend(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex], 100);
        }
    }
}
