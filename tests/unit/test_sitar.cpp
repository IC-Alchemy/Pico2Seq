#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "src/sitar/SitarControls.h"
#include "src/sitar/SitarInstrument.h"
#include "src/sitar/SitarParameters.h"
#include "src/sitar/SitarRagas.h"
#include "src/rpdsp/src/rpdsp/algorithm.h"
#include "src/rpdsp/src/rpdsp/sitar.h"
#include "src/voice/VoiceManager.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

// Sitar Explorer tests — the portable half of the mode plus the audio course
// harness. The firmware glue (LEDs, OLED, tiles, pads) is hardware-bound and
// stays out of the host suite; everything a reviewer can check without a panel
// is here.

using Catch::Approx;
namespace
{
constexpr float kSampleRate = 48000.0f;

// Renders `frames` samples of an instrument and returns peak and RMS.
struct RenderStats
{
    float peak = 0.0f;
    double rms = 0.0;
};

RenderStats render(Sitar::Instrument &instrument, uint32_t frames)
{
    // Block rendering, the way Core 1 pulls it (256-frame buffers).
    constexpr uint32_t kBlock = 256;
    std::vector<float> buffer(kBlock, 0.0f);
    RenderStats stats;
    double sum = 0.0;
    uint32_t rendered = 0;
    while (rendered < frames)
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        const uint32_t count = std::min<uint32_t>(kBlock, frames - rendered);
        instrument.renderAdd(buffer.data(), count);
        for (uint32_t i = 0; i < count; ++i)
        {
            stats.peak = std::max(stats.peak, std::fabs(buffer[i]));
            sum += static_cast<double>(buffer[i]) * buffer[i];
        }
        rendered += count;
    }
    stats.rms = rendered > 0 ? std::sqrt(sum / rendered) : 0.0;
    return stats;
}
} // namespace

TEST_CASE("Sitar ragas ascend in pitch and name their degrees", "[sitar][ragas]")
{
    for (uint8_t ragaIndex = 0; ragaIndex < Sitar::kRagaCount; ++ragaIndex)
    {
        const Sitar::Raga &table = Sitar::kRagas[ragaIndex];
        REQUIRE(table.name != nullptr);
        REQUIRE(table.vadi < 8);
        REQUIRE(table.samvadi < 8);
        REQUIRE(table.vadi != table.samvadi);
        // Sa is the tonic, the octave closes the aroh, and the frets never go
        // backwards — the fingerboard is played left to right, bottom to top.
        REQUIRE(Sitar::fretSemitones(table, 0) == 0);
        REQUIRE(Sitar::fretSemitones(table, 7) == 12);
        REQUIRE(Sitar::fretSemitones(table, 14) == 24);
        for (uint8_t fret = 1; fret < Sitar::kFretCount; ++fret)
            REQUIRE(Sitar::fretSemitones(table, fret) > Sitar::fretSemitones(table, fret - 1));
        // A raga has seven degrees; the eighth entry repeats the tonic an
        // octave up.
        for (uint8_t degree = 1; degree < 8; ++degree)
            REQUIRE(table.aroh[degree] > table.aroh[degree - 1]);
    }
    REQUIRE(std::string(Sitar::raga(0).name) == "Yaman");
    REQUIRE(std::string(Sitar::raga(Sitar::kRagaCount - 1).name) == "Khamaj");
    // Out-of-range indexes clamp to the first raga instead of reading past it.
    REQUIRE(std::string(Sitar::raga(200).name) == "Yaman");

    // Teevra Ma, the note that makes Yaman Yaman.
    REQUIRE(Sitar::kRagas[0].aroh[3] == 6);
    // Fret 9 is the raga's third degree an octave up.
    REQUIRE(std::string(Sitar::fretName(9)) == "Ga");
    REQUIRE(Sitar::fretDegree(14) == 0); // the tonic, two octaves up
}

TEST_CASE("Sitar fret frequencies agree with rpdsp's MIDI mapping", "[sitar][ragas]")
{
    Sitar::Controls controls;
    for (uint8_t ragaIndex = 0; ragaIndex < Sitar::kRagaCount; ++ragaIndex)
    {
        controls.ragaIndex = ragaIndex;
        for (uint8_t fret = 0; fret < Sitar::kFretCount; ++fret)
        {
            const int midiNote = Sitar::fretMidiNote(Sitar::kRagas[ragaIndex], fret);
            REQUIRE(controls.fretFrequency(fret) == Approx(rpdsp::midiNoteToHz(midiNote)).epsilon(1.0e-4));
        }
    }
}

