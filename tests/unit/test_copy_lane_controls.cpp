#include <catch2/catch_test_macros.hpp>

#include "ui/CopyLaneControls.h"
#include "ui/UITransitions.h"

#include <cstring>

// COPY LANE gesture policy: a voice button + a lane button copies (mode off) or
// pastes (mode on); Shift leaves. Pure edge logic, driven pass by pass like the bridge.

namespace
{
using CopyLane::Action;
using CopyLane::Controls;
using CopyLane::Input;

constexpr uint8_t kShift = Controls::kShift;
constexpr uint8_t lane(uint8_t key) { return static_cast<uint8_t>(1u << key); }  // ButtonModule8 bit
constexpr uint8_t voice(uint8_t v) { return static_cast<uint8_t>(1u << v); }     // SliderModule button

struct Rig
{
    Controls controls;
    uint32_t now = 1000;
    bool allowed = true;

    Input pass(uint8_t buttons, uint8_t voices, uint32_t advanceMs = 5)
    {
        now += advanceMs;
        return controls.poll(buttons, voices, allowed, now);
    }
    // What the bridge does with a Copy: fill the memory, switch the mode on.
    void turnOn(uint8_t sourceVoice = 0, ParamId lane = ParamId::Filter)
    {
        controls.clip.lane = lane;
        controls.clip.length = 16;
        controls.begin(sourceVoice);
    }
};
} // namespace

TEST_CASE("voice held first, then a lane button, copies that voice's lane", "[copy_lane][control_surface]")
{
    Rig rig;
    CHECK(rig.pass(0, voice(2)).action == Action::None); // voice alone: just a selection
    const Input chord = rig.pass(lane(2), voice(2));
    CHECK(chord.action == Action::Copy);
    CHECK(chord.voice == 2);
    CHECK(chord.laneKey == 2);
    CHECK(chord.claimed == lane(2));
    CHECK_FALSE(chord.undoLanePress);

    // Holding on, releasing, nothing more: the chord fires once per lane press.
    CHECK(rig.pass(lane(2), voice(2)).action == Action::None);
    CHECK(rig.pass(0, voice(2)).action == Action::None);
    CHECK(rig.pass(0, 0).action == Action::None);
}

TEST_CASE("every lane button chords with every voice button", "[copy_lane][control_surface]")
{
    for (uint8_t v = 0; v < 4; ++v)
    for (uint8_t key = 0; key < CopyLane::kLaneKeys; ++key)
    {
        CAPTURE(int(v), int(key));
        Rig rig;
        rig.pass(0, voice(v));
        const Input chord = rig.pass(lane(key), voice(v));
        CHECK(chord.action == Action::Copy);
        CHECK(chord.voice == v);
        CHECK(chord.laneKey == key);
    }
}

TEST_CASE("pressing both in the same pass is a chord", "[copy_lane][control_surface]")
{
    Rig rig;
    const Input chord = rig.pass(lane(4), voice(1));
    CHECK(chord.action == Action::Copy);
    CHECK(chord.voice == 1);
    CHECK(chord.laneKey == 4);
    CHECK(chord.claimed == lane(4));
}

TEST_CASE("with copy mode on, the same chord pastes - as often as wanted, anywhere", "[copy_lane][control_surface]")
{
    Rig rig;
    rig.turnOn(0, ParamId::Filter);
    REQUIRE(rig.controls.active);

    // Hold V2, press a lane, release the lane, press it again: two pastes.
    rig.pass(0, voice(1));
    CHECK(rig.pass(lane(2), voice(1)).action == Action::Paste);
    CHECK(rig.pass(0, voice(1)).action == Action::None);
    CHECK(rig.pass(lane(2), voice(1)).action == Action::Paste);

    // Another lane while the voice is still held, then another voice.
    const Input otherLane = rig.pass(lane(2) | lane(5), voice(1));
    CHECK(otherLane.action == Action::Paste);
    CHECK(otherLane.laneKey == 5);
    CHECK(rig.pass(0, 0).action == Action::None);
    rig.pass(0, voice(3));
    const Input otherVoice = rig.pass(lane(0), voice(3));
    CHECK(otherVoice.action == Action::Paste);
    CHECK(otherVoice.voice == 3);
    CHECK(otherVoice.laneKey == 0);

    // The memory survived every paste.
    CHECK(rig.controls.active);
    CHECK(rig.controls.clip.valid());
}

TEST_CASE("releasing every button leaves the memory in place", "[copy_lane][control_surface]")
{
    Rig rig;
    rig.pass(0, voice(0));
    REQUIRE(rig.pass(lane(1), voice(0)).action == Action::Copy);
    rig.controls.clip.lane = ParamId::Velocity;
    rig.controls.clip.length = 9;
    rig.controls.begin(0);
    for (int i = 0; i < 50; ++i)
        CHECK(rig.pass(0, 0, 100).action == Action::None);
    CHECK(rig.controls.active);
    CHECK(rig.controls.clip.lane == ParamId::Velocity);
    CHECK(rig.controls.clip.length == 9);
}

