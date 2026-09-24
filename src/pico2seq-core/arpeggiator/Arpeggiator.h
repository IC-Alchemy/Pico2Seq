#ifndef ARPEGGIATOR_H
#define ARPEGGIATOR_H

#include <stdint.h>

// Arpeggiator — the chord-driven note engine behind Arpeggiator mode.
//
// Plain portable C++ on purpose: no Arduino, no UI or voice types, no
// allocation, so the host suite drives it tick by tick
// (tests/unit/test_arpeggiator.cpp). It never touches a voice either: tick()
// reports note starts and stops by slot, and src/app/ArpPlayback.cpp maps slots
// onto voices and voices onto VoiceState.
//
// Pad indices are physical positions on the 8x4 touch panel. For a seven-note
// scale, each row is one octave: columns 0..6 are the seven scale degrees and
// column 7 repeats the next octave's root; the first pad of the next row is the
// same note as the last pad of the previous row. Other scales retain the
// original linear 32-degree ladder. Arp range climbs in whole octaves through
// VoiceState::octaveOffset (+12 semitones per step of range), not through a
// scale-table stride, so every scale gets true octaves.

//
// Timing is in uClock's 480 PPQN ticks: the caller feeds one tick per output
// pulse, the engine decides when the next note is due. Swing delays every
// second interval and shortens its partner to the same total, so pairs stay
// even and no drift accumulates against the transport.