TEST_CASE("Sitar lane table covers every sitar.h setter with a usable range", "[sitar][params]")
{
    REQUIRE(Sitar::kParamCount == 13);
    for (uint8_t index = 0; index < Sitar::kParamCount; ++index)
    {
        const auto id = static_cast<Sitar::Param>(index);
        const Sitar::ParamInfo &info = Sitar::paramInfo(id);
        REQUIRE(info.name != nullptr);
        REQUIRE(info.setter != nullptr);
        REQUIRE(std::string(info.setter).rfind("set", 0) == 0);
        REQUIRE(info.max > info.min);
        REQUIRE(info.defaultValue >= info.min);
        REQUIRE(info.defaultValue <= info.max);
        // The randomize pad must stay inside the slice a sitarist would set.
        REQUIRE(info.musicalMin >= info.min);
        REQUIRE(info.musicalMax <= info.max);
        REQUIRE(info.musicalMin < info.musicalMax);
        REQUIRE(Sitar::engineeringValue(id, 0.0f) == Approx(info.min).epsilon(1.0e-5));
        REQUIRE(Sitar::engineeringValue(id, 1.0f) == Approx(info.max).epsilon(1.0e-5));
        // Clamping, not wrapping: a fader parked at an end stays at the end.
        REQUIRE(Sitar::engineeringValue(id, -3.0f) == Approx(info.min));
        REQUIRE(Sitar::engineeringValue(id, 7.0f) == Approx(info.max));
        // Travel and value round-trip, so the default the table documents is
        // exactly the default sitar.h ships.
        const float travel = Sitar::normalizedValue(id, info.defaultValue);
        REQUIRE(Sitar::engineeringValue(id, travel) ==
                Approx(info.defaultValue).epsilon(1.0e-4));
    }

    // Browse order walks all thirteen lanes and wraps both ways.
    Sitar::Param cursor = Sitar::Param::StringT60;
    for (uint8_t step = 0; step < Sitar::kParamCount; ++step)
        cursor = Sitar::nextParam(cursor);
    REQUIRE(cursor == Sitar::Param::StringT60);
    REQUIRE(Sitar::previousParam(Sitar::Param::StringT60) == Sitar::Param::Meend);

    // Every group has a first lane and the groups partition the table.
    uint8_t visited = 0;
    for (uint8_t group = 0; group < Sitar::kGroupCount; ++group)
    {
        const auto wanted = static_cast<Sitar::ParamGroup>(group);
        const Sitar::Param first = Sitar::firstParamOf(wanted);
        REQUIRE(Sitar::groupOf(first) == wanted);
        for (uint8_t index = 0; index < Sitar::kParamCount; ++index)
            if (Sitar::groupOf(static_cast<Sitar::Param>(index)) == wanted)
                ++visited;
    }
    REQUIRE(visited == Sitar::kParamCount);
}

TEST_CASE("Sitar lane values format with units", "[sitar][params]")
{
    char text[32] = {};
    Sitar::formatParamValue(Sitar::Param::BodyTone, 0.0f, text, sizeof(text));
    REQUIRE(std::string(text) == "50.00 Hz");
    Sitar::formatParamValue(Sitar::Param::Jawari, 0.5f, text, sizeof(text));
    REQUIRE(std::string(text) == "0.50");
    Sitar::formatParamPosition(Sitar::Param::Jawari, text, sizeof(text));
    REQUIRE(std::string(text) == "7/13");
    REQUIRE(std::string(Sitar::groupName(Sitar::ParamGroup::Taraf)) == "Taraf");
}