TEST_CASE("Shift leaves copy mode and forgets the memory", "[copy_lane][control_surface]")
{
    Rig rig;
    rig.turnOn(1, ParamId::Attack);
    CHECK(rig.pass(kShift, 0).action == Action::Exit);
    CHECK_FALSE(rig.controls.active);
    CHECK_FALSE(rig.controls.clip.valid());
    CHECK_FALSE(rig.controls.pasteNoticeShowing(rig.now));

    // With copy mode off a Shift press means nothing to this gesture.
    CHECK(rig.pass(0, 0).action == Action::None);
    CHECK(rig.pass(kShift, 0).action == Action::None);
}

TEST_CASE("Shift exits even when the lane buttons are off limits", "[copy_lane][control_surface]")
{
    Rig rig;
    rig.turnOn();
    rig.allowed = false;
    CHECK(rig.pass(kShift, 0).action == Action::Exit);
    CHECK_FALSE(rig.controls.active);
}

TEST_CASE("a chord pressed with Shift down belongs to the Shift chords", "[copy_lane][control_surface]")
{
    Rig rig;
    rig.pass(kShift, 0); // Shift is down before anything else
    CHECK(rig.pass(kShift | lane(5), voice(0)).action == Action::None); // Shift + 6 + voice = ADSR page
    CHECK(rig.pass(kShift | lane(2), voice(0)).action == Action::None);

    // And once Shift is up again a fresh press chords as usual.
    rig.pass(0, voice(0));
    CHECK(rig.pass(lane(2), voice(0)).action == Action::Copy);
}

TEST_CASE("Slide and Shift are not lanes", "[copy_lane][control_surface]")
{
    Rig rig;
    rig.pass(0, voice(0));
    CHECK(rig.pass(1u << 6, voice(0)).action == Action::None); // button 7, Slide
    CHECK(rig.pass(0, voice(0)).action == Action::None);
}

TEST_CASE("nothing chords while the lane buttons mean something else", "[copy_lane][control_surface]")
{
    Rig rig;
    rig.allowed = false; // Utility mode, Arpeggiator, Settings, or a page is open
    rig.pass(0, voice(0));
    CHECK(rig.pass(lane(2), voice(0)).action == Action::None);

    // A lane pressed while off limits does not wait for a voice afterwards.
    rig.pass(0, 0);
    rig.pass(lane(1), 0);
    rig.allowed = true;
    CHECK(rig.pass(lane(1), voice(1), 10).action == Action::None);
}

TEST_CASE("a lane pressed first still chords if the voice follows at once", "[copy_lane][control_surface]")
{
    Rig rig;
    CHECK(rig.pass(lane(3), 0).action == Action::None); // armed as usual so far
    const Input chord = rig.pass(lane(3), voice(2), 30);
    CHECK(chord.action == Action::Copy);
    CHECK(chord.voice == 2);
    CHECK(chord.laneKey == 3);
    CHECK(chord.undoLanePress);     // the bridge takes the arm back
    CHECK(chord.claimed == 0);      // that press edge went by in an earlier pass

    SECTION("and pastes in copy mode")
    {
        Rig second;
        second.turnOn();
        second.pass(lane(0), 0);
        const Input paste = second.pass(lane(0), voice(3), CopyLane::kChordWindowMs);
        CHECK(paste.action == Action::Paste);
        CHECK(paste.voice == 3);
        CHECK(paste.undoLanePress);
    }
}

TEST_CASE("a lane held to record and a voice tapped later is not a chord", "[copy_lane][control_surface]")
{
    Rig rig;
    rig.pass(lane(1), 0);
    CHECK(rig.pass(lane(1), voice(2), CopyLane::kChordWindowMs + 1).action == Action::None);
    CHECK(rig.pass(lane(1), 0).action == Action::None);
    // Pressing the voice again, still long after the lane went down, is no chord either.
    CHECK(rig.pass(lane(1), voice(2)).action == Action::None);
}

TEST_CASE("a lane released before the voice lands is not a chord", "[copy_lane][control_surface]")
{
    Rig rig;
    rig.pass(lane(0), 0);
    rig.pass(0, 0, 5);
    CHECK(rig.pass(0, voice(1), 5).action == Action::None);
}

TEST_CASE("the lane-first window is spent by one voice press", "[copy_lane][control_surface]")
{
    Rig rig;
    rig.turnOn();
    rig.pass(lane(4), 0);
    REQUIRE(rig.pass(lane(4), voice(0), 10).action == Action::Paste);
    // A second voice shortly after, with the lane still down, is not a second chord.
    CHECK(rig.pass(lane(4), voice(0) | voice(1), 10).action == Action::None);
}