namespace Arpeggiator
{

// --- Geometry and limits -----------------------------------------------------

inline constexpr uint8_t kPadCount = 32;    // 8x4 touch pads = 32 physical positions
inline constexpr uint8_t kPadColumns = 8;   // one panel row
inline constexpr uint8_t kMaxSlots = 4;     // one voice per simultaneous note
inline constexpr uint8_t kSevenNoteScale = 7;
inline constexpr uint8_t kNoDegree = 0xFF;

/**
 * Map a physical pad to the scale degree it represents for a scale with
 * `notesPerOctave` notes.  Seven-note scales use the full eight-column panel
 * as an octave: columns 0..6 are the seven notes and column 7 is the next
 * root.  The first pad of the following row therefore deliberately repeats
 * the last pad of the preceding row. Other scales retain the original linear
 * 32-degree ladder until they have a layout rule of their own.
 */
constexpr uint8_t scaleDegreeForPad(uint8_t pad, uint8_t notesPerOctave) noexcept
{
  if (pad >= kPadCount)
    return kNoDegree;
  if (notesPerOctave != kSevenNoteScale)
    return pad;
  const uint8_t row = pad / kPadColumns;
  const uint8_t column = pad % kPadColumns;
  return static_cast<uint8_t>(row * kSevenNoteScale +
                             (column < kSevenNoteScale ? column : kSevenNoteScale));
}
inline constexpr uint8_t kMinOctaves = 1;
inline constexpr uint8_t kMaxOctaves = 4;
inline constexpr uint8_t kMaxRhythmSteps = 16;
inline constexpr uint8_t kRhythmPresetCount = 6;
// Note length as a fraction of the interval. The ceiling leaves a gap before
// the next note so a mono pattern always retriggers instead of gliding.
inline constexpr float kMinGate = 0.05f;
inline constexpr float kMaxGate = 0.95f;
// Slot 0 always plays the selected voice; the other slots take the remaining
// voices in index order, so a Chord pattern spreads over the whole instrument.
// --- Pattern and rate catalogue ---------------------------------------------

enum class Pattern : uint8_t
{
  Up = 0,   // low to high, climbing the octave range each pass
  Down,     // high to low
  UpDown,   // up then down without repeating the turning points
  Random,   // uniform over the chord-and-octave list
  Order,    // as played (press order), also through the octave range
  Chord,    // up to four chord notes at once, one per voice, range climbs
  Count
};

enum class Rate : uint8_t
{
  Quarter = 0,         // 480 ticks
  QuarterTriplet,      // 320
  Eighth,              // 240
  EighthTriplet,       // 160
  Sixteenth,           // 120
  SixteenthTriplet,    // 80
  ThirtySecond,        // 60
  ThirtySecondTriplet, // 40
  Count
};

inline constexpr uint8_t kPatternCount = static_cast<uint8_t>(Pattern::Count);
inline constexpr uint8_t kRateCount = static_cast<uint8_t>(Rate::Count);

/** Display name for the OLED and the serial log. */
const char *patternName(Pattern pattern) noexcept;
/** Display name for the rate, in note-division notation ("1/16T"). */
const char *rateName(Rate rate) noexcept;
/** PPQN ticks between notes for one rate entry. */
uint16_t rateTicks(Rate rate) noexcept;

/**
 * Pattern a ButtonModule8 bit selects in Arpeggiator mode (bits 0-5 = the six
 * patterns). Returns Pattern::Count for any other bit, so Shift and Latch keep
 * their own meanings.
 */
Pattern patternForButtonBit(uint8_t bit) noexcept;

/** Octave range after one bump of the cycle button: 1->2->3->4->1. */
uint8_t nextOctaves(uint8_t octaves) noexcept;

/** Clamp a gate fraction into the playable window (kMinGate..kMaxGate). */
float clampGate(float gate) noexcept;

/** Clamp an octave range into 1..kMaxOctaves. */
uint8_t clampOctaves(uint8_t octaves) noexcept;

/**
 * Octave range a fader position selects: 0..1 spreads evenly over
 * kMinOctaves..kMaxOctaves.
 */
uint8_t octavesForFader(float normalized) noexcept;

// Evenly distribute hits across a short repeating grid. Rotation moves the
// entire rhythm right; zero hits is an intentional rest, not a stopped clock.
bool rhythmHit(uint8_t step, uint8_t hits, uint8_t length, uint8_t rotation) noexcept;
const char *rhythmPresetName(uint8_t preset) noexcept;

/**
 * Voice behind one arp slot: slot 0 is the selected voice (the one the player
 * chose for the arp), the rest walk the other voices in index order. Chord
 * mode uses this so four held notes sound on up to four different patches.
 */
uint8_t slotVoiceIndex(uint8_t slot, uint8_t selectedVoice) noexcept;

/**
 * LED index for an arp pad. The WS2812B panel mirrors the touch pads and
 * ControlSurface::LedLayout's band/step geometry reduces to row*8+col for the
 * same (row, col), so a pad paints its own LED. The host suite pins this
 * against LedLayout so a geometry change cannot drift the two apart.
 */
constexpr int ledIndexForPad(uint8_t pad) noexcept
{
  return pad < kPadCount ? static_cast<int>(pad) : -1;
}

// --- Settings ----------------------------------------------------------------

struct Settings
{
  Pattern pattern = Pattern::Up;
  Rate rate = Rate::Sixteenth;
  uint8_t octaves = kMinOctaves; // arp range in octaves, 1..kMaxOctaves
  float gate = 0.5f;             // note length as a fraction of the interval
  float swing = 0.0f;            // 0..1 of half an interval, applied to every second gap
  float filter = 0.5f;           // per-note Filter lane, composed with the patch
  uint8_t hits = 8;
  uint8_t length = 8;
  uint8_t rotation = 0;
  float accent = 0.0f;           // soften other hits relative to the rotated first hit
};

uint16_t intervalTicks(const Settings &settings, bool longGap) noexcept;
uint16_t gateTicks(const Settings &settings, bool longGap) noexcept;

// --- One tick's worth of note events ----------------------------------------

/**
 * What happened on one PPQN tick.
 *
 * startMask/stopMask are per-slot bit masks: slot i is the i-th voice in the
 * mapping above. degrees[]/octaves[] are valid where startMask has the bit and
 * hold scale-table index plus octave-range index (the caller turns the second
 * into VoiceState::octaveOffset = 12 * octaves[i]).
 */
struct Tick
{
  uint8_t startMask = 0;
  uint8_t stopMask = 0;
  uint8_t degrees[kMaxSlots] = {};
  uint8_t octaves[kMaxSlots] = {};

  bool started() const noexcept { return startMask != 0; }
  bool stopped() const noexcept { return stopMask != 0; }
};

// --- Engine ------------------------------------------------------------------

/**
 * @brief Chord, pattern and clock state of the arpeggiator.
 *
 * Lives inside UIState so every surface that already receives UIState can read
 * it (pads, tiles, encoder, LEDs, OLED) while the playback layer in src/app/
 * drives tick(). Core 0 only: the control loop and the clock-callback drain.
 */
class Engine
{
public:
  // --- Mode ----------------------------------------------------------------
  bool active() const noexcept { return active_; }
  /**
   * Enter or leave Arpeggiator mode. Either edge drops the chord and the walk,
   * so nothing bleeds across a mode change; the caller silences the voices
   * because leaving stops the ticks that would have ended a sounding note.
   */
  void setActive(bool on) noexcept;

