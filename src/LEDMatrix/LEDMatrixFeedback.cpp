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

// LEDMatrixFeedback.cpp — per-frame 8x4 stage render (Core 0).
// Hue = voice, brightness = gate, white bloom = sounding step; settings pages
// reuse the pads. Fades run on wall-clock (frameBlend), envelopes decay per
// voice tempo, and every write goes through the smoothing buffers.

// Steps per band; layout geometry itself lives in ControlSurface::LedLayout.
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
// Smoothing targets: edited first, then the pushed pixels chase them.
CRGB smoothedTargetColorBuffer[LEDConstants::MATRIX_TOTAL_LEDS];

static constexpr uint8_t TARGET_SMOOTHING_BLEND_AMOUNT =
    LEDConstants::TARGET_SMOOTHING_BLEND_AMOUNT;

// ===========================================================================
//   FRAME TIMING
// ===========================================================================
// Every blend amount in this file was tuned against a fixed 40 ms display
// slice, so the fades used to run at whatever rate the renderer happened to be
// called at. frameBlend() reinterprets one of those amounts for the time that
// actually elapsed: a constant keeps its original look at 40 ms and produces
// the same fade in wall-clock terms at any other cadence.
static constexpr float kLegacyFrameMs = 40.0f;
static constexpr uint32_t kMaxFrameDtMs = 250; // after a stall, resume rather than snap
static float frameDeltaMs = kLegacyFrameMs;
static uint32_t lastFrameMs = 0;

struct BlendCacheEntry
{
  uint8_t legacy;
  uint8_t alpha;
};
static constexpr uint8_t kBlendCacheSize = 12;
static BlendCacheEntry blendCache[kBlendCacheSize];
static uint8_t blendCacheCount = 0;

// Perceptual curve for the step envelope. LED output is linear in PWM but the
// eye is not, so a linear fade appears to hang near the bottom of the range.
// Expanding the envelope through gamma makes the tail fall evenly. The theme
// palettes are deliberately left in PWM space - they are hand-levelled there.
static constexpr float kEnvelopeGamma = 2.2f;
static uint8_t envelopeGammaTable[256];

static void beginLEDFrame(uint32_t nowMs)
{
  uint32_t deltaMs =
      (lastFrameMs == 0) ? static_cast<uint32_t>(kLegacyFrameMs) : nowMs - lastFrameMs;
  lastFrameMs = nowMs;
  if (deltaMs == 0)
    deltaMs = 1;
  if (deltaMs > kMaxFrameDtMs)
    deltaMs = kMaxFrameDtMs;
  frameDeltaMs = static_cast<float>(deltaMs);
  blendCacheCount = 0;
}

// Legacy per-frame blend amount -> this frame's equivalent. Cached because the
// same handful of constants repeat across every LED of every band.
static uint8_t frameBlend(uint8_t legacyAmount)
{
  for (uint8_t i = 0; i < blendCacheCount; ++i)
  {
    if (blendCache[i].legacy == legacyAmount)
      return blendCache[i].alpha;
  }

  const float retainPerLegacyFrame = 1.0f - static_cast<float>(legacyAmount) / 256.0f;
  float alpha = 1.0f;
  if (retainPerLegacyFrame > 0.0f)
  {
    alpha = 1.0f - powf(retainPerLegacyFrame, frameDeltaMs / kLegacyFrameMs);
  }
  const uint8_t scaled =
      static_cast<uint8_t>(std::min(255.0f, alpha * 256.0f + 0.5f));
  if (blendCacheCount < kBlendCacheSize)
  {
    blendCache[blendCacheCount++] = {legacyAmount, scaled};
  }
  return scaled;
}