TEST_CASE("the newest voice button decides when several are down", "[copy_lane][control_surface]")
{
    Rig rig;
    rig.pass(0, voice(0));
    rig.pass(0, voice(0) | voice(2));
    CHECK(rig.pass(lane(1), voice(0) | voice(2)).voice == 2);

    // If the newest has been let go, the lowest remaining one is used.
    Rig other;
    other.pass(0, voice(1));
    other.pass(0, voice(1) | voice(3));
    other.pass(0, voice(1));
    CHECK(other.pass(lane(1), voice(1)).voice == 1);
}

TEST_CASE("two lane buttons in one pass chord with the lowest", "[copy_lane][control_surface]")
{
    Rig rig;
    rig.pass(0, voice(0));
    const Input chord = rig.pass(lane(4) | lane(1), voice(0));
    CHECK(chord.action == Action::Copy);
    CHECK(chord.laneKey == 1);
    CHECK(chord.claimed == lane(1));
}

TEST_CASE("buttons held across a page are not fresh presses afterwards", "[copy_lane][control_surface]")
{
    Rig rig;
    rig.controls.observe(lane(2), voice(1)); // both were already down while another screen was up
    CHECK(rig.pass(lane(2), voice(1)).action == Action::None);
    // A lane pressed first and never answered by a voice must not leak across the page.
    Rig pending;
    pending.pass(lane(2), 0);
    pending.controls.observe(lane(2), 0);
    CHECK(pending.pass(lane(2), voice(1), 5).action == Action::None);
}

TEST_CASE("copy mode is off until the bridge fills the memory", "[copy_lane][control_surface]")
{
    Controls controls;
    CHECK_FALSE(controls.active);
    controls.begin(2); // a capture that produced nothing
    CHECK_FALSE(controls.active);

    controls.clip.lane = ParamId::Octave;
    controls.clip.length = 4;
    controls.begin(2);
    CHECK(controls.active);
    CHECK(controls.sourceVoice == 2);

    controls.end();
    CHECK_FALSE(controls.active);
    CHECK_FALSE(controls.clip.valid());
}

TEST_CASE("the paste confirmation shows briefly, then yields to the COPY LANE screen", "[copy_lane][control_surface]")
{
    Controls controls;
    controls.clip.lane = ParamId::Velocity;
    controls.clip.length = 16;
    controls.begin(0);
    CHECK_FALSE(controls.pasteNoticeShowing(5000));

    controls.notePaste(3, ParamId::Release, 5000);
    CHECK(controls.pastedVoice == 3);
    CHECK(controls.pastedLane == ParamId::Release);
    CHECK(controls.pasteNoticeShowing(5000));
    CHECK(controls.pasteNoticeShowing(5000 + CopyLane::kPasteNoticeMs - 1));
    CHECK_FALSE(controls.pasteNoticeShowing(5000 + CopyLane::kPasteNoticeMs));

    // Leaving copy mode takes the card with it.
    controls.notePaste(1, ParamId::Note, 9000);
    controls.end();
    CHECK_FALSE(controls.pasteNoticeShowing(9001));
}

TEST_CASE("OLED copy fits the 21-column panel", "[copy_lane][control_surface]")
{
    CHECK(std::strlen(CopyLane::kPasteHint) <= 21);
    CHECK(std::strlen(CopyLane::kExitHint) <= 21);
    CHECK(std::strlen(CopyLane::kTitle) * 12 <= 128); // drawn at text size 2
    CHECK(std::strstr(CopyLane::kExitHint, "SHIFT") != nullptr);

    char line[22];
    CopyLane::formatVoiceLine(line, sizeof(line), 0, "Analog");
    CHECK(std::strcmp(line, "V1 Analog") == 0);
    CopyLane::formatVoiceLine(line, sizeof(line), 3, "A very long preset name indeed");
    CHECK(std::strlen(line) == 21); // cut to the buffer, still terminated
    CHECK(std::strncmp(line, "V4 A very long preset", 21) == 0);
    CopyLane::formatVoiceLine(line, sizeof(line), 1, nullptr);
    CHECK(std::strcmp(line, "V2 ") == 0);
}

TEST_CASE("copy mode keeps a held voice from becoming length entry", "[copy_lane][ui_transitions]")
{
    UIState state;
    UITransitions::selectPerformanceVoice(state, 1);
    UITransitions::beginGateLengthHold(state, 1);
    CHECK(state.gateSeqLengthVoice == 1); // normal: a hold would open length entry

    state.copyLane.clip.lane = ParamId::Filter;
    state.copyLane.clip.length = 16;
    state.copyLane.begin(0);
    CHECK_FALSE(UITransitions::updateGateLengthHold(state, 1, true, 5000, 400)); // armed before, cancelled now
    CHECK(state.gateSeqLengthVoice == -1);
    UITransitions::beginGateLengthHold(state, 1);
    CHECK(state.gateSeqLengthVoice == -1); // and a fresh press cannot arm one

    state.copyLane.end();
    UITransitions::beginGateLengthHold(state, 1);
    CHECK(state.gateSeqLengthVoice == 1);
}