  // --- Chord entry (touch pads) -------------------------------------------
  /**
   * A pad touch joins the chord. With Latch on and no pad physically held,
   * this starts a new chord: the classic latch gesture, so a latched chord can
   * be replaced by simply playing the next one.
   */
  void pressPad(uint8_t pad) noexcept;
  /** A pad release drops the note, unless Latch is holding it. */
  void releasePad(uint8_t pad) noexcept;
  /**
   * Drop every pad a finger is on, keeping latched notes.
   *
   * A modal state (the voice editor, or waiting for every control to be
   * released after it) swallows pad releases, so without this the engine would
   * keep believing a finger is down and that note could never be played again.
   */
  void releaseAllHeldPads() noexcept;
  void clearChord() noexcept;
  /**
   * Replace the chord with `notes` distinct random degrees and engage Latch, so
   * the generated chord keeps arping with no fingers on the panel.
   */
  void randomizeChord(uint8_t notes, uint32_t seed) noexcept;

  bool padInChord(uint8_t pad) const noexcept;  // physically held or latched
  bool padHeld(uint8_t pad) const noexcept;     // a finger is on it right now
  uint8_t chordCount() const noexcept;
  /** i-th chord degree, ascending (i >= chordCount() yields kNoDegree). */
  uint8_t chordDegree(uint8_t index) const noexcept;
  /** i-th chord degree in press order (i >= chordCount() yields kNoDegree). */
  uint8_t orderDegree(uint8_t index) const noexcept;

  bool latchEnabled() const noexcept { return latched_; }
  void setLatch(bool on) noexcept;
  void toggleLatch() noexcept { setLatch(!latched_); }

  // --- Settings -----------------------------------------------------------
  const Settings &settings() const noexcept { return settings_; }
  void setPattern(Pattern pattern) noexcept;
  void setRate(Rate rate) noexcept;
  /** Bump the rate by whole detents, clamped at both ends of the table. */
  void cycleRate(int steps) noexcept;
  /**
   * Rotate the rate by whatever motion accumulates to kDetent, keeping the
   * remainder. A slow turn therefore still reaches every division, which is the
   * same policy ControlSurface::EncoderMotion uses for stepped values.
   */
  void turnRate(float delta, float detent) noexcept;
  void setOctaves(uint8_t octaves) noexcept;
  void cycleOctaves() noexcept;
  void setGate(float gate) noexcept;
  void setSwing(float swing) noexcept;
  void setFilter(float filter) noexcept;
  void setRhythm(uint8_t hits, uint8_t length, uint8_t rotation) noexcept;
  void setRhythmPreset(uint8_t preset) noexcept;
  void setAccent(float accent) noexcept;
  /** Shift-faders: hits, grid length, rotation, accent, in that order. */
  void setRhythmFader(uint8_t channel, float normalized) noexcept;
  bool rhythmHitAt(uint8_t step) const noexcept;
  /** -1 for a custom grid, otherwise the matching starting point. */
  int rhythmPreset() const noexcept;
  uint8_t rhythmStep() const noexcept { return rhythmStep_; }
  bool hasRhythmStep() const noexcept { return rhythmStarted_; }
  /** Velocity multiplier captured at note-on, including hand and accent. */
  float lastVelocityScale() const noexcept { return lastVelocityScale_; }
  void resetRateMotion() noexcept { rateMotion_ = 0.0f; }
  /** Current rate name, for the OLED header. */
  const char *rateLabel() const noexcept { return rateName(settings_.rate); }
  /** Current pattern name, for the OLED header. */
  const char *patternLabel() const noexcept { return patternName(settings_.pattern); }