// Per-frame fade toward a target that always arrives.
//
// nblend() computes (target - value) * amount / 256 and truncates, so a step
// smaller than one unit is a step of zero. The blend amounts here were tuned
// for a 40 ms frame; at the 13 ms LED cadence frameBlend() scales them to about
// a third, and the product underflows for exactly the colours that matter most:
// a gate-off pad sits at 1/16 of its hue and the edit-mode blues are darker
// still, so those pixels never moved off black at all. They lit only when the
// playhead swept past and fell straight back - the flicker on the off pads, and
// the dark screen while a parameter button was held.
//
// Moving at least one unit whenever the target differs fixes both, and landing
// exactly on the target stops a settled pixel from hunting when the frame time
// jitters (13 ms, or 18 when the 40 ms OLED tick ran first).
static void blendTo(CRGB &destination, const CRGB &target, uint8_t amount)
{
  const auto step = [amount](uint8_t &value, uint8_t goal) {
    if (value == goal)
      return;
    const int delta = static_cast<int>(goal) - static_cast<int>(value);
    int move = (delta * static_cast<int>(amount)) / 256;
    if (move == 0)
      move = delta > 0 ? 1 : -1;
    value = static_cast<uint8_t>(static_cast<int>(value) + move);
  };
  step(destination.r, target.r);
  step(destination.g, target.g);
  step(destination.b, target.b);
}

// ===========================================================================
//   STEP TRIGGER ENVELOPE
// ===========================================================================
// Each LED carries an energy value in 0..1. A step trigger drives it to a peak
// scaled by that step's velocity, then it fades out on a release scaled to that
// voice's step interval - so the tail reads the same at 60 or 180 BPM, and each
// voice of a polymetric pair keeps its own trail length.
//
// The envelope deliberately does NOT follow the voice's gate. Holding the LED
// for the Gate Length lane read as a glitch: a short gate cut the light before
// the eye caught the step, and the hold was barely visible when it was long.
static constexpr float kEnergyAttackTauMs = 6.0f;    // ~1 frame rise at 13 ms
static constexpr uint32_t kEnergyAttackWindowMs = 20; // rise, then straight to release
static constexpr float kEnergyReleaseFactor = 0.35f; // of the voice's step interval
static constexpr float kEnergyReleaseMinMs = 18.0f;
static constexpr float kEnergyReleaseMaxMs = 180.0f;
static constexpr float kEnergyIdleReleaseMs = 90.0f; // interval not known yet
static constexpr float kEnergyEpsilon = 0.002f;
// A trigger lands at part of its peak in the same frame, so a gate shorter than
// one display frame still shows. The attack takes it the rest of the way.
static constexpr float kTriggerSeedFraction = 0.55f;
// Gate-off steps get no envelope at all. Giving them a dim peak lit every
// gate-off pad as the playhead swept past, which read as the whole off-row
// flickering. The playhead is carried by the gated steps.
static constexpr float kMutedStepPeak = 0.0f;
// Softest velocity that still reads as a hit.
static constexpr float kVelocityPeakFloor = 0.45f;
// White bloom at full energy on a gated step: a hot core, not a brighter tint.
static constexpr uint8_t kBloomAmount = 48;

struct BandEnvelope
{
  uint8_t lastStep = 0xFF;
  uint32_t lastTriggerMs = 0;
  float stepIntervalMs = 0.0f;
  float peak = 1.0f;
};

static float stepEnergy[LEDConstants::MATRIX_TOTAL_LEDS];
static BandEnvelope bandEnvelopes[ControlSurface::LedLayout::kBandCount];
static uint8_t energyPairFirstVoice = 0xFF;