TEST_CASE("Sitar mode swallows the entry chord and then plays the fingerboard", "[sitar][controls]")
{
    Sitar::Controls controls;
    controls.enter();
    REQUIRE(controls.active);

    // The fingers that opened the mode must leave first.
    REQUIRE(controls.padPressed(Sitar::kFretFirstPad, 0).kind == Sitar::PadResult::Kind::None);
    controls.waitRelease = false;

    // Landing on a fret plucks; travelling sideways bends the ringing string.
    const Sitar::PadResult pluck = controls.padPressed(Sitar::kFretFirstPad + 4, 10);
    REQUIRE(pluck.kind == Sitar::PadResult::Kind::Pluck);
    REQUIRE(pluck.fret == 4);
    REQUIRE(pluck.frequency == Approx(controls.fretFrequency(4)));
    REQUIRE(pluck.course == Sitar::Course::Main);

    const Sitar::PadResult meend = controls.padPressed(Sitar::kFretFirstPad + 11, 20);
    REQUIRE(meend.kind == Sitar::PadResult::Kind::Slide);
    REQUIRE(meend.frequency > pluck.frequency);

    // Lifting the finger ends the gesture, not the note: nothing to command.
    REQUIRE(controls.padReleased(Sitar::kFretFirstPad + 11, 30).kind == Sitar::PadResult::Kind::None);
    REQUIRE(controls.fingerDown == false);
    REQUIRE(controls.active);
    REQUIRE(controls.padPressed(Sitar::kFretFirstPad + 12, 40).kind == Sitar::PadResult::Kind::Pluck);
}

TEST_CASE("Sitar stroke row strikes the drone strings and the mode pads", "[sitar][controls]")
{
    Sitar::Controls controls;
    controls.enter();
    controls.waitRelease = false;
    controls.observeHand(false, 0.0f);

    const Sitar::PadResult pa = controls.padPressed(0, 1);
    REQUIRE(pa.kind == Sitar::PadResult::Kind::Chikari);
    REQUIRE(pa.course == Sitar::Course::ChikariLow);
    REQUIRE(pa.frequency == Approx(rpdsp::midiNoteToHz(Sitar::kSaMidiNote + Sitar::kChikariPaSemitones)));
    REQUIRE(pa.secondCourse == Sitar::Course::Count);

    const Sitar::PadResult jod = controls.padPressed(2, 2);
    REQUIRE(jod.kind == Sitar::PadResult::Kind::Chikari);
    REQUIRE(jod.course == Sitar::Course::ChikariLow);
    REQUIRE(jod.secondCourse == Sitar::Course::ChikariHigh);
    REQUIRE(jod.secondFrequency > jod.frequency);

    const Sitar::PadResult bass = controls.padPressed(3, 3);
    REQUIRE(bass.course == Sitar::Course::Kharaj);
    REQUIRE(bass.frequency < Sitar::kSaMidiNote); // two octaves under Sa

    // A shimmer wakes the taraf bank without retriggering the melody string.
    const Sitar::PadResult shimmer = controls.padPressed(6, 4);
    REQUIRE(shimmer.kind == Sitar::PadResult::Kind::Shimmer);
    REQUIRE(shimmer.course == Sitar::Course::Main);
    REQUIRE(shimmer.amplitude < 0.1f);

    REQUIRE(controls.padPressed(4, 5).kind == Sitar::PadResult::Kind::Jhala);
    REQUIRE(controls.padPressed(5, 6).kind == Sitar::PadResult::Kind::Tanpura);
    REQUIRE(controls.padPressed(7, 7).kind == Sitar::PadResult::Kind::Damp);
    REQUIRE(controls.padPressed(8, 8).kind == Sitar::PadResult::Kind::Explore);
}