  // --- Dynamics (VL53L1X lidar) -------------------------------------------
  /**
   * Hand height drives note velocity. With no hand in range the arp plays the
   * patch's own velocity, so a chord entered and left alone sounds as designed;
   * a hand in range scales it from a quarter (close) to full (raised).
   */
  void observeDynamics(bool handPresent, float handHeight) noexcept;
  bool handInRange() const noexcept { return handPresent_; }
  /** Hand height 0..1, or 1.0 while no hand is in range. */
  float dynamics() const noexcept { return handPresent_ ? handHeight_ : 1.0f; }
  /** Velocity multiplier applied to the patch velocity at note-on. */
  float velocityScale() const noexcept;

  // --- Clock ---------------------------------------------------------------
  /** Restart the walk at the chord's first note on the next tick. */
  void restart() noexcept;
  /** Advance one PPQN tick and report the notes that start or stop on it. */
  Tick tick() noexcept;

  // --- Playback and display queries ---------------------------------------
  /** Note a gated-on slot is playing; false when that slot is silent. */
  bool slotSounding(uint8_t slot, uint8_t &degree, uint8_t &octave) const noexcept;
  /** True while any gated-on slot is playing this scale degree. */
  bool degreeSounding(uint8_t degree) const noexcept;
  /** True while a physical pad is sounding, including duplicate octave pads. */
  bool padSounding(uint8_t pad) const noexcept;
  /** Select seven-note octave-row mapping; zero restores linear mapping. */
  void setScaleNotesPerOctave(uint8_t notesPerOctave) noexcept;
  /** Note-on events since the last restart(), for the OLED step readout. */
  uint16_t stepCount() const noexcept { return stepCount_; }
  /** Scale degree of the most recent note-on, or kNoDegree before the first. */
  uint8_t lastDegree() const noexcept { return lastDegree_; }
  uint8_t lastOctave() const noexcept { return lastOctave_; }
  /** Gate length the last note-on took, in PPQN ticks (0 before the first). */
  uint16_t lastGateTicks() const noexcept { return lastGateTicks_; }
  /** Deterministic Random pattern for tests and for randomizeChord(). */
  void seedRandom(uint32_t seed) noexcept { rng_ = seed ? seed : 1u; }

private:
  // Named padBit: Arduino's Common.h #defines bit(b) as a macro.
  static uint32_t padBit(uint8_t pad) noexcept { return 1ul << pad; }
  uint8_t effectiveCount() const noexcept;
  bool inOrder(uint8_t pad) const noexcept;
  void appendOrder(uint8_t pad) noexcept;
  void removeOrder(uint8_t pad) noexcept;
  void keepOnlyEffectivePads() noexcept;
  uint32_t nextRandom() noexcept;
  void scheduleNext() noexcept;
  /** Walk index of the next note (before the octave split) for this step. */
  uint16_t walkIndex() noexcept;
  void startNotes(Tick &out) noexcept;

  bool active_ = false;
  Settings settings_{};

  // Chord: physically held pads, latched pads, and press order of the union.
  uint32_t physicalMask_ = 0;
  uint32_t latchedMask_ = 0;
  uint8_t order_[kPadCount] = {};
  uint8_t orderCount_ = 0;
  bool latched_ = false;

  // Dynamics
  bool handPresent_ = false;
  float handHeight_ = 1.0f;

  // Clock
  uint16_t ticksToNext_ = 0;  // 0 = the next tick starts a note
  uint16_t gateTicksLeft_ = 0;
  bool stopPending_ = false;
  uint32_t walk_ = 0;  // intervals scheduled since the last restart
  uint8_t rhythmStep_ = 0;
  uint8_t nextRhythmStep_ = 0;
  bool rhythmStarted_ = false;
  float lastVelocityScale_ = 1.0f;

  // Sounding slots
  uint8_t soundingMask_ = 0;
  uint8_t soundingDegree_[kMaxSlots] = {};
  uint8_t soundingOctave_[kMaxSlots] = {};
  uint16_t lastGateTicks_ = 0;

  // Display
  uint16_t stepCount_ = 0;
  uint8_t lastDegree_ = kNoDegree;
  uint8_t lastOctave_ = 0;

  // Encoder motion left over from rate turning
  float rateMotion_ = 0.0f;
  uint8_t scaleNotesPerOctave_ = 0;
  uint32_t rng_ = 0x9E3779B9ul;
};

} // namespace Arpeggiator

#endif // ARPEGGIATOR_H