static float clampUnit(float value)
{
  return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

// Peak energy for a fresh trigger, scaled by step velocity.
static float peakForStep(const Sequencer &sequencer, uint8_t voiceIndex,
                         uint8_t step)
{
  if (!sequencer.getStep(step).isGateActive)
  {
    return kMutedStepPeak;
  }
  float velocity = sequencer.getStepParameterValue(ParamId::Velocity, step);
  if (velocity < 0.0f)
  {
    // LANE_FOLLOWS_PATCH: the step plays at the voice's own level.
    velocity = voiceSystem.getVoiceState(voiceIndex).velocityLevel;
  }
  velocity = clampUnit(velocity);
  return kVelocityPeakFloor + (1.0f - kVelocityPeakFloor) * velocity;
}

// Advances every visible LED's envelope by one frame. Runs in every UI mode so
// that a mode the renderer does not draw still fades out instead of freezing.
static void advanceStepEnergy(const SequencerView &sequencers,
                              const UIState &uiState)
{
  const uint8_t firstVoice = (uiState.selectedVoiceIndex < 2) ? 0 : 2;
  if (firstVoice != energyPairFirstVoice)
  {
    // Page switch: these LEDs now belong to a different voice pair.
    energyPairFirstVoice = firstVoice;
    for (int i = 0; i < LEDConstants::MATRIX_TOTAL_LEDS; ++i)
      stepEnergy[i] = 0.0f;
    for (auto &band : bandEnvelopes)
      band = BandEnvelope{};
  }

  const uint32_t nowMs = millis();
  const float attackAlpha = 1.0f - expf(-frameDeltaMs / kEnergyAttackTauMs);

  for (uint8_t band = 0; band < ControlSurface::LedLayout::kBandCount; ++band)
  {
    const uint8_t voiceIndex = static_cast<uint8_t>(firstVoice + band);
    const Sequencer &sequencer = sequencers.clamped(voiceIndex);
    BandEnvelope &envelope = bandEnvelopes[band];

    float releaseTauMs = kEnergyIdleReleaseMs;
    if (envelope.stepIntervalMs > 0.0f)
    {
      releaseTauMs = std::min(
          kEnergyReleaseMaxMs,
          std::max(kEnergyReleaseMinMs,
                   envelope.stepIntervalMs * kEnergyReleaseFactor));
    }
    const float retain = expf(-frameDeltaMs / releaseTauMs);

    // Release first, so the step the playhead just left starts fading at once.
    for (uint8_t step = 0; step < ControlSurface::LedLayout::kStepsPerBand;
         ++step)
    {
      const int ledIndex = ControlSurface::LedLayout::linearIndex(band, step);
      if (ledIndex < 0)
        continue;
      stepEnergy[ledIndex] *= retain;
      if (stepEnergy[ledIndex] < kEnergyEpsilon)
        stepEnergy[ledIndex] = 0.0f;
    }

    if (!sequencer.isRunning())
    {
      envelope.lastStep = 0xFF;
      continue;
    }

    const uint8_t step = sequencer.getCurrentStepForParameter(ParamId::Gate);
    const int ledIndex = ControlSurface::LedLayout::linearIndex(band, step);
    if (ledIndex < 0)
      continue;

    if (step != envelope.lastStep)
    {
      // The interval is measured step to step, gated or not, so the release
      // stays tied to the clock rather than to how sparse the gates are.
      if (envelope.lastStep != 0xFF && envelope.lastTriggerMs != 0)
      {
        const float interval =
            static_cast<float>(nowMs - envelope.lastTriggerMs);
        envelope.stepIntervalMs = (envelope.stepIntervalMs > 0.0f)
                                      ? 0.5f * (envelope.stepIntervalMs + interval)
                                      : interval;
      }
      envelope.lastTriggerMs = nowMs;
      envelope.lastStep = step;
      envelope.peak = peakForStep(sequencer, voiceIndex, step);
      // A gate-off step has no peak, so it lights nothing and the previous
      // step's tail keeps fading undisturbed.
      if (envelope.peak > 0.0f)
        stepEnergy[ledIndex] =
            std::max(stepEnergy[ledIndex], envelope.peak * kTriggerSeedFraction);
    }

    // Fast rise to the peak, then the release above takes over. Nothing holds
    // the LED up, so every gated step reads as the same shape.
    if (envelope.peak > 0.0f && nowMs - envelope.lastTriggerMs <= kEnergyAttackWindowMs)
    {
      stepEnergy[ledIndex] +=
          (envelope.peak - stepEnergy[ledIndex]) * attackAlpha;
    }
  }
}

// Add the step's envelope glow onto its gate color.
static void applyStepGlow(CRGB &color, const LEDThemeColors &themeColors,
                          float energy, bool gateActive)
{
  if (energy <= 0.0f)
    return;
  const uint8_t punch =
      envelopeGammaTable[static_cast<uint8_t>(clampUnit(energy) * 255.0f + 0.5f)];
  if (punch == 0)
    return;

  CRGB glow = themeColors.playheadAccent;
  glow.nscale8_video(punch);
  color += glow;
  if (gateActive)
  {
    nblend(color, CRGB::White, scale8(punch, kBloomAmount));
  }
}

// Live palette mirrors; refreshed from the active theme each frame.
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
     CRGB(0, 8, 8),
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
        CRGB(62, 44, 4),     // playheadAccent - dark lava accent
        CRGB(50, 20, 8),     // idleBreathingBlue - warm ember glow
        CRGB(5, 6, 12),      // editModeDimBlueV1 - very dark warm slate
        CRGB(12, 12, 5),      // editModeDimBlueV2
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
  if (!sequencer.isRunning()) {
    return;
  }

  // Which lanes get a playhead tint when their cycle differs from Gate.
  struct PolyrhythmicParameterOverlay {
    ParamId parameterID;
    CRGB overlayColor;
  };

  const PolyrhythmicParameterOverlay
      overlayParameters[LEDConstants::POLYRHYTHM_PARAM_COUNT] = {
          {ParamId::Note, LEDColors::POLYRHYTHM_NOTE},
          {ParamId::Velocity, LEDColors::POLYRHYTHM_VELOCITY},
          {ParamId::Filter, LEDColors::POLYRHYTHM_FILTER}};

  for (size_t paramIndex = 0; paramIndex < LEDConstants::POLYRHYTHM_PARAM_COUNT;
       ++paramIndex) {
    const ParamId currentParameter = overlayParameters[paramIndex].parameterID;
    const uint8_t currentParameterStep =
        sequencer.getCurrentStepForParameter(currentParameter);
    const uint8_t parameterStepCount =
        sequencer.getParameterStepCount(currentParameter);

    if (currentParameterStep < LEDConstants::MAX_STEP_BUTTONS &&
        parameterStepCount > 1 &&
        parameterStepCount <= LEDConstants::MAX_STEP_BUTTONS) {

      const int ledLinearIndex =
          ControlSurface::LedLayout::linearIndex(band, currentParameterStep);
      if (ledLinearIndex < 0) {
        continue;
      }
      CRGB currentLEDColor = ledMatrix.getLeds()[ledLinearIndex];

      // Tint, don't replace: the gate hue underneath must survive.
      currentLEDColor += overlayParameters[paramIndex].overlayColor;

      ledMatrix.setLED(ControlSurface::LedLayout::x(currentParameterStep),
                       ControlSurface::LedLayout::y(band, currentParameterStep),
                       currentLEDColor);
    }
  }
}

