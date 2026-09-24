#include "Arpeggiator.h"

#include <cmath>

namespace Arpeggiator
{
namespace
{
// Note divisions of a quarter note at 480 PPQN. Indexed by Rate.
constexpr uint16_t kRateTicks[] = {480, 320, 240, 160, 120, 80, 60, 40};
static_assert(sizeof(kRateTicks) / sizeof(kRateTicks[0]) == kRateCount,
              "Every Rate needs an interval");

const char *const kRateNames[] = {"1/4", "1/4T", "1/8", "1/8T",
                                  "1/16", "1/16T", "1/32", "1/32T"};
static_assert(sizeof(kRateNames) / sizeof(kRateNames[0]) == kRateCount,
              "Every Rate needs a display name");

const char *const kPatternNames[] = {"Up", "Down", "Up-Dn", "Rnd", "Order", "Chord"};
static_assert(sizeof(kPatternNames) / sizeof(kPatternNames[0]) == kPatternCount,
              "Every Pattern needs a display name");

// Hand height to velocity: a hand close to the sensor plays a quarter of the
// patch velocity, a raised hand plays all of it.
constexpr float kDynamicsFloor = 0.25f;

float clampUnit(float value) noexcept
{
  if (!(value >= 0.0f)) // also catches NaN
    return 0.0f;
  return value > 1.0f ? 1.0f : value;
}
} // namespace

const char *patternName(Pattern pattern) noexcept
{
  const auto index = static_cast<uint8_t>(pattern);
  return index < kPatternCount ? kPatternNames[index] : "--";
}

const char *rateName(Rate rate) noexcept
{
  const auto index = static_cast<uint8_t>(rate);
  return index < kRateCount ? kRateNames[index] : "--";
}

uint16_t rateTicks(Rate rate) noexcept
{
  const auto index = static_cast<uint8_t>(rate);
  return index < kRateCount ? kRateTicks[index]
                            : kRateTicks[static_cast<uint8_t>(Rate::Sixteenth)];
}

Pattern patternForButtonBit(uint8_t bit) noexcept
{
  return bit < kPatternCount ? static_cast<Pattern>(bit) : Pattern::Count;
}

uint8_t clampOctaves(uint8_t octaves) noexcept
{
  if (octaves < kMinOctaves)
    return kMinOctaves;
  return octaves > kMaxOctaves ? kMaxOctaves : octaves;
}

uint8_t nextOctaves(uint8_t octaves) noexcept
{
  const uint8_t current = clampOctaves(octaves);
  return current >= kMaxOctaves ? kMinOctaves : static_cast<uint8_t>(current + 1);
}

float clampGate(float gate) noexcept
{
  if (!(gate >= kMinGate)) // also catches NaN
    return kMinGate;
  return gate > kMaxGate ? kMaxGate : gate;
}

uint8_t octavesForFader(float normalized) noexcept
{
  const float unit = clampUnit(normalized);
  return static_cast<uint8_t>(
      kMinOctaves + lroundf(unit * static_cast<float>(kMaxOctaves - kMinOctaves)));
}

uint8_t slotVoiceIndex(uint8_t slot, uint8_t selectedVoice) noexcept
{
  const uint8_t base = static_cast<uint8_t>(selectedVoice % kMaxSlots);
  uint8_t voice = base;
  for (uint8_t i = 0; i < slot; ++i)
  {
    // Step over the selected voice so slot 1 is a different voice, not a repeat.
    do
    {
      voice = static_cast<uint8_t>((voice + 1) % kMaxSlots);
    } while (voice == base);
  }
  return voice;
}

// --- Mode --------------------------------------------------------------------

// Both edges of the mode change reset the performance state. Settings survive:
// they are how the player wants the arp to behave, not part of one performance.
void Engine::setActive(bool on) noexcept
{
  active_ = on;
  physicalMask_ = 0;
  latchedMask_ = 0;
  orderCount_ = 0;
  latched_ = false;
  soundingMask_ = 0;
  gateTicksLeft_ = 0;
  stopPending_ = false;
  rateMotion_ = 0.0f;
  walk_ = 0;
  ticksToNext_ = 0;
  stepCount_ = 0;
  lastGateTicks_ = 0;
  lastDegree_ = kNoDegree;
  lastOctave_ = 0;
}

// --- Chord -------------------------------------------------------------------

bool Engine::inOrder(uint8_t pad) const noexcept
{
  for (uint8_t i = 0; i < orderCount_; ++i)
    if (order_[i] == pad)
      return true;
  return false;
}

void Engine::appendOrder(uint8_t pad) noexcept
{
  if (inOrder(pad) || orderCount_ >= kPadCount)
    return;
  order_[orderCount_++] = pad;
}

void Engine::removeOrder(uint8_t pad) noexcept
{
  for (uint8_t i = 0; i < orderCount_; ++i)
  {
    if (order_[i] != pad)
      continue;
    for (uint8_t j = static_cast<uint8_t>(i + 1); j < orderCount_; ++j)
      order_[j - 1] = order_[j];
    --orderCount_;
    return;
  }
}

// Why: dropping the latched notes (or the last finger) must also drop them from
// the as-played order, or the Order pattern would keep calling degrees that are
// no longer in the chord.
void Engine::keepOnlyEffectivePads() noexcept
{
  const uint32_t effective = physicalMask_ | latchedMask_;
  uint8_t kept = 0;
  for (uint8_t i = 0; i < orderCount_; ++i)
    if (effective & padBit(order_[i]))
      order_[kept++] = order_[i];
  orderCount_ = kept;
  for (uint8_t pad = 0; pad < kPadCount; ++pad)
    if ((effective & padBit(pad)) && !inOrder(pad))
      order_[orderCount_++] = pad;
}

void Engine::clearChord() noexcept
{
  physicalMask_ = 0;
  latchedMask_ = 0;
  orderCount_ = 0;
}

void Engine::pressPad(uint8_t pad) noexcept
{
  if (pad >= kPadCount)
    return;
  // Latch on with nothing held means the player is starting the next chord.
  if (latched_ && physicalMask_ == 0)
  {
    latchedMask_ = 0;
    orderCount_ = 0;
  }
  physicalMask_ |= padBit(pad);
  appendOrder(pad);
}

void Engine::releasePad(uint8_t pad) noexcept
{
  if (pad >= kPadCount)
    return;
  const uint32_t mask = padBit(pad);
  if (!(physicalMask_ & mask))
    return; // a release with no press behind it: nothing to drop
  physicalMask_ &= ~mask;
  if (latched_)
  {
    // The latch takes the note over: it stays in the chord and in the order.
    latchedMask_ |= mask;
    return;
  }
  latchedMask_ &= ~mask;
  removeOrder(pad);
}

void Engine::releaseAllHeldPads() noexcept
{
  if (physicalMask_ == 0)
    return;
  physicalMask_ = 0;
  // Latched notes are not held and stay; the order list follows the smaller
  // chord either way.
  keepOnlyEffectivePads();
}

void Engine::setLatch(bool on) noexcept
{
  if (latched_ == on)
    return;
  latched_ = on;
  if (!on)
  {
    // Turning the latch off lets its notes go; whatever is still held stays.
    latchedMask_ = 0;
    keepOnlyEffectivePads();
  }
}

void Engine::randomizeChord(uint8_t notes, uint32_t seed) noexcept
{
  if (notes < 1)
    notes = 1;
  if (notes > kPadCount)
    notes = kPadCount;
  seedRandom(seed);
  clearChord();
  uint32_t chosen = 0;
  for (uint8_t i = 0; i < notes; ++i)
  {
    uint8_t pad = 0;
    do
    {
      pad = static_cast<uint8_t>(nextRandom() % kPadCount);
    } while (chosen & padBit(pad));
    chosen |= padBit(pad);
  }
  // Ascending insert: the sorted patterns start on the new chord's root, and the
  // Order pattern hears the chord the way the panel shows it.
  for (uint8_t pad = 0; pad < kPadCount; ++pad)
  {
    if (!(chosen & padBit(pad)))
      continue;
    latchedMask_ |= padBit(pad);
    appendOrder(pad);
  }
  // Hold the generated chord with the latch, or it would fall silent the moment
  // the player let go of the button that made it.
  latched_ = true;
}

uint8_t Engine::effectiveCount() const noexcept
{
  uint32_t mask = physicalMask_ | latchedMask_;
  uint8_t count = 0;
  while (mask)
  {
    mask &= mask - 1u;
    ++count;
  }
  return count;
}

uint8_t Engine::chordDegree(uint8_t index) const noexcept
{
  const uint32_t mask = physicalMask_ | latchedMask_;
  uint8_t seen = 0;
  for (uint8_t pad = 0; pad < kPadCount; ++pad)
  {
    if (!(mask & padBit(pad)))
      continue;
    if (seen == index)
      return pad;
    ++seen;
  }
  return kNoDegree;
}

uint8_t Engine::orderDegree(uint8_t index) const noexcept
{
  return index < orderCount_ ? order_[index] : kNoDegree;
}

bool Engine::padInChord(uint8_t pad) const noexcept
{
  return pad < kPadCount && ((physicalMask_ | latchedMask_) & padBit(pad)) != 0;
}

bool Engine::padHeld(uint8_t pad) const noexcept
{
  return pad < kPadCount && (physicalMask_ & padBit(pad)) != 0;
}

uint8_t Engine::chordCount() const noexcept
{
  return effectiveCount();
}

// --- Settings ----------------------------------------------------------------

void Engine::setPattern(Pattern pattern) noexcept
{
  if (static_cast<uint8_t>(pattern) >= kPatternCount)
    return;
  // The walk keeps its phase: flipping patterns mid-performance must not lose the
  // beat, so only the order the walk visits notes in changes.
  settings_.pattern = pattern;
}

void Engine::setRate(Rate rate) noexcept
{
  if (static_cast<uint8_t>(rate) >= kRateCount)
    rate = Rate::Sixteenth;
  settings_.rate = rate;
  // A pending wait longer than the new interval would stall the next note, so a
  // rate change takes effect at once instead of after the old division elapsed.
  const uint16_t interval = rateTicks(rate);
  if (ticksToNext_ > interval)
    ticksToNext_ = interval;
  const uint16_t gateCap = interval > 1 ? static_cast<uint16_t>(interval - 1) : 1;
  if (gateTicksLeft_ > gateCap)
    gateTicksLeft_ = gateCap;
}

void Engine::cycleRate(int steps) noexcept
{
  if (steps == 0)
    return;
  int next = static_cast<int>(settings_.rate) + steps;
  if (next < 0)
    next = 0;
  if (next >= kRateCount)
    next = kRateCount - 1;
  setRate(static_cast<Rate>(next));
}

void Engine::turnRate(float delta, float detent) noexcept
{
  if (!(delta == delta) || !(detent > 0.0f))
    return; // NaN motion, or a detent that cannot be reached
  if (delta != 0.0f && (delta < 0.0f) != (rateMotion_ < 0.0f))
    rateMotion_ = 0.0f; // a reversal answers at once instead of unwinding first
  rateMotion_ += delta;
  int steps = 0;
  while (rateMotion_ >= detent)
  {
    rateMotion_ -= detent;
    ++steps;
  }
  while (rateMotion_ <= -detent)
  {
    rateMotion_ += detent;
    --steps;
  }
  cycleRate(steps);
}

void Engine::setOctaves(uint8_t octaves) noexcept
{
  settings_.octaves = clampOctaves(octaves);
}

void Engine::cycleOctaves() noexcept
{
  settings_.octaves = nextOctaves(settings_.octaves);
}

void Engine::setGate(float gate) noexcept
{
  settings_.gate = clampGate(gate);
}

void Engine::setSwing(float swing) noexcept
{
  settings_.swing = clampUnit(swing);
}

void Engine::setFilter(float filter) noexcept
{
  settings_.filter = clampUnit(filter);
}

// --- Dynamics ----------------------------------------------------------------

void Engine::observeDynamics(bool handPresent, float handHeight) noexcept
{
  handPresent_ = handPresent;
  handHeight_ = clampUnit(handHeight);
}

float Engine::velocityScale() const noexcept
{
  if (!handPresent_)
    return 1.0f; // no hand: the patch's own velocity, unattenuated
  return kDynamicsFloor + (1.0f - kDynamicsFloor) * handHeight_;
}

// --- Clock -------------------------------------------------------------------

void Engine::restart() noexcept
{
  walk_ = 0;
  ticksToNext_ = 0; // the very next tick starts a note
  stepCount_ = 0;
  lastGateTicks_ = 0;
  lastDegree_ = kNoDegree;
  lastOctave_ = 0;
  rateMotion_ = 0.0f;
  // A sounding note's gate is re-reported by the next tick so the caller can end
  // it: restart() must never leave a note gated with no expiry left.
  if (soundingMask_ != 0)
    stopPending_ = true;
}

Tick Engine::tick() noexcept
{
  Tick out;
  if (!active_)
    return out;

  if (stopPending_)
  {
    out.stopMask = soundingMask_;
    soundingMask_ = 0;
    gateTicksLeft_ = 0;
    stopPending_ = false;
  }
  // A gate that ends on this tick stops before anything starts on it, so one
  // voice is retriggered from silence instead of being gated twice.
  if (gateTicksLeft_ > 0 && --gateTicksLeft_ == 0)
  {
    out.stopMask |= soundingMask_;
    soundingMask_ = 0;
  }
  if (ticksToNext_ > 0)
    --ticksToNext_;
  if (ticksToNext_ == 0)
    startNotes(out);
  return out;
}

uint32_t Engine::nextRandom() noexcept
{
  // xorshift32: deterministic, allocation-free, and plenty for picking a chord
  // degree. A non-zero seed cannot reach zero, so the sequence cannot lock up.
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}

uint16_t Engine::walkIndex() noexcept
{
  const uint16_t notes =
      static_cast<uint16_t>(effectiveCount()) * clampOctaves(settings_.octaves);
  if (notes == 0)
    return 0;
  switch (settings_.pattern)
  {
  case Pattern::Down:
    return static_cast<uint16_t>(notes - 1 - (walk_ % notes));
  case Pattern::UpDown:
  {
    if (notes <= 1)
      return 0;
    const uint16_t period = static_cast<uint16_t>(2 * notes - 2);
    const uint16_t k = static_cast<uint16_t>(walk_ % period);
    return k < notes ? k : static_cast<uint16_t>(period - k);
  }
  case Pattern::Random:
    return static_cast<uint16_t>(nextRandom() % notes);
  case Pattern::Up:
  case Pattern::Order:
  default:
    return static_cast<uint16_t>(walk_ % notes);
  }
}

void Engine::startNotes(Tick &out) noexcept
{
  const uint8_t count = effectiveCount();
  const uint8_t octaves = clampOctaves(settings_.octaves);

  if (count > 0)
  {
    if (settings_.pattern == Pattern::Chord)
    {
      // One slot per chord note (up to the four voices), all on the same octave
      // of the range; the range steps up once per chord.
      const uint8_t slots = count < kMaxSlots ? count : kMaxSlots;
      const uint8_t octave = static_cast<uint8_t>(walk_ % octaves);
      out.startMask = static_cast<uint8_t>((1u << slots) - 1u);
      for (uint8_t slot = 0; slot < slots; ++slot)
      {
        out.degrees[slot] = chordDegree(slot);
        out.octaves[slot] = octave;
      }
      lastDegree_ = out.degrees[0];
      lastOctave_ = octave;
    }
    else
    {
      // Mono patterns walk the chord repeated once per octave of range: index i
      // is chord note i % count at octave i / count.
      const uint16_t index = walkIndex();
      const uint8_t chordIndex = static_cast<uint8_t>(index % count);
      const uint8_t degree = settings_.pattern == Pattern::Order ? orderDegree(chordIndex)
                                                                 : chordDegree(chordIndex);
      // A missing order entry (should not happen; the order list is maintained
      // with the chord) plays as a rest instead of a garbage pitch.
      if (degree != kNoDegree)
      {
        out.startMask = 1u;
        out.degrees[0] = degree;
        out.octaves[0] = static_cast<uint8_t>(index / count);
        lastDegree_ = degree;
        lastOctave_ = out.octaves[0];
      }
    }

    if (out.startMask != 0)
    {
      soundingMask_ = out.startMask;
      for (uint8_t slot = 0; slot < kMaxSlots; ++slot)
      {
        if (!(out.startMask & (1u << slot)))
          continue;
        soundingDegree_[slot] = out.degrees[slot];
        soundingOctave_[slot] = out.octaves[slot];
      }
      ++stepCount_;
    }
  }
  // The interval is scheduled whether or not a note was emitted: an empty chord
  // leaves rests, not a rhythm that stops and restarts out of time.
  scheduleNext();
}

void Engine::scheduleNext() noexcept
{
  const uint16_t interval = rateTicks(settings_.rate);
  // Swing: every second gap is long and its partner short by the same amount, so
  // a pair still lasts two intervals and the arp cannot drift against the
  // transport. Half an interval is the most a pair can take before the short gap
  // would collapse.
  int32_t swing = static_cast<int32_t>(
      lroundf(clampUnit(settings_.swing) * 0.5f * static_cast<float>(interval)));
  const int32_t maxSwing = static_cast<int32_t>(interval / 2) - 1;
  if (maxSwing <= 0)
    swing = 0;
  else if (swing > maxSwing)
    swing = maxSwing;
  const bool longGap = (walk_ % 2) == 0;
  const int32_t gap = static_cast<int32_t>(interval) + (longGap ? swing : -swing);
  const uint16_t finalGap = static_cast<uint16_t>(gap < 1 ? 1 : gap);
  ticksToNext_ = finalGap;

  // The note lasts a fraction of the gap that follows it and always leaves a
  // sliver, so the next note retriggers from silence instead of gliding.
  uint16_t gate = static_cast<uint16_t>(
      lroundf(clampGate(settings_.gate) * static_cast<float>(finalGap)));
  if (gate < 1)
    gate = 1;
  if (gate >= finalGap)
    gate = static_cast<uint16_t>(finalGap - 1);
  gateTicksLeft_ = gate;
  lastGateTicks_ = gate;
  ++walk_;
}

// --- Playback and display queries --------------------------------------------

bool Engine::slotSounding(uint8_t slot, uint8_t &degree, uint8_t &octave) const noexcept
{
  if (slot >= kMaxSlots || !(soundingMask_ & (1u << slot)))
    return false;
  degree = soundingDegree_[slot];
  octave = soundingOctave_[slot];
  return true;
}

bool Engine::degreeSounding(uint8_t degree) const noexcept
{
  for (uint8_t slot = 0; slot < kMaxSlots; ++slot)
    if ((soundingMask_ & (1u << slot)) && soundingDegree_[slot] == degree)
      return true;
  return false;
}

} // namespace Arpeggiator