TEST_CASE("Sitar exploration row jumps groups and reshapes the lanes", "[sitar][controls]")
{
    Sitar::Controls controls;
    controls.enter();
    controls.waitRelease = false;

    // Group jump pads focus the first lane of their group.
    const Sitar::PadResult taraf = controls.padPressed(Sitar::kExploreFirstPad + 2, 1);
    REQUIRE(taraf.kind == Sitar::PadResult::Kind::Explore);
    REQUIRE(taraf.explore == Sitar::ExploreAction::GroupTaraf);
    controls.setGroup(Sitar::exploreGroup(taraf.explore));
    REQUIRE(controls.focus == Sitar::Param::TarafAmount);
    REQUIRE(Sitar::groupOf(controls.focus) == Sitar::ParamGroup::Taraf);

    // The knob moves the focused lane, then clamps at the ends.
    const float before = controls.value[Sitar::paramIndex(controls.focus)];
    REQUIRE(controls.nudgeFocused(-0.05f));
    REQUIRE(controls.value[Sitar::paramIndex(controls.focus)] < before);
    for (int turn = 0; turn < 40; ++turn)
        controls.nudgeFocused(0.1f);
    REQUIRE(controls.value[Sitar::paramIndex(controls.focus)] == 1.0f);
    // Moving past the end is reported as "nothing changed", so the glue does
    // not republish a lane that is already parked.
    REQUIRE_FALSE(controls.nudgeFocused(0.1f));

    // Faders (and the knob) share one truth: a lane set from outside lands in
    // the same place the UI reads.
    controls.setValue(Sitar::Param::Meend, 0.25f);
    REQUIRE(controls.value[Sitar::paramIndex(Sitar::Param::Meend)] == Approx(0.25f));

    // Randomize lands inside the musical slice and defaults restore sitar.h.
    controls.randomize();
    for (uint8_t index = 0; index < Sitar::kParamCount; ++index)
    {
        const auto id = static_cast<Sitar::Param>(index);
        const Sitar::ParamInfo &info = Sitar::paramInfo(id);
        const float value = Sitar::engineeringValue(id, controls.value[index]);
        REQUIRE(value >= Approx(info.musicalMin).epsilon(1.0e-3));
        REQUIRE(value <= Approx(info.musicalMax).epsilon(1.0e-3));
    }
    controls.restoreDefaults();
    for (uint8_t index = 0; index < Sitar::kParamCount; ++index)
    {
        const auto id = static_cast<Sitar::Param>(index);
        REQUIRE(Sitar::engineeringValue(id, controls.value[index]) ==
                Approx(Sitar::paramInfo(id).defaultValue).epsilon(1.0e-4));
    }
}

TEST_CASE("Sitar buttons and the randomize hold carry the mode's gestures", "[sitar][controls]")
{
    Sitar::Controls controls;
    controls.enter();
    controls.waitRelease = false;

    REQUIRE(controls.buttonPressed(0, false).kind == Sitar::ButtonIntent::Kind::StrokeUp);
    REQUIRE(controls.buttonPressed(1, false).kind == Sitar::ButtonIntent::Kind::StrokeDown);
    REQUIRE(controls.buttonPressed(2, false).kind == Sitar::ButtonIntent::Kind::RagaNext);
    REQUIRE(controls.buttonPressed(2, true).kind == Sitar::ButtonIntent::Kind::RagaPrevious);
    REQUIRE(controls.buttonPressed(3, false).kind == Sitar::ButtonIntent::Kind::JhalaCycle);
    REQUIRE(controls.buttonPressed(3, true).kind == Sitar::ButtonIntent::Kind::TanpuraToggle);
    REQUIRE(controls.buttonPressed(4, false).kind == Sitar::ButtonIntent::Kind::FocusNext);
    REQUIRE(controls.buttonPressed(5, false).kind == Sitar::ButtonIntent::Kind::FocusPrevious);
    REQUIRE(controls.buttonPressed(5, true).kind == Sitar::ButtonIntent::Kind::Defaults);
    REQUIRE(controls.buttonPressed(6, false).kind == Sitar::ButtonIntent::Kind::Randomize);
    // Leaving the mode is deliberate: Shift + randomize, never a bare tap.
    REQUIRE(controls.buttonPressed(6, true).kind == Sitar::ButtonIntent::Kind::Exit);

    // Tile polling: edges, waitRelease and the raga pick in one call.
    Sitar::Controls tiled;
    tiled.enter();
    // Entry chord still held: nothing fires until the tiles read clear.
    REQUIRE(tiled.poll(0x80u | (1u << 4), 0, true, 0).button.kind == Sitar::ButtonIntent::Kind::None);
    REQUIRE(tiled.poll(0, 0, false, 10).button.kind == Sitar::ButtonIntent::Kind::None);
    REQUIRE_FALSE(tiled.waitRelease);
    // Tapping button 4 walks the sitar.h lanes.
    REQUIRE(tiled.poll(1u << 4, 0, false, 20).button.kind == Sitar::ButtonIntent::Kind::FocusNext);
    // Holding it does not repeat the action; only the randomize hold resets.
    REQUIRE(tiled.poll(1u << 4, 0, false, 900).button.kind == Sitar::ButtonIntent::Kind::None);
    // Randomize fires on the tap, then a continued hold restores sitar.h's
    // defaults 700 ms later — the tap and the wipe never share a pass.
    REQUIRE(tiled.poll(1u << 6, 0, false, 1000).button.kind == Sitar::ButtonIntent::Kind::Randomize);
    REQUIRE(tiled.poll(1u << 6, 0, false, 1500).button.kind == Sitar::ButtonIntent::Kind::None);
    REQUIRE(tiled.poll(1u << 6, 0, false, 2100).button.kind == Sitar::ButtonIntent::Kind::None);
    REQUIRE(tiled.poll(1u << 6, 0, false, 2250).button.kind == Sitar::ButtonIntent::Kind::Defaults);
    // Voice buttons choose ragas.
    const Sitar::Controls::PollResult raga = tiled.poll(0, 1u << 2, false, 2300);
    REQUIRE(raga.raga == 2);
    REQUIRE(tiled.ragaIndex == 2);

    // A hold on the randomize bit restores sitar.h's defaults after 700 ms.
    REQUIRE(controls.pollHeld(1u << 6, 1000).kind == Sitar::ButtonIntent::Kind::None);
    REQUIRE(controls.pollHeld(1u << 6, 1500).kind == Sitar::ButtonIntent::Kind::None);
    REQUIRE(controls.pollHeld(1u << 6, 1700).kind == Sitar::ButtonIntent::Kind::Defaults);
    // Released: the hold disarms, so the next press starts a fresh 700 ms.
    REQUIRE(controls.pollHeld(0, 1800).kind == Sitar::ButtonIntent::Kind::None);
    REQUIRE(controls.pollHeld(1u << 6, 2000).kind == Sitar::ButtonIntent::Kind::None);
    REQUIRE(controls.pollHeld(1u << 6, 2800).kind == Sitar::ButtonIntent::Kind::Defaults);
}