// easeInOutQuad for the idle breathing wash (eye-linear, not PWM-linear).
float ease(float x) { return x < 0.5 ? 2 * x * x : 1 - pow(-2 * x + 2, 2) / 2; }

float smoothBreathing(uint32_t timeMs) {
  const float normalizedTime =
      static_cast<float>(timeMs % LEDConstants::BREATHING_CYCLE_MS) /
      static_cast<float>(LEDConstants::BREATHING_CYCLE_MS);
  return ease(0.5f * (1.0f + sin(2.0f * PI * normalizedTime)));
}

void setStepLedColor(uint8_t stepIndex, uint8_t redValue, uint8_t greenValue,
                     uint8_t blueValue) {
  // Legacy no-op (needs a matrix ref); prefer updateStepLEDs.
}

void setupLEDMatrixFeedback() {
  for (int ledIndex = 0; ledIndex < LEDConstants::MATRIX_TOTAL_LEDS;
       ++ledIndex) {
    smoothedTargetColorBuffer[ledIndex] = LEDColors::BLACK;
    stepEnergy[ledIndex] = 0.0f;
  }
  for (auto &band : bandEnvelopes) {
    band = BandEnvelope{};
  }
  energyPairFirstVoice = 0xFF;
  lastFrameMs = 0;

  // Envelope gamma table. Zero stays off so a fade reaches true black; every
  // other level keeps at least 1/255 so the tail does not cut out early.
  envelopeGammaTable[0] = 0;
  for (int i = 1; i < 256; ++i) {
    const float normalized = static_cast<float>(i) / 255.0f;
    const int value =
        static_cast<int>(powf(normalized, kEnvelopeGamma) * 255.0f + 0.5f);
    envelopeGammaTable[i] = static_cast<uint8_t>(value < 1 ? 1 : value);
  }
}

