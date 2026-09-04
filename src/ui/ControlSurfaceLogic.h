#ifndef CONTROL_SURFACE_LOGIC_H
#define CONTROL_SURFACE_LOGIC_H

#include <cstdint>

#include "../pico2seq-core/sequencer/SequencerDefs.h"

// ControlSurfaceLogic — the decisions of the Alchemy tile control surface,
// as pure C++ with no Arduino dependency so the host test suite can drive
// them directly (tests/unit/test_control_surface_logic.cpp).
//
// The glue that talks to tiles/GP7/uClock lives in AlchemyControlBridge; the
// four small policies here own everything worth unit-testing:
//
//   ModeStabilizer — raw GP7 readings in, stable Mode out (20 ms), + edge.
//   PadBank        — pad index + selected voice -> (voice, step).
//   ShiftLatch     — shift level + param edges -> parameterButtonHeld state
//                    with Shift+tap latching.
//   FaderMap       — mode + fader channel -> control target, with a send
//                    deadband so steady faders stay quiet.

namespace ControlSurface
{

// ---------------------------------------------------------------------------
// Mode system
// ---------------------------------------------------------------------------

enum class Mode : uint8_t
{
  Param,   // GP7 reads kModeParamLevel: tiles carry the parameter button set
  Utility, // GP7 reads the other level: tiles carry transport/utility functions
};

// GP7 level that selects Param mode. LOW = Param per the design; flip this
// constant if the wired switch polarity turns out inverted (bench item).
inline constexpr bool kModeParamLevel = false;

// A raw GP7 level must hold stable this long before the mode flips.
inline constexpr uint32_t kModeStabilityMs = 20;

/**
 * @brief Software debounce + edge detection for the GP7 mode strap.
 *
 * Feed every control-loop pass's raw reading; query mode() / tookChange().
 * A flip only happens after the raw level has continuously agreed with the
 * candidate mode for kModeStabilityMs; bouncing restarts the wait.
 */
class ModeStabilizer
{
public:
  /** Force the starting mode without an edge (call once at begin()). */
  void begin(Mode initialMode, uint32_t nowMs);

  /**
   * Feed one raw GP7 sample. @param rawHigh true when the pin reads HIGH.
   * @return the current (possibly just-flipped) stable mode.
   */
  Mode update(bool rawHigh, uint32_t nowMs);

  /** True once after update() flipped the mode; cleared by clearChange(). */
  bool tookChange() const { return pendingEdge_; }
  void clearChange() { pendingEdge_ = false; }
  Mode mode() const { return mode_; }

private:
  Mode mode_ = Mode::Param;
  Mode candidate_ = Mode::Param;
  bool candidateValid_ = false;
  bool pendingEdge_ = false;
  uint32_t candidateSinceMs_ = 0;
};

// ---------------------------------------------------------------------------
// Pad bank resolution
// ---------------------------------------------------------------------------

/** A resolved pad: sequencer voice index (0..3) and step within that voice. */
struct PadAddress
{
  uint8_t voice;
  uint8_t step;
};

/** The two voices served by the low and high 16-pad banks. */
struct PadPair
{
  uint8_t lowVoice;
  uint8_t highVoice;
};

class PadBank
{
public:
  static constexpr uint8_t kPadCount = 32;
  static constexpr uint8_t kStepsPerVoice = 16;

  /**
   * Voice pair behind the pad banks for a selected voice (0..3). The selected
   * voice always stays on its own bank; the pair partner takes the other:
   * voice 0/1 selected -> banks are voice 1+2, voice 2/3 selected -> 3+4
   * (0-based indices, as everywhere in this codebase).
   */
  static PadPair pairFor(uint8_t selectedVoice);

  /** Resolve a raw pad index (0..31, clamped) to its voice and step. */
  static PadAddress resolve(uint8_t padIndex, uint8_t selectedVoice);
};

// ---------------------------------------------------------------------------
// LED layout (pad-mirror geometry)
// ---------------------------------------------------------------------------

/**
 * @brief Linear LED-index geometry for the 8x4 WS2812B panel.
 *
 * The LED panel mirrors the 4x8 MPR121 touch matrix: band b (0 = the voice
 * pair's low voice, 1 = its high voice) occupies LED linear indices
 * b*16..b*16+15 — exactly the pad indices of touch rows 2b..2b+1. All step
 * rendering in src/LEDMatrix/ must go through this helper so the two
 * surfaces cannot drift apart. The constexprs intentionally duplicate
 * LEDConstants.h, which cannot be included here (it pulls in FastLED);
 * LEDMatrixFeedback.cpp static_asserts the agreement.
 */
class LedLayout
{
public:
  static constexpr uint8_t kWidth = 8;                             // LEDs/pads per row
  static constexpr uint8_t kRowsPerBand = 2;                       // rows per voice band
  static constexpr uint8_t kStepsPerBand = kWidth * kRowsPerBand;  // 16 steps per voice
  static constexpr uint8_t kBandCount = 2;                         // voices visible at once
  static constexpr uint8_t kLedCount = kStepsPerBand * kBandCount; // 32 LEDs

