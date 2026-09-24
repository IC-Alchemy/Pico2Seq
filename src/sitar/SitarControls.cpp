#include "SitarControls.h"

#include <cmath>

// SitarControls.cpp — Sitar Explorer's gesture policy (portable, Core 0).
// Everything here is a decision: which fret a pad plays, what a slide means,
// how the knob moves a lane, which strokes the clock fires. No Arduino, no
// rpdsp, no registers — the firmware glue (SitarPerformance.cpp) turns these
// results into queue pushes and light.

namespace Sitar
{
namespace
{
// MIDI note -> Hz, duplicated here so this file stays rpdsp-free: the tests
// compare it against rpdsp::midiNoteToHz to prove the two agree.
constexpr float kNoteAFrequency = 440.0f;
constexpr int kNoteAMidiNote = 69;

float hzFromMidi(int midiNote) noexcept
{
    const float semitones = static_cast<float>(midiNote - kNoteAMidiNote);
    return kNoteAFrequency * std::pow(2.0f, semitones / 12.0f);
}

float clamp01(float value) noexcept
{
    if (!(value > 0.0f)) // also catches NaN
        return 0.0f;
    return value > 1.0f ? 1.0f : value;
}

// A jhala accent and the tanpura's bar pulse can share a sixteenth, so every
// write into the stroke list checks the bound instead of trusting the caller.
void addStroke(ClockStrokes &strokes, Course course, float frequency, float amplitude) noexcept
{
    if (strokes.count >= kClockStrokeParts)
        return;
    const uint8_t index = strokes.count++;
    strokes.course[index] = course;
    strokes.frequency[index] = frequency;
    strokes.amplitude[index] = amplitude;
}

// Course a row-1 stroke pad sounds. Course::Count = the pad is a mode action.
Course courseForStroke(Stroke stroke) noexcept
{
    switch (stroke)
    {
    case Stroke::ChikariPa: return Course::ChikariLow;
    case Stroke::ChikariSa: return Course::ChikariHigh;
    case Stroke::Jod: return Course::ChikariLow;
    case Stroke::Kharaj: return Course::Kharaj;
    case Stroke::Shimmer: return Course::Main;
    case Stroke::JhalaCycle:
    case Stroke::Tanpura:
    case Stroke::Damp:
    case Stroke::Count:
        break;
    }
    return Course::Count;
}
} // namespace

float strokeFrequency(Stroke stroke) noexcept
{
    switch (stroke)
    {
    case Stroke::ChikariPa: return hzFromMidi(kSaMidiNote + kChikariPaSemitones);
    case Stroke::ChikariSa: return hzFromMidi(kSaMidiNote + kChikariSaSemitones);
    case Stroke::Jod: return hzFromMidi(kSaMidiNote + kChikariPaSemitones);
    case Stroke::Kharaj: return hzFromMidi(kSaMidiNote + kKharajSemitones);
    case Stroke::JhalaCycle:
    case Stroke::Tanpura:
    case Stroke::Shimmer:
    case Stroke::Damp:
    case Stroke::Count:
        break;
    }
    return 0.0f;
}

const char *strokeName(Stroke stroke) noexcept
{
    switch (stroke)
    {
    case Stroke::ChikariPa: return "Chikari Pa";
    case Stroke::ChikariSa: return "Chikari Sa";
    case Stroke::Jod: return "Jod (both)";
    case Stroke::Kharaj: return "Kharaj";
    case Stroke::JhalaCycle: return "Jhala";
    case Stroke::Tanpura: return "Tanpura";
    case Stroke::Shimmer: return "Shimmer";
    case Stroke::Damp: return "Damp";
    case Stroke::Count: break;
    }
    return "?";
}

Course strokeCourse(Stroke stroke) noexcept
{
    return courseForStroke(stroke);
}

const char *exploreName(ExploreAction action) noexcept
{
    switch (action)
    {
    case ExploreAction::GroupString: return "Group: String";
    case ExploreAction::GroupJawari: return "Group: Jawari";
    case ExploreAction::GroupTaraf: return "Group: Taraf";
    case ExploreAction::GroupBody: return "Group: Body";
    case ExploreAction::GroupMeend: return "Group: Meend";
    case ExploreAction::Randomize: return "Randomize";
    case ExploreAction::Defaults: return "sitar.h defaults";
    case ExploreAction::Dump: return "Console legend";
    case ExploreAction::Count: break;
    }
    return "?";
}

const char *jhalaName(JhalaPattern pattern) noexcept
{
    switch (pattern)
    {
    case JhalaPattern::Off: return "off";
    case JhalaPattern::Eighths: return "1/8";
    case JhalaPattern::Sixteenths: return "1/16";
    case JhalaPattern::Jhala: return "jhala";
    case JhalaPattern::Count: break;
    }
    return "?";
}

void Controls::enter() noexcept
{
    active = true;
    // The fingers that opened the mode must leave the panel before any pad can
    // fire a pluck, or the entry chord plays itself.
    waitRelease = true;
    fingerDown = false;
    lastClockSlot = 0xFF;
    holdArmed = false;
    for (auto &stamp : fretStampMs)
        stamp = 0;
    for (auto &stamp : strokeStampMs)
        stamp = 0;
    restoreDefaults();
}

void Controls::exit() noexcept
{
    active = false;
    fingerDown = false;
    holdArmed = false;
    // Strings are left ringing on purpose: a sitar does not stop when the
    // player lifts their hands, and the mode has no note of its own to cut.
}

void Controls::stampStroke(Stroke stroke, uint32_t now) noexcept
{
    if (stroke >= Stroke::Count)
        return;
    strokeStampMs[static_cast<uint8_t>(stroke)] = now;
}

void Controls::stampExplore(ExploreAction action, uint32_t now) noexcept
{
    if (action >= ExploreAction::Count)
        return;
    exploreStampMs[static_cast<uint8_t>(action)] = now;
}

PadResult Controls::padPressed(uint8_t pad, uint32_t now) noexcept
{
    PadResult result{};
    if (!active || waitRelease)
        return result;

    const PadRole role = roleForPad(pad);
    if (role == PadRole::Fret)
    {
        const uint8_t fret = fretForPad(pad);
        result.fret = fret;
        result.frequency = fretFrequency(fret);
        result.course = Course::Main;
        result.amplitude = pluckForce_;
        // A finger that lands on a fret plays a new note; a finger already down
        // that travels sideways bends the note that is ringing (meend). That
        // distinction is the whole point of the fingerboard.
        result.kind = fingerDown ? PadResult::Kind::Slide : PadResult::Kind::Pluck;
        fingerDown = true;
        fingerFret = fret;
        lastFretChangeMs = now;
        if (result.kind == PadResult::Kind::Pluck)
            fretStampMs[fret] = now;
        return result;
    }

    if (role == PadRole::Explore)
    {
        const auto action = static_cast<ExploreAction>(pad - kExploreFirstPad);
        const ExploreAction current =
            action < ExploreAction::Count ? action : ExploreAction::GroupString;
        stampExplore(current, now);
        result.kind = PadResult::Kind::Explore;
        result.explore = current;
        return result;
    }

    const auto stroke = static_cast<Stroke>(pad - kStrokeFirstPad);
    const Stroke current = stroke < Stroke::Count ? stroke : Stroke::Damp;
    stampStroke(current, now);
    switch (current)
    {
    case Stroke::ChikariPa:
    case Stroke::ChikariSa:
    case Stroke::Kharaj:
        result.kind = PadResult::Kind::Chikari;
        result.course = courseForStroke(current);
        result.frequency = strokeFrequency(current);
        result.amplitude = (current == Stroke::Kharaj ? 0.95f : 0.8f) * pluckForce_;
        return result;
    case Stroke::Jod:
        // Both chikari strings in one stroke: the classic jod "both" stroke.
        result.kind = PadResult::Kind::Chikari;
        result.course = Course::ChikariLow;
        result.frequency = strokeFrequency(Stroke::Jod);
        result.amplitude = 0.8f * pluckForce_;
        result.secondCourse = Course::ChikariHigh;
        result.secondFrequency = hzFromMidi(kSaMidiNote + kChikariSaSemitones);
        result.secondAmplitude = 0.7f * pluckForce_;
        return result;
    case Stroke::JhalaCycle:
        result.kind = PadResult::Kind::Jhala;
        return result;
    case Stroke::Tanpura:
        result.kind = PadResult::Kind::Tanpura;
        return result;
    case Stroke::Shimmer:
        result.kind = PadResult::Kind::Shimmer;
        result.course = Course::Main;
        result.frequency = fretFrequency(fingerFret);
        // Barely a pluck: the taraf bank answers the string, and the melody
        // string itself stays a whisper instead of being retriggered. Not
        // scaled by the hand — a whisper is a whisper.
        result.amplitude = 0.06f;
        return result;
    case Stroke::Damp:
        result.kind = PadResult::Kind::Damp;
        return result;
    case Stroke::Count:
        break;
    }
    return result;
}

PadResult Controls::padReleased(uint8_t pad, uint32_t now) noexcept
{
    PadResult result{};
    if (!active || waitRelease)
        return result;
    if (roleForPad(pad) != PadRole::Fret)
        return result;
    (void)now;
    // Lifting the finger ends the gesture but not the note: the string keeps
    // ringing at the fret it was left on, exactly like a real sitar.
    fingerDown = false;
    return result;
}

ButtonIntent Controls::buttonPressed(uint8_t bit, bool shift) noexcept
{
    ButtonIntent intent{};
    if (!active)
        return intent;
    switch (bit)
    {
    case 0: intent.kind = ButtonIntent::Kind::StrokeUp; break;
    case 1: intent.kind = ButtonIntent::Kind::StrokeDown; break;
    case 2:
        intent.kind = shift ? ButtonIntent::Kind::RagaPrevious : ButtonIntent::Kind::RagaNext;
        break;
    case 3:
        intent.kind = shift ? ButtonIntent::Kind::TanpuraToggle : ButtonIntent::Kind::JhalaCycle;
        break;
    case 4: intent.kind = ButtonIntent::Kind::FocusNext; break;
    case 5:
        intent.kind = shift ? ButtonIntent::Kind::Defaults : ButtonIntent::Kind::FocusPrevious;
        break;
    case 6:
        // Randomize on tap; Shift makes it the deliberate way out of the mode,
        // so leaving never wipes the sound the performer just dialled in.
        intent.kind = shift ? ButtonIntent::Kind::Exit : ButtonIntent::Kind::Randomize;
        break;
    default:
        break;
    }
    return intent;
}

ButtonIntent Controls::pollHeld(uint8_t bits, uint32_t now) noexcept
{
    ButtonIntent intent{};
    if (!active)
        return intent;
    const bool randomizeHeld = (bits & (1u << 6)) != 0;
    if (!randomizeHeld)
    {
        holdArmed = false;
        return intent;
    }
    if (!holdArmed)
    {
        holdArmed = true;
        holdStartedMs = now;
        return intent;
    }
    // 700 ms, the same deliberate hold the voice editor uses for its reset.
    if (now - holdStartedMs >= 700u)
    {
        holdArmed = false;
        intent.kind = ButtonIntent::Kind::Defaults;
    }
    return intent;
}

bool Controls::nudgeFocused(float travel) noexcept
{
    if (!active || travel == 0.0f)
        return false;
    float updated = value[paramIndex(focus)] + travel;
    if (!(updated > 0.0f))
        updated = 0.0f;
    if (updated > 1.0f)
        updated = 1.0f;
    if (updated == value[paramIndex(focus)])
        return false;
    value[paramIndex(focus)] = updated;
    return true;
}

Controls::PollResult Controls::poll(uint8_t buttons, uint8_t voices, bool shift, uint32_t now) noexcept
{
    PollResult result{};
    if (!active)
        return result;

    if (waitRelease)
    {
        // The fingers that opened the mode must leave the tiles before any
        // button can fire, unlike the pads, which the mode simply ignores.
        if (buttons == 0 && voices == 0)
            waitRelease = false;
        previousButtons_ = buttons;
        previousVoices_ = voices;
        return result;
    }

    const uint8_t pressed = static_cast<uint8_t>(buttons & ~previousButtons_);
    const uint8_t voicePress = static_cast<uint8_t>(voices & ~previousVoices_);
    previousButtons_ = buttons;
    previousVoices_ = voices;

    // A voice button picks a raga; the first one in the pass wins (the tile bus
    // serializes them anyway).
    for (uint8_t voice = 0; voice < kRagaCount; ++voice)
    {
        if (voicePress & (1u << voice))
        {
            result.raga = static_cast<int8_t>(voice);
            ragaIndex = voice;
            break;
        }
    }

    // Bits 0..6 are the mode's buttons; bit 7 is Shift, handled as a level.
    for (uint8_t bit = 0; bit <= 6; ++bit)
    {
        if (pressed & (1u << bit))
        {
            result.button = buttonPressed(bit, shift);
            break;
        }
    }

    // Holds are polled, never blocking: a randomize hold restores sitar.h's
    // defaults, and a press edge above outranks it in the same pass.
    if (result.button.kind == ButtonIntent::Kind::None)
        result.button = pollHeld(buttons, now);

    return result;
}

void Controls::setValue(Param id, float normalized) noexcept
{
    value[paramIndex(id) < kParamCount ? paramIndex(id) : 0] = clamp01(normalized);
}

void Controls::setGroup(ParamGroup group) noexcept
{
    if (group >= ParamGroup::Count)
        return;
    focus = firstParamOf(group);
}

void Controls::restoreDefaults() noexcept
{
    for (uint8_t index = 0; index < kParamCount; ++index)
        value[index] = normalizedValue(static_cast<Param>(index), kParamTable[index].defaultValue);
}

void Controls::randomize() noexcept
{
    for (uint8_t index = 0; index < kParamCount; ++index)
    {
        const ParamInfo &info = kParamTable[index];
        const float engineering =
            info.musicalMin + (info.musicalMax - info.musicalMin) * nextRandom01();
        value[index] = normalizedValue(static_cast<Param>(index), engineering);
    }
    // The cursor lands on the first lane, so the OLED names something the
    // performer can hear change.
    focus = Param::StringT60;
}

void Controls::observeHand(bool handPresent, float hand01) noexcept
{
    if (!handPresent)
    {
        // No hand over the sensor: a firm default stroke, so a bare panel plays.
        pluckForce_ = 0.85f;
        return;
    }
    // Hand height is force: near the sensor a soft brush, high above it a hard
    // mizrab stroke — and sitar.h makes harder plucks buzz harder.
    pluckForce_ = 0.3f + 0.7f * clamp01(hand01);
}

ClockStrokes Controls::clockStep(uint8_t sixteenthInBar, uint32_t nowMs) noexcept
{
    ClockStrokes strokes{};
    if (!active)
        return strokes;
    const uint8_t slot = static_cast<uint8_t>(sixteenthInBar & 15u);
    // A stalled loop can drain a burst of staged steps in one pass, and a step
    // number delivered twice must not double-strike the drone.
    if (slot == lastClockSlot)
        return strokes;
    lastClockSlot = slot;

    if (jhalaStrike(jhala, slot))
    {
        const bool accent = jhala == JhalaPattern::Jhala && (slot % 4u) == 0u;
        addStroke(strokes, Course::ChikariLow, strokeFrequency(Stroke::ChikariPa),
                  (accent ? 0.85f : 0.5f) * pluckForce_);
        // The clock stamps the drone row itself, so the panel shows the rhythm
        // the transport is playing even with no finger on the pads.
        stampStroke(Stroke::ChikariPa, nowMs);
        // Sixteenths strike both chikari strings; the jhala pattern doubles
        // them only on the accent, the way a player's stroke catches both when
        // the tempo drives them.
        if (accent || jhala == JhalaPattern::Sixteenths)
        {
            addStroke(strokes, Course::ChikariHigh, strokeFrequency(Stroke::ChikariSa),
                      (accent ? 0.7f : 0.4f) * pluckForce_);
            stampStroke(Stroke::ChikariSa, nowMs);
        }
    }

    // The tanpura pulse keeps the bass alive once per bar, independent of the
    // jhala pattern — exactly like a tanpura player's loop under the melody.
    if (tanpura && slot == 0u)
    {
        addStroke(strokes, Course::Kharaj, strokeFrequency(Stroke::Kharaj), 0.8f * pluckForce_);
        stampStroke(Stroke::Kharaj, nowMs);
    }

    return strokes;
}

StrokeRequest Controls::strokeUp() const noexcept
{
    // Jod up: a pluck at the bottom fret and a meend to the top, so the whole
    // two octaves of the raga travel in one gesture.
    StrokeRequest request{};
    request.pluck = true;
    request.slide = true;
    request.frequency = fretFrequency(static_cast<uint8_t>(kFretCount - 1));
    request.amplitude = 0.95f * pluckForce_;
    return request;
}

StrokeRequest Controls::strokeDown() const noexcept
{
    // Jod down: glide the ringing string back to the bottom fret and rest it
    // there — the descent a player makes before starting the next phrase.
    StrokeRequest request{};
    request.slide = true;
    request.frequency = fretFrequency(0);
    return request;
}

float Controls::fretFrequency(uint8_t fret) const noexcept
{
    return hzFromMidi(fretMidiNote(currentRaga(), fret));
}

float Controls::nextRandom01() noexcept
{
    // Xor-shift in the same family the DSP uses: deterministic, allocation-free
    // and good enough to land on settings a sitarist would recognize.
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return static_cast<float>(randomState & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

} // namespace Sitar