TEST_CASE("Sitar jod strokes travel the whole fingerboard", "[sitar][controls]")
{
    Sitar::Controls controls;
    controls.enter();
    controls.waitRelease = false;
    controls.observeHand(false, 0.0f);

    const Sitar::StrokeRequest up = controls.strokeUp();
    REQUIRE(up.pluck);
    REQUIRE(up.slide);
    REQUIRE(up.frequency == Approx(controls.fretFrequency(Sitar::kFretCount - 1)));
    REQUIRE(up.amplitude > 0.5f);

    const Sitar::StrokeRequest down = controls.strokeDown();
    REQUIRE_FALSE(down.pluck); // the string is already ringing
    REQUIRE(down.slide);
    REQUIRE(down.frequency == Approx(controls.fretFrequency(0)));
    REQUIRE(down.frequency < up.frequency);

    // Hand height is force: a low hand plays softer, no hand plays firm.
    controls.observeHand(true, 0.0f);
    REQUIRE(controls.pluckForce() == Approx(0.3f));
    controls.observeHand(true, 1.0f);
    REQUIRE(controls.pluckForce() == Approx(1.0f));
    controls.observeHand(false, 1.0f);
    REQUIRE(controls.pluckForce() == Approx(0.85f));
}

TEST_CASE("Sitar jhala and tanpura follow the bar's sixteenths", "[sitar][controls]")
{
    Sitar::Controls controls;
    controls.enter();
    controls.waitRelease = false;
    controls.observeHand(false, 0.0f);

    auto strokesInBar = [&controls](Sitar::JhalaPattern pattern, bool tanpura) {
        controls.jhala = pattern;
        controls.tanpura = tanpura;
        controls.lastClockSlot = 0xFF;
        unsigned total = 0;
        unsigned sixteenthsWithStroke = 0;
        for (uint8_t slot = 0; slot < 16; ++slot)
        {
            const Sitar::ClockStrokes strokes = controls.clockStep(slot, 1000u + slot * 125u);
            total += strokes.count;
            if (strokes.count > 0)
                ++sixteenthsWithStroke;
            for (uint8_t i = 0; i < strokes.count; ++i)
                REQUIRE(strokes.frequency[i] > 0.0f);
        }
        return std::pair<unsigned, unsigned>(total, sixteenthsWithStroke);
    };

    REQUIRE(strokesInBar(Sitar::JhalaPattern::Off, false) == std::pair<unsigned, unsigned>(0, 0));
    // Eighths strike one chikari on every other sixteenth.
    REQUIRE(strokesInBar(Sitar::JhalaPattern::Eighths, false) == std::pair<unsigned, unsigned>(8, 8));
    // Sixteenths double-stroke both chikari strings.
    REQUIRE(strokesInBar(Sitar::JhalaPattern::Sixteenths, false) == std::pair<unsigned, unsigned>(32, 16));
    // Jhala doubles the pair only on the four accents (12 single + 4 double =
    // 20), and the tanpura adds one bass stroke on the downbeat.
    REQUIRE(strokesInBar(Sitar::JhalaPattern::Jhala, true) == std::pair<unsigned, unsigned>(21, 16));

    // A step number delivered twice must not double-strike (and the tanpura is
    // off here, so the downbeat carries exactly one chikari stroke).
    controls.jhala = Sitar::JhalaPattern::Eighths;
    controls.tanpura = false;
    controls.lastClockSlot = 0xFF;
    REQUIRE(controls.clockStep(0, 5000).count == 1);
    REQUIRE(controls.clockStep(0, 5000).count == 0);

    // Leaving the mode stops the drone but leaves the strings ringing.
    controls.exit();
    REQUIRE_FALSE(controls.active);
    controls.lastClockSlot = 0xFF;
    REQUIRE(controls.clockStep(4, 5000).count == 0);
}