  /** Band (0/1) of a selected voice (0..3, clamped like PadBank::pairFor). */
  static constexpr uint8_t bandOfVoiceInPair(uint8_t selectedVoiceIndex)
  {
    if (selectedVoiceIndex >= 4)
    {
      selectedVoiceIndex = 0;
    }
    return static_cast<uint8_t>(selectedVoiceIndex & 1u);
  }

  /** Linear LED index for (band, step); -1 when out of range. */
  static constexpr int linearIndex(uint8_t band, uint8_t step)
  {
    if (band >= kBandCount || step >= kStepsPerBand)
    {
      return -1;
    }
    return static_cast<int>(band * kStepsPerBand + step);
  }

  /** X column for a step; -1 when out of range. */
  static constexpr int x(uint8_t step)
  {
    return (step < kStepsPerBand) ? static_cast<int>(step % kWidth) : -1;
  }

  /** Y row for (band, step); -1 when out of range. */
  static constexpr int y(uint8_t band, uint8_t step)
  {
    if (band >= kBandCount || step >= kStepsPerBand)
    {
      return -1;
    }
    return static_cast<int>(band * kRowsPerBand + step / kWidth);
  }
};

// ---------------------------------------------------------------------------
// Shift latch semantics
// ---------------------------------------------------------------------------

/**
 * @brief Shift-keyed parameter hold semantics for the tile parameter buttons.
 *
 * Owns the momentary hold state and the single Shift latch, and derives the
 * final parameterButtonHeld[] array from them:
 *
 *   - ordinary press/release sets/clears a momentary hold;
 *   - Shift + param press toggles the latch (pressing a different param
 *     moves the single latch; pressing the latched param clears it);
 *   - a latched param reads as held with no finger on the button, while
 *     physically pressed params stay momentary holds alongside it.
 *
 * Pure policy: the bridge feeds edges and copies the derived array into
 * UIState::parameterButtonHeld. reset() is used on mode flips so nothing
 * sticks across a mode change.
 */
class ShiftLatch
{
public:
  static constexpr uint8_t kParamCount = static_cast<uint8_t>(PARAM_ID_COUNT);
  static constexpr int8_t kNoLatch = -1;

  void reset();

  /**
   * Feed one parameter button edge.
   * @param paramId    ParamId as uint8_t (0..PARAM_ID_COUNT-1)
   * @param pressed    true on press edge, false on release edge
   * @param shiftHeld  Shift button level at the moment of this edge
   */
  void onParamButton(uint8_t paramId, bool pressed, bool shiftHeld);

  /**
   * Write the derived held states. Every entry is rewritten (cleared or set),
   * so the output array never keeps stale holds from other code paths.
   */
  void applyTo(bool *heldOut, uint8_t count) const;

  int8_t latched() const { return latched_; }
  bool isMomentary(uint8_t paramId) const
  {
    return paramId < kParamCount && momentary_[paramId];
  }

private:
  bool momentary_[kParamCount] = {false};
  int8_t latched_ = kNoLatch;
};

// ---------------------------------------------------------------------------
// Fader map
// ---------------------------------------------------------------------------

/** What a fader channel controls in the current mode. */
enum class FaderTarget : uint8_t
{
  StepParam,   // records a ParamId into steps (param mode)
  Tempo,       // uClock BPM (utility mode)
  SwingAmount, // continuous shuffle depth (utility mode)
  DelayMix,    // delay feedback amount (utility mode)
  GateLength,  // gate length across the selected voice's steps (utility mode)
};

struct FaderAssignment
{
  FaderTarget target = FaderTarget::StepParam;
  ParamId paramId = ParamId::Count; // valid when target == StepParam
};

class FaderMap
{
public:
  static constexpr uint8_t kChannelCount = 4;
  static constexpr uint16_t kFaderMaxCounts = 4095;
  // Movement smaller than this (in 12-bit counts) is not sent.
  static constexpr uint16_t kDeadbandCounts = 8;

  /** Target of one fader channel (0..3) in the given mode. */
  static FaderAssignment assignmentFor(Mode mode, uint8_t channel);

  /** 12-bit raw fader counts -> normalized 0..1. */
  static float normalize(uint16_t rawCounts);

  /**
   * Deadband filter: true when this channel's value should be sent (first
   * sample after a reset always sends, so controls snap to fader positions).
   */
  bool accept(uint8_t channel, uint16_t rawCounts);

  /** Forget the last-sent values (mode flip): the next sample re-sends. */
  void resetDeadband();

private:
  uint16_t lastSent_[kChannelCount] = {0, 0, 0, 0};
  bool valid_[kChannelCount] = {false, false, false, false};
};

} // namespace ControlSurface

#endif // CONTROL_SURFACE_LOGIC_H