// Settings page: preset pads in the voice hue (pulse = current), or the
// settings-pad values as brightness.
void updateSettingsModeLEDs(LEDMatrix &ledMatrix, const UIState &uiState) {
  const LEDThemeColors *activeThemeColors = getActiveThemeColors();

  for (int i = 0; i < LEDMatrix::WIDTH * LEDMatrix::HEIGHT; ++i) {
    ledMatrix.getLeds()[i] = CRGB::Black;
  }

  if (uiState.isPresetSelection()) {
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
      if (presetIndex == currentPresetIndex) {
        // Current preset breathes; the rest sit at gate-off level.
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

// One voice pair (1/2 or 3/4) into the two matrix bands: gate hue per step,
// slide tint, tempo-scaled comet glow, then the smoothing buffers.
static void renderVoicePair(LEDMatrix &ledMatrix,
                            const Sequencer &firstVoiceSequencer,
                            const Sequencer &secondVoiceSequencer,
                            const LEDThemeColors *themeColors,
                            uint8_t firstVoiceIndex, uint8_t band) {
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

  for (int stepIndex = 0; stepIndex < LEDConstants::MAX_STEP_BUTTONS;
       ++stepIndex) {
    // Upper band of the pair.
    const Step &firstVoiceStep = firstVoiceSequencer.getStep(stepIndex);
    const int topRowLEDIndex =
        ControlSurface::LedLayout::linearIndex(band, stepIndex);
    if (topRowLEDIndex < 0) {
      continue;
    }

    // Gate hue first; slide/glow/smoothing layer on top.
    CRGB firstVoiceColor = getVoiceGateColor(*themeColors, firstVoiceIndex,
                                             firstVoiceStep.isGateActive);

    // Slide tint: a sounding glide reads before the note moves.
    if (firstVoiceSequencer.getStepParameterValue(ParamId::Slide, stepIndex) >
        0) {
      nblend(firstVoiceColor, themeColors->modSlideActive,
             LEDConstants::MEDIUM_BRIGHTNESS);
    }

    // Trigger envelope: attack, gate hold, tempo-scaled fade out. The steps
    // behind the playhead are still fading, which is the comet trail.
    applyStepGlow(firstVoiceColor, *themeColors, stepEnergy[topRowLEDIndex],
                  firstVoiceStep.isGateActive);

    // Two-stage settle: targets ease, then pushed pixels chase the targets.
    blendTo(smoothedTargetColorBuffer[topRowLEDIndex], firstVoiceColor,
           frameBlend(TARGET_SMOOTHING_BLEND_AMOUNT));
    blendTo(ledMatrix.getLeds()[topRowLEDIndex],
           smoothedTargetColorBuffer[topRowLEDIndex],
           frameBlend(LEDConstants::STANDARD_BLEND_AMOUNT));

    // Lower band of the pair.
    const Step &secondVoiceStep = secondVoiceSequencer.getStep(stepIndex);
    const int bottomRowLEDIndex = ControlSurface::LedLayout::linearIndex(
        static_cast<uint8_t>(band + 1), stepIndex);
    if (bottomRowLEDIndex < 0) {
      continue;
    }

    // Gate hue first; slide/glow/smoothing layer on top.
    CRGB secondVoiceColor = getVoiceGateColor(
        *themeColors, static_cast<uint8_t>(firstVoiceIndex + 1),
        secondVoiceStep.isGateActive);

    // Slide tint: a sounding glide reads before the note moves.
    if (secondVoiceSequencer.getStepParameterValue(ParamId::Slide, stepIndex) >
        0) {
      nblend(secondVoiceColor, themeColors->modSlideActive,
             LEDConstants::MEDIUM_BRIGHTNESS);
    }

    applyStepGlow(secondVoiceColor, *themeColors,
                  stepEnergy[bottomRowLEDIndex], secondVoiceStep.isGateActive);

    // Two-stage settle: targets ease, then pushed pixels chase the targets.
    blendTo(smoothedTargetColorBuffer[bottomRowLEDIndex], secondVoiceColor,
           frameBlend(TARGET_SMOOTHING_BLEND_AMOUNT));
    blendTo(ledMatrix.getLeds()[bottomRowLEDIndex],
           smoothedTargetColorBuffer[bottomRowLEDIndex],
           frameBlend(LEDConstants::STANDARD_BLEND_AMOUNT));
  }
}

/**
 * @brief Paint the 8x4 panel as the arp's 32-degree chord map.
 *
 * Arpeggiator mode reuses the panel the touch pads mirror: each LED is one
 * physical scale position. Seven-note scales lay out one octave per row, with
 * the first/last columns and row-boundary pads repeating the octave root. The
 * grid therefore shows the chord, where the scale's octaves fall and which
 * physical positions are sounding right now. Colour carries the state:
 *   - a finger on the pad: the arp voice's gate-on hue,
 *   - latched with the finger off: the same hue at gate-off brightness,
 *   - sounding: hue pushed toward the theme's playhead accent, brighter the
 *     higher the octave of range and the further the hand is from the sensor,
 *   - free: a quiet shade of the selected voice hue, with roots at a brighter
 *     shade so the ladder is navigable in the dark,
 *   - roots, chord tones, and other scale degrees use three distinct shades
 *     from the same theme palette.
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
  const uint8_t notesPerOctave = scaleNotesPerOctave(row);
  const uint32_t now = millis();
  const float breathing = smoothBreathing(now);
  // Lidar dynamics: the same hand that sets note velocity brightens the note
  // that is sounding, so the panel shows the gesture that is being heard.
  const uint8_t dynamicScale =
      static_cast<uint8_t>(LEDConstants::MEDIUM_BRIGHTNESS +
                           (arp.lastVelocityScale() * (LEDConstants::FULL_BRIGHTNESS -
                                              LEDConstants::MEDIUM_BRIGHTNESS)));

  for (uint8_t pad = 0; pad < Arpeggiator::kPadCount; ++pad) {
    const int ledIndex = Arpeggiator::ledIndexForPad(pad);
    if (ledIndex < 0) continue;

    CRGB target = CRGB::Black;
    if (arp.padInChord(pad)) {
      // Held or latched: the voice's hue, bright under a finger.
      target = getVoiceGateColor(*theme, voice, arp.padHeld(pad));
    } else if ((notesPerOctave == Arpeggiator::kSevenNoteScale &&
                Arpeggiator::scaleDegreeForPad(pad, notesPerOctave) %
                        Arpeggiator::kSevenNoteScale == 0) ||
               (notesPerOctave != Arpeggiator::kSevenNoteScale &&
                row[pad] % 12 == 0)) {
      // Root, chord, and free-note roles use three distinct brightnesses of
      // the same selected-voice hue: root half, chord full, other one-eighth.
      // Seven-note layouts also mark the repeated root at each row boundary,
      // making the octave grid legible.
      const uint8_t clampedVoice = voice < LED_THEME_VOICE_COUNT ? voice : 0;
      target = scaleGateHue(theme->gateOn[clampedVoice], 1, 2);
    } else {
      // Non-root scale degrees use a quiet shade from the same voice hue, so
      // the free scale ladder remains visible without competing with a chord.
      const uint8_t clampedVoice = voice < LED_THEME_VOICE_COUNT ? voice : 0;
      target = scaleGateHue(theme->gateOn[clampedVoice], 1, 4);
    }

    if (isClockRunning && arp.padSounding(pad)) {
      // Sounding now: push the hue toward the playhead accent, harder for the
      // higher octaves of the range so a climbing arp reads as a climb.
      uint8_t octave = 0;
      for (uint8_t slot = 0; slot < Arpeggiator::kMaxSlots; ++slot) {
        uint8_t degree = 0;
        uint8_t slotOctave = 0;
        if (arp.slotSounding(slot, degree, slotOctave) &&
            degree == Arpeggiator::scaleDegreeForPad(pad, notesPerOctave) &&
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
  // Everything below fades on elapsed time, not on frame count.
  beginLEDFrame(millis());
  // Runs in every mode: a view that does not draw the envelope still has to
  // let it fade, or the energy would be frozen when the view comes back.
  advanceStepEnergy(sequencers, uiState);

  // If requested, immediately clear smoothed buffers to force a visual refresh
  if (uiState.resetStepsLightsFlag) {
    for (int i = 0; i < LEDConstants::MATRIX_TOTAL_LEDS; ++i) {
      smoothedTargetColorBuffer[i] = CRGB::Black;
      stepEnergy[i] = 0.0f;
    }
    // One-shot consumption of the flag. UIState is passed as const to
    // renderers, so we clear it here intentionally to prevent continuous
    // clearing every frame.
    const_cast<UIState &>(uiState).resetStepsLightsFlag = false;
  }

  if (uiState.settingsMode) {
    updateSettingsModeLEDs(ledMatrix, uiState);
    return;
  }

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

  // Length edit: blink the selected voice's bar up to its gate length.
  if (uiState.gateSeqLengthMode) {
    const uint8_t selBand = ControlSurface::LedLayout::bandOfVoiceInPair(
        uiState.selectedVoiceIndex);
    const CRGB withinColorBase = getVoiceGateColor(
        *getActiveThemeColors(), uiState.selectedVoiceIndex, true);

    static bool blinkState = false;
    static uint32_t lastBlinkMs = 0;
    const uint32_t now = millis();
    if (now - lastBlinkMs > 250) { // ~4 Hz
      blinkState = !blinkState;
      lastBlinkMs = now;
    }

    const uint8_t gateLen = activeSeq.getParameterStepCount(ParamId::Gate);

    // Other pair goes dark so the length bar reads alone.
    for (int step = 0; step < LEDConstants::MAX_STEP_BUTTONS; ++step) {
      const int otherIndex = ControlSurface::LedLayout::linearIndex(
          static_cast<uint8_t>(1 - selBand), step);
      blendTo(smoothedTargetColorBuffer[otherIndex], CRGB::Black,
             frameBlend(LEDConstants::TARGET_SMOOTHING_BLEND_AMOUNT));
      blendTo(ledMatrix.getLeds()[otherIndex],
             smoothedTargetColorBuffer[otherIndex],
             frameBlend(LEDConstants::DIM_BLEND_AMOUNT));
    }

    for (int step = 0; step < LEDConstants::MAX_STEP_BUTTONS; ++step) {
      CRGB target = CRGB::Black;
      if (step < gateLen && gateLen > 1) {
        target = withinColorBase;
        if (blinkState) {
          target.nscale8(60);
        }
      }
      const int ledIndex =
          ControlSurface::LedLayout::linearIndex(selBand, step);
      blendTo(smoothedTargetColorBuffer[ledIndex], target,
             frameBlend(LEDConstants::TARGET_SMOOTHING_BLEND_AMOUNT));
      blendTo(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex],
             frameBlend(LEDConstants::STANDARD_BLEND_AMOUNT));
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

    // Held lane: other pair dark, this pair shows length + playhead.
    const uint8_t selBand = ControlSurface::LedLayout::bandOfVoiceInPair(
        uiState.selectedVoiceIndex);
    bool isSecondInPair = selBand == 1;
    for (int step = 0; step < SEQ_STEPS; ++step) {
      int topIndex = ControlSurface::LedLayout::linearIndex(0, step);
      int bottomIndex = ControlSurface::LedLayout::linearIndex(1, step);
      if (!isSecondInPair) {
        blendTo(smoothedTargetColorBuffer[bottomIndex], CRGB::Black,
               frameBlend(TARGET_SMOOTHING_BLEND_AMOUNT));
        blendTo(ledMatrix.getLeds()[bottomIndex],
               smoothedTargetColorBuffer[bottomIndex], frameBlend(32));
      } else {
        blendTo(smoothedTargetColorBuffer[topIndex], CRGB::Black,
               frameBlend(TARGET_SMOOTHING_BLEND_AMOUNT));
        blendTo(ledMatrix.getLeds()[topIndex],
               smoothedTargetColorBuffer[topIndex], frameBlend(32));
      }
    }

    for (int step = 0; step < SEQ_STEPS; ++step) {
      CRGB targetColor;
      if (step < currentLength) {
        if (step == paramPlayhead && activeSeq.isRunning()) {
          targetColor = getParameterColor(activeParamIdForLength, 180);
        } else {
          // Edit tint follows the row (V1 top, V2 bottom).
          targetColor = isSecondInPair ? activeThemeColors->editModeDimBlueV2
                                       : activeThemeColors->editModeDimBlueV1;
        }
      } else {
        targetColor = CRGB::Black;
      }
      int ledIndex = ControlSurface::LedLayout::linearIndex(selBand, step);
      blendTo(smoothedTargetColorBuffer[ledIndex], targetColor,
             frameBlend(TARGET_SMOOTHING_BLEND_AMOUNT));
      blendTo(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex],
             frameBlend(isSecondInPair ? 122 : 64));
    }

    return;
  }

  if (anyParamForLengthHeld) {
    uint8_t currentLength =
        activeSeq.getParameterStepCount(activeParamIdForLength);
    uint8_t paramPlayhead =
        activeSeq.getCurrentStepForParameter(activeParamIdForLength);

    // Latched-lane length view: same bar, only the held band paints.
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
      blendTo(smoothedTargetColorBuffer[ledIndex], targetColor,
             frameBlend(TARGET_SMOOTHING_BLEND_AMOUNT));
      blendTo(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex],
             frameBlend(isSecondInPair ? 200 : 60));
    }

    for (int step = 0; step < currentLength; ++step) {
      int otherIndex = ControlSurface::LedLayout::linearIndex(
          static_cast<uint8_t>(1 - selBand), step);
      blendTo(smoothedTargetColorBuffer[otherIndex], CRGB::Black,
             frameBlend(TARGET_SMOOTHING_BLEND_AMOUNT));
      blendTo(ledMatrix.getLeds()[otherIndex],
             smoothedTargetColorBuffer[otherIndex], frameBlend(150));
    }
  } else {
    // Show the selected pair (1/2 or 3/4); the other page stays cached.
    const uint8_t firstVoice = (uiState.selectedVoiceIndex < 2) ? 0 : 2;
    const Sequencer &firstSeq = sequencers.clamped(firstVoice);
    const Sequencer &secondSeq = sequencers.clamped(firstVoice + 1);
    const LEDThemeColors *theme = getActiveThemeColors();

    // No pre-clear pass here. renderVoicePair() writes every one of the 32 LEDs
    // from the visible pair's own state, so clearing first only meant each LED
    // was pulled toward black and then toward its target in the same frame.
    // That two-stage pull settles at a fraction of the target which depends on
    // the alpha, so it moved with every wobble in frame time - the flicker.
    // A page switch is covered by the new targets and by the energy reset.

    renderVoicePair(ledMatrix, firstSeq, secondSeq, theme, firstVoice, 0);

    // Lane playheads that differ from Gate get their tint (visible pair only).
    addPolyrhythmicOverlay(ledMatrix, firstSeq, 0, 32);
    addPolyrhythmicOverlay(ledMatrix, secondSeq, 1, 32);

    // Step-edit cursor blinks over the pair view.
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
      blendTo(smoothedTargetColorBuffer[ledIndex], highlightColor,
             frameBlend(TARGET_SMOOTHING_BLEND_AMOUNT));
      blendTo(ledMatrix.getLeds()[ledIndex], smoothedTargetColorBuffer[ledIndex],
             frameBlend(100));
    }
  }
}
