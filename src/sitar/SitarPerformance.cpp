#include "SitarPerformance.h"

#include "../app/AppState.h"
#include "../LEDMatrix/ledMatrix.h"
#include "../sensors/EncoderManager.h"
#include "../sensors/SensorConstants.h"
#include "../ui/ControlSurfaceLogic.h"
#include "../ui/UITransitions.h"
#include "../voice/VoiceManager.h"

#include <Arduino.h>
#include <algorithm>
#include <uClock.h>

// SitarPerformance.cpp — Sitar Explorer's Core 0 glue (see the header for the
// musical contract). Everything here is translation: hardware edges become
// Controls decisions, Controls results become Instrument publications, and the
// panel's lights and the console report what the sitar is doing.

namespace Sitar::Performance
{
namespace
{
// Fret colours, warm like a sitar's stage light: the raga's degrees walk from
// saffron through green to rose, so the fingerboard reads as a scale map.
constexpr CRGB kFretPalette[7] = {
    CRGB(255, 90, 40),   // Sa
    CRGB(255, 140, 40),  // Re
    CRGB(255, 200, 60),  // Ga
    CRGB(180, 255, 120), // Ma
    CRGB(90, 220, 255),  // Pa
    CRGB(150, 140, 255), // Dha
    CRGB(255, 120, 200), // Ni
};
// A struck or held fret, and the warm wash the ringing taraf adds.
constexpr CRGB kFretHit = CRGB(255, 245, 225);
constexpr CRGB kBloom = CRGB(255, 170, 80);
// Right-hand row: chikari strokes are ember, the mode pads cooler.
constexpr CRGB kStrokeHit = CRGB(255, 70, 25);
constexpr CRGB kDroneIdle = CRGB(60, 22, 10);
constexpr CRGB kModeIdle = CRGB(30, 30, 38);
// Exploration row: the focused lane's group burns bright, the rest wait.
constexpr CRGB kGroupIdle = CRGB(28, 34, 30);
constexpr CRGB kGroupActive = CRGB(120, 255, 190);
// How long a pad's flash takes to fade (wall clock, never frame-counted).
constexpr uint32_t kFretFlashMs = 700;
constexpr uint32_t kStrokeFlashMs = 220;
constexpr uint32_t kModeFlashMs = 900;

// Linear pad index -> the LED the panel mirrors it on, through the shared
// geometry helper so the two surfaces can never drift apart.
void setPadLed(LEDMatrix &ledMatrix, uint8_t pad, const CRGB &color)
{
    const uint8_t band = static_cast<uint8_t>(pad / ControlSurface::LedLayout::kStepsPerBand);
    const uint8_t step = static_cast<uint8_t>(pad % ControlSurface::LedLayout::kStepsPerBand);
    ledMatrix.setLED(ControlSurface::LedLayout::x(step), ControlSurface::LedLayout::y(band, step), color);
}

// Decaying flash envelope for a wall-clock stamp (0 = never lit).
float flashLevel(uint32_t stamp, uint32_t nowMs, uint32_t decayMs)
{
    if (stamp == 0)
        return 0.0f;
    const uint32_t age = nowMs - stamp;
    if (age >= decayMs)
        return 0.0f;
    return 1.0f - static_cast<float>(age) / static_cast<float>(decayMs);
}

CRGB blend(const CRGB &base, const CRGB &over, float amount)
{
    if (amount <= 0.0f)
        return base;
    if (amount > 1.0f)
        amount = 1.0f;
    const auto mix = [amount](uint8_t a, uint8_t b) {
        return static_cast<uint8_t>(a + (b - a) * amount);
    };
    return CRGB(mix(base.r, over.r), mix(base.g, over.g), mix(base.b, over.b));
}

// Scale a colour by 0..1 without touching the global brightness.
CRGB scaleColor(const CRGB &color, float level)
{
    if (level <= 0.0f)
        return CRGB(0, 0, 0);
    if (level > 1.0f)
        level = 1.0f;
    return CRGB(static_cast<uint8_t>(color.r * level), static_cast<uint8_t>(color.g * level),
                static_cast<uint8_t>(color.b * level));
}

// Push one lane's travel to the audio thread. Everything the mode edits goes
// through here, so there is exactly one place where UI travel becomes a DSP
// value.
void publishLane(Sitar::Controls &controls, Sitar::Param id)
{
    Sitar::instrument().setParameter(id, controls.value[Sitar::paramIndex(id)]);
}

void publishAllLanes(Sitar::Controls &controls)
{
    for (uint8_t index = 0; index < Sitar::kParamCount; ++index)
        publishLane(controls, static_cast<Sitar::Param>(index));
}

// Names for the console, in browse order.
void printLaneLine(const Sitar::Controls &controls, Sitar::Param id)
{
    char value[32] = {};
    Sitar::formatParamValue(id, controls.value[Sitar::paramIndex(id)], value, sizeof(value));
    const Sitar::ParamInfo &info = Sitar::paramInfo(id);
    Serial.printf("  %2u  %-16s %-12s %-22s %s\n", static_cast<unsigned>(Sitar::paramIndex(id) + 1),
                  info.name, value, info.setter, info.note);
}

void announceRaga(const Sitar::Controls &controls)
{
    const Sitar::Raga &table = controls.currentRaga();
    Serial.printf("[SITAR] raga %s (%s) vadi %s samvadi %s\n", table.name, table.thaat,
                  Sitar::kSargam[table.vadi], Sitar::kSargam[table.samvadi]);
}

// The jod gestures: one pluck and/or one meend, in the order the performer's
// hand would make them.
void applyStroke(Sitar::Controls &controls, const Sitar::StrokeRequest &request)
{
    if (request.pluck)
        Sitar::instrument().pluck(Sitar::Course::Main, controls.fretFrequency(0), request.amplitude);
    if (request.slide)
        Sitar::instrument().slide(Sitar::Course::Main, request.frequency);
    // The stroke leaves the string ringing at the fret it ended on, so the
    // next meend starts from there.
    controls.fingerFret = 0;
    controls.fingerDown = false;
}

void applyExplore(UIState &uiState, Sitar::ExploreAction action)
{
    Sitar::Controls &controls = uiState.sitar;
    const Sitar::ParamGroup group = Sitar::exploreGroup(action);
    if (group != Sitar::ParamGroup::Count)
    {
        controls.setGroup(group);
        Serial.printf("[SITAR] group %s -> lane %u %s\n", Sitar::groupName(group),
                      static_cast<unsigned>(Sitar::paramIndex(controls.focus) + 1),
                      Sitar::paramInfo(controls.focus).name);
        return;
    }
    switch (action)
    {
    case Sitar::ExploreAction::Randomize:
        controls.randomize();
        publishAllLanes(controls);
        controls.stampExplore(Sitar::ExploreAction::Randomize, millis());
        Serial.println("[SITAR] randomized inside the musical ranges");
        break;
    case Sitar::ExploreAction::Defaults:
        controls.restoreDefaults();
        publishAllLanes(controls);
        controls.stampExplore(Sitar::ExploreAction::Defaults, millis());
        Serial.println("[SITAR] back to sitar.h defaults");
        break;
    case Sitar::ExploreAction::Dump:
        printLegend(uiState);
        break;
    case Sitar::ExploreAction::Count:
        break;
    default:
        break;
    }
}

void applyButtonIntent(UIState &uiState, Sitar::ButtonIntent::Kind kind, uint32_t nowMs)
{
    Sitar::Controls &controls = uiState.sitar;
    switch (kind)
    {
    case Sitar::ButtonIntent::Kind::StrokeUp:
        applyStroke(controls, controls.strokeUp());
        break;
    case Sitar::ButtonIntent::Kind::StrokeDown:
        applyStroke(controls, controls.strokeDown());
        break;
    case Sitar::ButtonIntent::Kind::RagaNext:
        controls.ragaIndex = static_cast<uint8_t>((controls.ragaIndex + 1) % Sitar::kRagaCount);
        announceRaga(controls);
        break;
    case Sitar::ButtonIntent::Kind::RagaPrevious:
        controls.ragaIndex =
            static_cast<uint8_t>((controls.ragaIndex + Sitar::kRagaCount - 1) % Sitar::kRagaCount);
        announceRaga(controls);
        break;
    case Sitar::ButtonIntent::Kind::JhalaCycle:
        controls.jhala = static_cast<Sitar::JhalaPattern>(
            (static_cast<uint8_t>(controls.jhala) + 1) % Sitar::kJhalaCount);
        Serial.printf("[SITAR] jhala %s\n", Sitar::jhalaName(controls.jhala));
        break;
    case Sitar::ButtonIntent::Kind::TanpuraToggle:
        controls.tanpura = !controls.tanpura;
        Serial.printf("[SITAR] tanpura %s\n", controls.tanpura ? "on" : "off");
        break;
    case Sitar::ButtonIntent::Kind::FocusNext:
        controls.focus = Sitar::nextParam(controls.focus);
        Serial.printf("[SITAR] lane %u %s\n", static_cast<unsigned>(Sitar::paramIndex(controls.focus) + 1),
                      Sitar::paramInfo(controls.focus).name);
        break;
    case Sitar::ButtonIntent::Kind::FocusPrevious:
        controls.focus = Sitar::previousParam(controls.focus);
        Serial.printf("[SITAR] lane %u %s\n", static_cast<unsigned>(Sitar::paramIndex(controls.focus) + 1),
                      Sitar::paramInfo(controls.focus).name);
        break;
    case Sitar::ButtonIntent::Kind::Randomize:
        applyExplore(uiState, Sitar::ExploreAction::Randomize);
        break;
    case Sitar::ButtonIntent::Kind::Defaults:
        applyExplore(uiState, Sitar::ExploreAction::Defaults);
        break;
    case Sitar::ButtonIntent::Kind::Dump:
        applyExplore(uiState, Sitar::ExploreAction::Dump);
        break;
    case Sitar::ButtonIntent::Kind::Exit:
        UITransitions::exitSitar(uiState);
        onExited(uiState, nowMs);
        break;
    case Sitar::ButtonIntent::Kind::None:
        break;
    }
}
} // namespace

void begin()
{
    // Prepare on Core 0 at setup, the same way the voices are prepared, then
    // hang the instrument on the master bus: from here the master fader, the
    // master delay and the compressor hear the sitar like any other source.
    Sitar::instrument().prepare(SensorConstants::System::SAMPLE_RATE_HZ);
    if (voiceManager)
        voiceManager->setAuxiliaryInstrument(&Sitar::instrument());
    Serial.println("[SITAR] Sitar Explorer armed: Shift + Utility 5 (theme) enters the mode");
}

void onPadEvent(const MatrixButtonEvent &event, UIState &uiState, uint32_t nowMs)
{
    Sitar::Controls &controls = uiState.sitar;
    if (!controls.active)
        return;

    if (event.type == MATRIX_BUTTON_RELEASED)
    {
        controls.padReleased(event.buttonIndex, nowMs);
        return;
    }

    const Sitar::PadResult result = controls.padPressed(event.buttonIndex, nowMs);
    switch (result.kind)
    {
    case Sitar::PadResult::Kind::Pluck:
        Sitar::instrument().pluck(result.course, result.frequency, result.amplitude);
        break;
    case Sitar::PadResult::Kind::Slide:
        // A meend: no new excitation, the ringing string bends to the finger.
        Sitar::instrument().slide(result.course, result.frequency);
        break;
    case Sitar::PadResult::Kind::Chikari:
        Sitar::instrument().pluck(result.course, result.frequency, result.amplitude);
        if (result.secondCourse != Sitar::Course::Count)
            Sitar::instrument().pluck(result.secondCourse, result.secondFrequency, result.secondAmplitude);
        break;
    case Sitar::PadResult::Kind::Shimmer:
        Sitar::instrument().pluck(result.course, result.frequency, result.amplitude);
        break;
    case Sitar::PadResult::Kind::Damp:
        Sitar::instrument().dampAll();
        controls.stampStroke(Sitar::Stroke::Damp, nowMs);
        Serial.println("[SITAR] palms down");
        break;
    case Sitar::PadResult::Kind::Jhala:
        applyButtonIntent(uiState, Sitar::ButtonIntent::Kind::JhalaCycle, nowMs);
        break;
    case Sitar::PadResult::Kind::Tanpura:
        applyButtonIntent(uiState, Sitar::ButtonIntent::Kind::TanpuraToggle, nowMs);
        break;
    case Sitar::PadResult::Kind::Explore:
        applyExplore(uiState, result.explore);
        break;
    case Sitar::PadResult::Kind::None:
        break;
    }
}

void pollTiles(UIState &uiState, uint8_t buttons, uint8_t voices, uint32_t nowMs)
{
    if (!uiState.sitar.active)
        return;
    const Sitar::Controls::PollResult result =
        uiState.sitar.poll(buttons, voices, uiState.shiftHeld, nowMs);
    if (result.raga >= 0)
        announceRaga(uiState.sitar);
    applyButtonIntent(uiState, result.button.kind, nowMs);
}

void onFader(UIState &uiState, uint8_t channel, float normalized, uint32_t nowMs)
{
    Sitar::Controls &controls = uiState.sitar;
    if (!controls.active)
        return;
    const bool shift = uiState.shiftHeld;
    switch (channel)
    {
    case 0:
        // The master volume keeps its fader in every mode; Shift hands the same
        // fader to the jhala tempo, because the transport's clock is what drives
        // the drone.
        if (shift)
        {
            uClock.setTempo(ControlSurface::tempoForFader(normalized));
        }
        else if (voiceManager)
        {
            voiceManager->setGlobalVolume(normalized);
        }
        break;
    case 1:
    {
        const Sitar::Param lane = shift ? Sitar::Param::JawariContact : Sitar::Param::Jawari;
        controls.setValue(lane, normalized);
        publishLane(controls, lane);
        break;
    }
    case 2:
    {
        const Sitar::Param lane = shift ? Sitar::Param::TarafRing : Sitar::Param::TarafAmount;
        controls.setValue(lane, normalized);
        publishLane(controls, lane);
        break;
    }
    case 3:
        // The fourth fader is the exploration lane: whichever one the knob is
        // on. Shift puts the string's ring time under it instead.
    {
        const Sitar::Param lane = shift ? Sitar::Param::StringT60 : controls.focus;
        controls.setValue(lane, normalized);
        publishLane(controls, lane);
        break;
    }
    default:
        break;
    }
    (void)nowMs;
}

void observeHand(UIState &uiState, bool handPresent, float hand01)
{
    uiState.sitar.observeHand(handPresent, hand01);
}

void nudgeFocused(UIState &uiState, float travel)
{
    Sitar::Controls &controls = uiState.sitar;
    if (!controls.active)
        return;
    if (controls.nudgeFocused(travel))
        publishLane(controls, controls.focus);
}

void onClockStep(UIState &uiState, uint8_t sixteenthInBar)
{
    Sitar::Controls &controls = uiState.sitar;
    if (!controls.active)
        return;
    const Sitar::ClockStrokes strokes = controls.clockStep(sixteenthInBar, millis());
    for (uint8_t i = 0; i < strokes.count; ++i)
        Sitar::instrument().pluck(strokes.course[i], strokes.frequency[i], strokes.amplitude[i]);
}

void onEntered(UIState &uiState, uint32_t nowMs)
{
    // The model starts at sitar.h's own defaults, the strings are cleared so a
    // previous visit cannot ring under this one, and any encoder motion from the
    // sequencer modes is dropped so it cannot edit a lane the performer has not
    // even looked at yet.
    publishAllLanes(uiState.sitar);
    Sitar::instrument().dampAll();
    magEncoder.clearPendingTicks();
    announceRaga(uiState.sitar);
    Serial.printf("[SITAR] jhala %s tanpura %s\n", Sitar::jhalaName(uiState.sitar.jhala),
                  uiState.sitar.tanpura ? "on" : "off");
    printLegend(uiState);
    (void)nowMs;
}

void onExited(UIState &uiState, uint32_t nowMs)
{
    // Nothing is damped: the sitar rings on after the player lifts their hands,
    // and the mode has no note of its own to end. Pending encoder motion is
    // dropped so the knob cannot edit a sequencer lane on the way out.
    magEncoder.clearPendingTicks();
    Serial.println("[SITAR] left the mode; strings ring out");
    (void)uiState;
    (void)nowMs;
}

void toggleFromConsole(UIState &uiState, uint32_t nowMs)
{
    if (uiState.sitar.active)
    {
        UITransitions::exitSitar(uiState);
        onExited(uiState, nowMs);
        return;
    }
    UITransitions::enterSitar(uiState);
    onEntered(uiState, nowMs);
}

void printLegend(const UIState &uiState)
{
    const Sitar::Controls &controls = uiState.sitar;
    Serial.println();
    Serial.println("[SITAR] rpdsp::SitarStringVoice - every lane of sitar.h, on the panel");
    Serial.println("  pads 16-31 fingerboard (raga frets; slide across them = meend)");
    Serial.println("  pads  0-7  right hand (0 Pa, 1 Sa, 2 jod, 3 kharaj, 4 jhala, 5 tanpura, 6 shimmer, 7 palm)");
    Serial.println("  pads  8-15  explore (8 String, 9 Jawari, 10 Taraf, 11 Body, 12 Meend, 13 randomize, 14 defaults, 15 this legend)");
    Serial.println("  buttons 1 jod up, 2 jod down, 3 raga, 4 jhala, 5 next lane, 6 previous lane, 7 randomize");
    Serial.println("          7 held = sitar.h defaults, Shift+7 = leave the mode");
    Serial.println("  faders  1 volume (Shift: tempo) | 2 jawari (Shift: contact) | 3 taraf (Shift: ring) | 4 focused lane (Shift: T60)");
    Serial.println("  knob    edits the focused lane | voice buttons 1-4 pick the raga | hand height = pluck force");
    const Sitar::Raga &table = controls.currentRaga();
    Serial.printf("  raga %s (%s): vadi %s, samvadi %s, frets run %s to %s\n", table.name, table.thaat,
                  Sitar::kSargam[table.vadi], Sitar::kSargam[table.samvadi], Sitar::fretName(0),
                  Sitar::fretName(Sitar::kFretCount - 1));
    for (uint8_t index = 0; index < Sitar::kParamCount; ++index)
        printLaneLine(controls, static_cast<Sitar::Param>(index));
}

void renderLeds(LEDMatrix &ledMatrix, const UIState &uiState, uint32_t nowMs)
{
    const Sitar::Controls &controls = uiState.sitar;
    if (!controls.active)
        return;
    const Sitar::Raga &table = controls.currentRaga();

    // How alive the strings are right now: the taraf bank's bloom washes over
    // the whole panel, so the lights breathe with the tail, not with the pluck.
    float bloom = 0.0f;
    for (uint8_t course = 0; course < Sitar::kCourseCount; ++course)
        bloom = std::max(bloom, Sitar::instrument().courseLevel(static_cast<Sitar::Course>(course)));

    // Fingerboard: the raga's map, the held fret, and the bloom.
    for (uint8_t fret = 0; fret < Sitar::kFretCount; ++fret)
    {
        const uint8_t pad = static_cast<uint8_t>(Sitar::kFretFirstPad + fret);
        const uint8_t degree = Sitar::fretDegree(fret);
        // Vadi and samvadi are the notes the raga rests on, so they stay lit
        // even when nothing is sounding — the panel shows where home is.
        float base = 0.16f;
        if (degree == table.vadi)
            base = 0.5f;
        else if (degree == table.samvadi)
            base = 0.36f;
        CRGB color = blend(scaleColor(kFretPalette[degree], base), kBloom, bloom * 0.4f);
        const float hit = flashLevel(controls.fretStampMs[fret], nowMs, kFretFlashMs);
        const bool held = controls.fingerDown && controls.fingerFret == fret;
        color = blend(color, kFretHit, held ? 1.0f : hit);
        setPadLed(ledMatrix, pad, color);
    }

    // Right hand: strokes flash where they were struck (by a finger or by the
    // clock), and the drone pads glow with whatever is still ringing.
    for (uint8_t stroke = 0; stroke < Sitar::kStrokeCount; ++stroke)
    {
        const uint8_t pad = static_cast<uint8_t>(Sitar::kStrokeFirstPad + stroke);
        const bool string = stroke < 4; // 0-3 strike strings, 4-7 are the mode pads
        const float flash = flashLevel(controls.strokeStampMs[stroke], nowMs,
                                       string ? kStrokeFlashMs : kModeFlashMs);
        CRGB color = scaleColor(string ? kDroneIdle : kModeIdle, 1.0f);
        // The two clocked pads show their own state while nothing is touching
        // them: jhala is on, the tanpura pulse is armed.
        if (stroke == static_cast<uint8_t>(Sitar::Stroke::JhalaCycle) &&
            controls.jhala != Sitar::JhalaPattern::Off)
            color = scaleColor(kGroupActive, 0.45f);
        if (stroke == static_cast<uint8_t>(Sitar::Stroke::Tanpura) && controls.tanpura)
            color = scaleColor(kGroupActive, 0.45f);
        color = blend(color, string ? kStrokeHit : kGroupActive, flash);
        if (string)
            color = blend(color, kBloom, bloom * 0.35f);
        setPadLed(ledMatrix, pad, color);
    }

    // Exploration row: where the cursor is, and the pads that reshape every
    // lane at once.
    const Sitar::ParamGroup focusGroup = Sitar::groupOf(controls.focus);
    for (uint8_t action = 0; action < Sitar::kExploreCount; ++action)
    {
        const uint8_t pad = static_cast<uint8_t>(Sitar::kExploreFirstPad + action);
        const auto kind = static_cast<Sitar::ExploreAction>(action);
        const Sitar::ParamGroup group = Sitar::exploreGroup(kind);
        // A group pad burns bright while the cursor lives in its group; the
        // action pads sit dim until they fire.
        const bool focused = group != Sitar::ParamGroup::Count && group == focusGroup;
        CRGB color = focused ? kGroupActive : kGroupIdle;
        const float flash = flashLevel(controls.exploreStampMs[action], nowMs, kModeFlashMs);
        color = blend(color, kFretHit, flash);
        setPadLed(ledMatrix, pad, color);
    }
}

} // namespace Sitar::Performance