TEST_CASE("Sitar courses sound, ring, glide and damp", "[sitar][instrument]")
{
    Sitar::Instrument &instrument = Sitar::instrument();
    instrument.prepare(kSampleRate);
    instrument.dampAll();
    // 256 samples of audio let the queued damp events land before measuring.
    REQUIRE(render(instrument, 256).peak == 0.0f);

    REQUIRE(instrument.pluck(Sitar::Course::Main, 220.0f, 0.9f));
    const RenderStats plucked = render(instrument, 4800);
    REQUIRE(plucked.peak > 0.02f);
    REQUIRE(instrument.courseLevel(Sitar::Course::Main) > 0.0f);
    // The other courses were not struck.
    REQUIRE(instrument.courseLevel(Sitar::Course::ChikariLow) == Approx(0.0f));

    // The envelope decays once the string is left alone.
    render(instrument, 24000);
    REQUIRE(Sitar::instrument().courseLevel(Sitar::Course::Main) <
            plucked.peak);

    // A meend retunes the ringing string instead of plucking it again: the
    // peak stays continuous (no fresh excitation).
    REQUIRE(instrument.slide(Sitar::Course::Main, 330.0f));
    const RenderStats glided = render(instrument, 4800);
    REQUIRE(glided.peak <= plucked.peak * 1.05f);

    // Damping silences the instrument again.
    instrument.dampAll();
    render(instrument, 4800);
    const RenderStats damped = render(instrument, 4800);
    REQUIRE(damped.peak < 0.001f);
}

TEST_CASE("Sitar drone courses ring independently of the melody string", "[sitar][instrument]")
{
    Sitar::Instrument &instrument = Sitar::instrument();
    instrument.prepare(kSampleRate);
    instrument.dampAll();
    render(instrument, 512);

    REQUIRE(instrument.pluck(Sitar::Course::Main, 262.0f, 0.8f));
    REQUIRE(instrument.pluck(Sitar::Course::Kharaj, 65.4f, 0.9f));
    const RenderStats both = render(instrument, 4800);
    REQUIRE(both.peak > 0.02f);
    REQUIRE(instrument.courseLevel(Sitar::Course::Main) > 0.0f);
    REQUIRE(instrument.courseLevel(Sitar::Course::Kharaj) > 0.0f);

    // Plucking the melody string again must not cut the bass: separate courses.
    REQUIRE(instrument.pluck(Sitar::Course::Main, 330.0f, 0.8f));
    render(instrument, 1024);
    REQUIRE(instrument.courseLevel(Sitar::Course::Kharaj) > 0.0f);
}

TEST_CASE("Sitar lane targets change what the strings do", "[sitar][instrument]")
{
    Sitar::Instrument &instrument = Sitar::instrument();
    instrument.prepare(kSampleRate);

    auto renderPluck = [&instrument](float jawari) {
        instrument.setParameter(Sitar::Param::Jawari, jawari);
        instrument.setParameter(Sitar::Param::TarafAmount, 0.0f);
        instrument.setParameter(Sitar::Param::BodyAmount, 0.0f);
        instrument.dampAll();
        render(instrument, 1024);
        instrument.pluck(Sitar::Course::Main, 196.0f, 1.0f);
        return render(instrument, 9600);
    };

    // Clean string vs. aggressive bridge buzz: same pluck, different tone.
    const RenderStats clean = renderPluck(0.0f);
    const RenderStats buzzing = renderPluck(1.0f);
    REQUIRE(clean.peak > 0.01f);
    REQUIRE(buzzing.peak > 0.01f);
    REQUIRE(std::fabs(clean.rms - buzzing.rms) > 1.0e-4);

    // A silent taraf bank really is silent, and an open one is not: the two
    // lanes the mode advertises are wired to the model.
    instrument.setParameter(Sitar::Param::TarafAmount, 0.0f);
    instrument.dampAll();
    render(instrument, 1024);
    instrument.pluck(Sitar::Course::Main, 196.0f, 1.0f);
    render(instrument, 19200);
    const float quietTail = instrument.courseLevel(Sitar::Course::Main);
    instrument.setParameter(Sitar::Param::TarafAmount, 0.9f);
    instrument.setParameter(Sitar::Param::TarafRing, 0.9f);
    instrument.dampAll();
    render(instrument, 1024);
    instrument.pluck(Sitar::Course::Main, 196.0f, 1.0f);
    render(instrument, 19200);
    REQUIRE(instrument.courseLevel(Sitar::Course::Main) > quietTail);
}

TEST_CASE("A full event queue drops gestures instead of stalling the control core", "[sitar][instrument]")
{
    Sitar::Instrument &instrument = Sitar::instrument();
    instrument.prepare(kSampleRate);
    // Nothing drains the queue here, so the capacity is measurable.
    size_t accepted = 0;
    while (instrument.pluck(Sitar::Course::Main, 220.0f, 0.5f))
        ++accepted;
    REQUIRE(accepted == Sitar::Instrument::kEventCapacity);
    // One block of audio drains a bounded number, never the whole backlog.
    std::vector<float> buffer(256, 0.0f);
    instrument.renderAdd(buffer.data(), 256);
    REQUIRE(instrument.pluck(Sitar::Course::Main, 220.0f, 0.5f));
}

TEST_CASE("The auxiliary hook lands the sitar on the master bus", "[sitar][bus]")
{
    // Two identical sitar instruments (the model's RNG seeds are fixed, so the
    // same gestures render the same audio) on two master buses: the only
    // difference is the master fader. That is what proves the sitar goes
    // through the master bus rather than around it.
    static Sitar::Instrument loudInstrument;
    static Sitar::Instrument quietInstrument;
    loudInstrument.prepare(kSampleRate);
    quietInstrument.prepare(kSampleRate);

    VoiceManager loudBus(4);
    VoiceManager quietBus(4);
    loudBus.init(kSampleRate);
    quietBus.init(kSampleRate);
    loudBus.setAuxiliaryInstrument(&loudInstrument);
    quietBus.setAuxiliaryInstrument(&quietInstrument);
    REQUIRE(loudBus.auxiliaryInstrument() == &loudInstrument);
    loudBus.setGlobalVolume(1.0f);
    quietBus.setGlobalVolume(0.25f);

    // No voices added: whatever comes out of the bus is the sitar.
    auto renderPluck = [](VoiceManager &bus, Sitar::Instrument &instrument) {
        instrument.dampAll();
        std::vector<float> buffer(256, 0.0f);
        // Let the bus's master-gain smoother settle on the new fader position
        // before the pluck; otherwise the measurement is the fade, not the
        // fader.
        for (int block = 0; block < 64; ++block)
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            bus.processBlock(buffer.data(), 256);
        }
        instrument.pluck(Sitar::Course::Main, 220.0f, 0.9f);
        float peak = 0.0f;
        for (int block = 0; block < 8; ++block)
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            bus.processBlock(buffer.data(), 256);
            for (float sample : buffer)
                peak = std::max(peak, std::fabs(sample));
        }
        return peak;
    };

    const float loud = renderPluck(loudBus, loudInstrument);
    const float quiet = renderPluck(quietBus, quietInstrument);
    REQUIRE(loud > 0.01f);
    REQUIRE(quiet > 0.0f);
    // The pluck happens after the gain has settled, and the master compressor
    // squeezes the last of the ratio away, so this is a range check rather than
    // an equality: a quarter of the master volume must be clearly quieter.
    REQUIRE(quiet < loud * 0.4f);

    // Detaching leaves the bus without an instrument.
    std::vector<float> buffer(256, 0.0f);
    loudBus.setAuxiliaryInstrument(nullptr);
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    loudBus.processBlock(buffer.data(), 256);
    for (float sample : buffer)
        REQUIRE(sample == 0.0f);
}
