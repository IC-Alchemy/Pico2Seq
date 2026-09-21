// Unit tests for the round-display page link (src/RoundDisplay/
// RoundDisplayLink.cpp): the Pico-side serializer that re-runs the OLED's
// page priority chain and pushes PAGE_FRAME packets over the stubbed Wire.
//
// The regressions these exist for: the gate chain must match
// src/OLED/oled.cpp:243-506 exactly (a reordered gate shows the wrong page
// on the panel), and the shadow/SEQ/heartbeat discipline must keep the wire
// quiet when nothing changed (the plan's §2 budget claim).

#include "RoundDisplay/RoundDisplayLink.h"

#include <catch2/catch_test_macros.hpp>

#include <Arduino.h> // controllable fake clock
#include <Wire.h>    // recording/fault-injecting stub
#include <uClock.h>

#include <cstring>
#include <string>
#include <vector>

#include "LEDMatrix/LEDMatrixFeedback.h" // LEDThemeColors
#include "app/AppState.h"                 // AppState::performanceInput, globals
#include "app/SequencerView.h"
#include "sensors/DistanceSensor.h"
#include "voice/VoicePresets.h"

namespace rd = rdisplay;

// ---------------------------------------------------------------------------
// Test-provided definitions for symbols whose firmware homes cannot compile
// on the host: LEDMatrixFeedback.cpp needs the real FastLED, VoiceEditor.cpp
// needs the encoder driver. The suite pins the exact API RoundDisplayLink
// consumes — the signature contract, not the firmware behavior.
// ---------------------------------------------------------------------------
namespace
{
LEDThemeColors testTheme = {};
} // namespace

const LEDThemeColors *getActiveThemeColors()
{
    return &testTheme;
}

namespace VoiceEditor
{
VoiceEdit::Id encoderTarget()
{
    return VoiceEdit::Id::Velocity;
}
} // namespace VoiceEditor

// The suite always passes a null voice manager (RoundDisplayLink guards every
// use behind a null check, as the OLED does), but the symbol must still link.
// VoiceManager.cpp itself drags in the DSP stack, so the suite provides the
// one out-of-line member the link calls.
VoiceConfig const *VoiceManager::getVoiceConfig(uint8_t)
{
    return nullptr;
}

// ---------------------------------------------------------------------------
// Rig: a present panel, a fake clock at 1000 ms, and a recording Wire
// ---------------------------------------------------------------------------

namespace
{

struct TestRig
{
    UIState ui;
    Sequencer seq1{1};
    Sequencer seq2{2};
    Sequencer seq3{3};
    Sequencer seq4{4};
    Sequencer *const sequencers[4] = {&seq1, &seq2, &seq3, &seq4};
    SequencerView view{sequencers};
    RoundDisplayLink link;

    explicit TestRig(bool panelPresent = true)
    {
        arduino_testing::setMillis(1000);
        uClock.setTempo(120.0f);
        Wire.clearTestState();
        Wire.recordTransmissions = true;
        Wire.endTransmissionStatus = 0;
        if (panelPresent)
        {
            Wire.rxFeed.assign({rd::kWhoAmIMagic});
            REQUIRE(link.begin());
        }
        else
        {
            CHECK_FALSE(link.begin());
        }
        Wire.clearTestState();
        Wire.recordTransmissions = true;
    }

    void setTime(unsigned long ms) { arduino_testing::setMillis(ms); }
    void advance(unsigned long ms) { arduino_testing::advanceMillis(ms); }
    void update(const UIState &state, const SequencerView &sequencers,
                VoiceManager *voiceManager)
    {
        link.update(state, sequencers, voiceManager);
    }
};

/** The captured PAGE_FRAME transactions (probe pointer-writes filtered out). */
std::vector<const TwoWire::Transmission *> frameTransactions()
{
    std::vector<const TwoWire::Transmission *> frames;
    for (const auto &t : Wire.transmissions)
    {
        if (t.address == rd::kDisplayAddress && t.bytes.size() >= 2 &&
            t.bytes[0] == rd::kRegFrame)
            frames.push_back(&t);
    }
    return frames;
}

/** Number of PAGE_FRAME write transactions captured so far. */
size_t frameCount()
{
    return frameTransactions().size();
}

/** Decode the index-th PAGE_FRAME transaction (register pointer stripped). */
rd::PageFrame decodeFrameAt(size_t index)
{
    const auto frames = frameTransactions();
    REQUIRE(index < frames.size());
    const auto &bytes = frames[index]->bytes;
    rd::PageFrame frame{};
    REQUIRE(rd::decodeFrame(bytes.data() + 1, bytes.size() - 1, frame));
    return frame;
}

/** Raw bytes of the index-th PAGE_FRAME transaction, for identity checks. */
std::vector<uint8_t> rawFrameAt(size_t index)
{
    const auto frames = frameTransactions();
    REQUIRE(index < frames.size());
    return frames[index]->bytes;
}

std::string bodyString(const char *text, size_t cap)
{
    return std::string(text, strnlen(text, cap));
}

/** The decoded first frame's Status body, for spot checks. */
rd::StatusBody statusBodyOf(const rd::PageFrame &frame)
{
    REQUIRE(frame.page == rd::PageId::Status);
    REQUIRE(frame.bodyLen == sizeof(rd::StatusBody));
    rd::StatusBody body{};
    std::memcpy(&body, frame.body, sizeof(body));
    return body;
}

} // namespace

// ---------------------------------------------------------------------------
// Presence
// ---------------------------------------------------------------------------

TEST_CASE("begin probes WHOAMI at the display address", "[round_display]")
{
    Wire.clearTestState();
    Wire.recordTransmissions = true;
    Wire.rxFeed.assign({rd::kWhoAmIMagic});
    RoundDisplayLink link;
    CHECK(link.begin());
    CHECK(link.isInitialized());

    // The probe is a tile-shaped pointer read: write reg 0x00, STOP, read 1.
    REQUIRE(Wire.transmissions.size() == 1);
    CHECK(Wire.transmissions[0].address == rd::kDisplayAddress);
    CHECK(Wire.transmissions[0].bytes == std::vector<uint8_t>{rd::kRegWhoAmI});
}

TEST_CASE("begin fails cleanly when the panel is absent", "[round_display]")
{
    Wire.clearTestState();
    RoundDisplayLink link;
    CHECK_FALSE(link.begin());
    CHECK_FALSE(link.isInitialized());
    // A NACKing bus (endTransmission != 0) is the same "absent" outcome.
    Wire.endTransmissionStatus = 1;
    Wire.rxFeed.assign({rd::kWhoAmIMagic});
    CHECK_FALSE(link.begin());
    Wire.endTransmissionStatus = 0;
}

TEST_CASE("probeIdentity reads TYPE and PROTO by register pointer", "[round_display]")
{
    Wire.clearTestState();
    Wire.recordTransmissions = true;
    RoundDisplayLink link;
    Wire.rxFeed.assign({rd::kTypeDisplay, rd::kProtoVerV1});

    uint8_t typeId = 0;
    uint8_t protoVer = 0;
    CHECK(link.probeIdentity(typeId, protoVer));
    CHECK(typeId == rd::kTypeDisplay);
    CHECK(protoVer == rd::kProtoVerV1);
    REQUIRE(Wire.transmissions.size() == 2);
    CHECK(Wire.transmissions[0].bytes == std::vector<uint8_t>{rd::kRegTypeId});
    CHECK(Wire.transmissions[1].bytes == std::vector<uint8_t>{rd::kRegProtoVer});

    // A panel answering with the wrong type is not our display.
    Wire.clearTestState();
    Wire.recordTransmissions = true;
    Wire.rxFeed.assign({0x01, rd::kProtoVerV1});
    CHECK_FALSE(link.probeIdentity(typeId, protoVer));
}

TEST_CASE("an absent panel stays silent until a later probe recovers",
          "[round_display]")
{
    TestRig rig(/*panelPresent=*/false);

    // Before the re-probe window elapses, update() neither probes nor sends.
    rig.ui.currentThemeIndex = 2;
    rig.update(rig.ui, rig.view, nullptr);
    CHECK(frameCount() == 0);
    CHECK_FALSE(rig.link.isInitialized());

    // 1000 ms later the probe succeeds and the first frame goes out.
    rig.advance(1000);
    Wire.rxFeed.assign({rd::kWhoAmIMagic});
    rig.update(rig.ui, rig.view, nullptr);
    CHECK(rig.link.isInitialized());
    REQUIRE(frameCount() == 1);
    CHECK(decodeFrameAt(0).page == rd::PageId::Status);
    CHECK(decodeFrameAt(0).themeIdx == 2);
}

// ---------------------------------------------------------------------------
// Page hierarchy
// ---------------------------------------------------------------------------

TEST_CASE("the default state serializes the Status page", "[round_display]")
{
    TestRig rig;
    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 1);
    CHECK(decodeFrameAt(0).page == rd::PageId::Status);
}

TEST_CASE("each transient page gate isolates to its PageId", "[round_display]")
{
    struct GateCase
    {
        const char *name;
        rd::PageId expected;
        void (*configure)(UIState &ui, uint32_t now);
    };
    const GateCase cases[] = {
        {"voice editor", rd::PageId::VoiceEditor,
         [](UIState &ui, uint32_t) { ui.voiceEditor.active = true; }},
        {"mode banner", rd::PageId::ModeBanner,
         [](UIState &ui, uint32_t now) { ui.alchemyModeBannerUntil = now + 100; }},
        {"notice", rd::PageId::Notice,
         [](UIState &ui, uint32_t now) {
             ui.oledNoticeUntil = now + 100;
             ui.oledNoticeKind = UIState::OledNoticeKind::Saved;
         }},
        {"held param", rd::PageId::HeldParam,
         [](UIState &ui, uint32_t) {
             ui.parameterButtonHeld[static_cast<uint8_t>(ParamId::Velocity)] = true;
         }},
        {"settings presets", rd::PageId::SettingsPresets,
         [](UIState &ui, uint32_t) { ui.settingsMode = true; }},
        {"settings toggles", rd::PageId::SettingsToggles,
         [](UIState &ui, uint32_t) {
             ui.settingsMode = true;
             ui.currentSubMode = UIState::SettingsSubMode::VOICE_PARAMETER;
         }},
        {"gate length", rd::PageId::GateLength,
         [](UIState &ui, uint32_t) { ui.gateSeqLengthMode = true; }},
        {"step env", rd::PageId::StepEnv,
         [](UIState &ui, uint32_t) { ui.selectedStepForEdit = 3; }},
        {"param edit", rd::PageId::ParamEdit,
         [](UIState &ui, uint32_t) {
             ui.selectedStepForEdit = 3;
             ui.currentEditParameter = ParamId::Filter;
         }},
    };

    for (const auto &gate : cases)
    {
        DYNAMIC_SECTION(gate.name << " wins over the default")
        {
            TestRig rig;
            gate.configure(rig.ui, 1000);
            rig.update(rig.ui, rig.view, nullptr);
            REQUIRE(frameCount() == 1);
            CHECK(decodeFrameAt(0).page == gate.expected);
        }
    }
}

TEST_CASE("gate precedence mirrors the OLED chain", "[round_display]")
{
    const uint32_t now = 1000;
    struct PairCase
    {
        const char *name;
        rd::PageId expected;
        void (*configure)(UIState &ui, uint32_t now);
    };
    const PairCase cases[] = {
        {"voice editor beats notice", rd::PageId::VoiceEditor,
         [](UIState &ui, uint32_t now) {
             ui.voiceEditor.active = true;
             ui.oledNoticeUntil = now + 100;
             ui.oledNoticeKind = UIState::OledNoticeKind::Saved;
         }},
        {"mode banner beats notice", rd::PageId::ModeBanner,
         [](UIState &ui, uint32_t now) {
             ui.alchemyModeBannerUntil = now + 100;
             ui.oledNoticeUntil = now + 100;
             ui.oledNoticeKind = UIState::OledNoticeKind::Saved;
         }},
        {"notice beats held param", rd::PageId::Notice,
         [](UIState &ui, uint32_t now) {
             ui.oledNoticeUntil = now + 100;
             ui.oledNoticeKind = UIState::OledNoticeKind::Saved;
             ui.parameterButtonHeld[static_cast<uint8_t>(ParamId::Velocity)] = true;
         }},
        {"held param beats settings", rd::PageId::HeldParam,
         [](UIState &ui, uint32_t) {
             ui.parameterButtonHeld[static_cast<uint8_t>(ParamId::Velocity)] = true;
             ui.settingsMode = true;
             ui.currentSubMode = UIState::SettingsSubMode::VOICE_PARAMETER;
         }},
        {"settings beats gate length", rd::PageId::SettingsPresets,
         [](UIState &ui, uint32_t) {
             ui.settingsMode = true;
             ui.gateSeqLengthMode = true;
         }},
        {"gate length beats step env", rd::PageId::GateLength,
         [](UIState &ui, uint32_t) {
             ui.gateSeqLengthMode = true;
             ui.selectedStepForEdit = 3;
         }},
        {"step env beats param edit while the ENV window is up",
         rd::PageId::StepEnv,
         [](UIState &ui, uint32_t now) {
             ui.selectedStepForEdit = 3;
             ui.currentEditParameter = ParamId::Filter;
             ui.envViewUntil = now + 100;
         }},
        {"param edit beats status", rd::PageId::ParamEdit,
         [](UIState &ui, uint32_t) {
             ui.selectedStepForEdit = 3;
             ui.currentEditParameter = ParamId::Filter;
         }},
    };

    for (const auto &pair : cases)
    {
        DYNAMIC_SECTION(pair.name)
        {
            TestRig rig;
            pair.configure(rig.ui, now);
            rig.update(rig.ui, rig.view, nullptr);
            REQUIRE(frameCount() == 1);
            CHECK(decodeFrameAt(0).page == pair.expected);
        }
    }
}

TEST_CASE("expired transient windows fall through to the default page",
          "[round_display]")
{
    TestRig rig;
    // All three transient windows in the past: Status must win.
    rig.ui.alchemyModeBannerUntil = 900;
    rig.ui.oledNoticeUntil = 900;
    rig.ui.oledNoticeKind = UIState::OledNoticeKind::Saved;
    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 1);
    CHECK(decodeFrameAt(0).page == rd::PageId::Status);
}

// ---------------------------------------------------------------------------
// Dirty shadow, SEQ and heartbeat
// ---------------------------------------------------------------------------

TEST_CASE("an unchanged state sends exactly one frame", "[round_display]")
{
    TestRig rig;
    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 1);

    rig.update(rig.ui, rig.view, nullptr);
    rig.update(rig.ui, rig.view, nullptr);
    CHECK(frameCount() == 1); // the shadow kept the wire quiet
    CHECK(rig.link.sentFrames() == 1);
    CHECK(rig.link.sendFailures() == 0);
}

TEST_CASE("a changed field advances the SEQ by one and re-sends", "[round_display]")
{
    TestRig rig;
    rig.update(rig.ui, rig.view, nullptr);
    const uint8_t firstSeq = decodeFrameAt(0).seq;

    uClock.setTempo(126.0f); // Status bpmX10 changes
    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 2);
    const rd::PageFrame second = decodeFrameAt(1);
    CHECK(second.seq == static_cast<uint8_t>((firstSeq + 1) & rd::kStatusSeqMask));
    CHECK(statusBodyOf(second).bpmX10 == 1260);
}

TEST_CASE("the SEQ wraps at 15 instead of comparing by magnitude",
          "[round_display]")
{
    TestRig rig;
    rig.update(rig.ui, rig.view, nullptr);
    uint8_t seq = decodeFrameAt(0).seq;
    // Walk the counter to 15, then one more change must wrap to 0.
    while (seq != 15)
    {
        uClock.setTempo(uClock.getTempo() + 1.0f);
        rig.update(rig.ui, rig.view, nullptr);
        seq = decodeFrameAt(frameCount() - 1).seq;
    }
    uClock.setTempo(uClock.getTempo() + 1.0f);
    rig.update(rig.ui, rig.view, nullptr);
    CHECK(decodeFrameAt(frameCount() - 1).seq == 0);
}

TEST_CASE("after 500 ms of silence the heartbeat re-sends identical bytes "
          "with the same SEQ",
          "[round_display]")
{
    TestRig rig;
    uClock.setTempo(123.0f);
    rig.update(rig.ui, rig.view, nullptr);
    const std::vector<uint8_t> original = rawFrameAt(0);
    const uint8_t seq = decodeFrameAt(0).seq;

    // Just under the heartbeat window: still nothing.
    rig.advance(499);
    rig.update(rig.ui, rig.view, nullptr);
    CHECK(frameCount() == 1);

    rig.advance(1); // 500 ms since the last send
    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 2);
    CHECK(decodeFrameAt(1).seq == seq);      // same SEQ: idempotent re-send
    CHECK(rawFrameAt(1) == original);        // byte-identical frame
    CHECK(rig.link.sentFrames() == 2);

    // The heartbeat resets the window; the next tick stays quiet again.
    rig.advance(400);
    rig.update(rig.ui, rig.view, nullptr);
    CHECK(frameCount() == 2);
}

TEST_CASE("clear() forces a rebuild and re-send with the same SEQ",
          "[round_display]")
{
    TestRig rig;
    rig.update(rig.ui, rig.view, nullptr);
    const std::vector<uint8_t> original = rawFrameAt(0);
    const uint8_t seq = decodeFrameAt(0).seq;

    rig.link.clear();
    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 2);
    CHECK(decodeFrameAt(1).seq == seq);
    CHECK(rawFrameAt(1) == original);
}

// ---------------------------------------------------------------------------
// Send failure and retry
// ---------------------------------------------------------------------------

TEST_CASE("a failed send keeps the shadow dirty and the next update retries",
          "[round_display]")
{
    TestRig rig;
    rig.update(rig.ui, rig.view, nullptr);
    const uint8_t firstSeq = decodeFrameAt(0).seq;
    const uint32_t sentBefore = rig.link.sentFrames();

    // The changed frame NACKs.
    uClock.setTempo(130.0f);
    Wire.endTransmissionStatus = 1;
    rig.update(rig.ui, rig.view, nullptr);
    CHECK(frameCount() == 2); // attempted
    CHECK(rig.link.sendFailures() == 1);
    CHECK(rig.link.sentFrames() == sentBefore);

    // The immediate retry succeeds with the same (single) SEQ advance.
    Wire.clearTestState();
    Wire.recordTransmissions = true;
    Wire.endTransmissionStatus = 0;
    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 1);
    const rd::PageFrame retried = decodeFrameAt(0);
    CHECK(retried.seq == static_cast<uint8_t>((firstSeq + 1) & rd::kStatusSeqMask));
    CHECK(statusBodyOf(retried).bpmX10 == 1300);
    CHECK(rig.link.sentFrames() == sentBefore + 1);

    // The shadow is clean again: no further sends.
    rig.update(rig.ui, rig.view, nullptr);
    CHECK(frameCount() == 1);
}

TEST_CASE("a failed heartbeat retries without advancing the SEQ",
          "[round_display]")
{
    TestRig rig;
    rig.update(rig.ui, rig.view, nullptr);
    const std::vector<uint8_t> original = rawFrameAt(0);
    const uint8_t seq = decodeFrameAt(0).seq;

    rig.advance(500);
    Wire.endTransmissionStatus = 1;
    rig.update(rig.ui, rig.view, nullptr);
    CHECK(rig.link.sendFailures() == 1);

    Wire.clearTestState();
    Wire.recordTransmissions = true;
    Wire.endTransmissionStatus = 0;
    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 1);
    CHECK(decodeFrameAt(0).seq == seq); // the panel may or may not have it:
    CHECK(rawFrameAt(0) == original);   // a same-SEQ re-send covers both.
}

// ---------------------------------------------------------------------------
// Body spot checks
// ---------------------------------------------------------------------------

TEST_CASE("the Status body carries tempo, presets, scale and gate bits",
          "[round_display]")
{
    TestRig rig;
    uClock.setTempo(120.0f);
    rig.ui.currentThemeIndex = 3;
    rig.ui.currentShufflePatternIndex = 5;
    rig.ui.voicePresetIndices[0] = 7;
    ::currentScale = 2;
    rig.seq1.setParameterStepCount(ParamId::Gate, 4);
    rig.seq1.setStepParameterValue(ParamId::Gate, 0, 1.0f);
    rig.seq1.setStepParameterValue(ParamId::Gate, 1, 0.0f);
    rig.seq1.setStepParameterValue(ParamId::Gate, 2, 1.0f);
    rig.seq1.setStepParameterValue(ParamId::Gate, 3, 0.0f);

    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 1);

    const rd::PageFrame frame = decodeFrameAt(0);
    CHECK(frame.themeIdx == 3);
    const rd::StatusBody body = statusBodyOf(frame);
    CHECK(body.bpmX10 == 1200);
    CHECK(body.playing == 1);
    CHECK(body.scaleIdx == 2);
    CHECK(body.shuffleIdx == 5);
    CHECK(body.currentStep == 0);
    CHECK(body.encTargetId ==
          static_cast<uint8_t>(VoiceEdit::Id::Velocity)); // the suite's fake target
    for (uint8_t i = 0; i < 4; ++i)
    {
        CHECK(body.presetIdx[i] == rig.ui.voicePresetIndices[i]);
    }
    // Gate bits are MSB-first per byte: steps 0 and 2 on -> 0b1010_0000.
    CHECK(body.gateBits[0] == 0xA0);
    for (size_t i = 1; i < sizeof(body.gateBits); ++i)
        CHECK(body.gateBits[i] == 0);

    ::currentScale = 0;
}

TEST_CASE("the Status playing flag mirrors the clock state", "[round_display]")
{
    TestRig rig;
    ::isClockRunning = false;
    rig.update(rig.ui, rig.view, nullptr);
    CHECK(statusBodyOf(decodeFrameAt(0)).playing == 0);
    ::isClockRunning = true;
    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 2); // content changed: a second frame went out
    CHECK(statusBodyOf(decodeFrameAt(1)).playing == 1);
}

TEST_CASE("the theme block converts the active theme to RGB565",
          "[round_display]")
{
    TestRig rig;
    testTheme.gateOn[0] = CRGB(200, 30, 30);
    testTheme.gateOn[1] = CRGB(30, 200, 30);
    testTheme.gateOn[2] = CRGB(30, 30, 200);
    testTheme.gateOn[3] = CRGB(180, 180, 40);
    testTheme.playheadAccent = CRGB(255, 255, 255);
    testTheme.backgroundBase = CRGB(8, 8, 16);
    testTheme.textAccent = CRGB(240, 240, 240);

    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 1);
    const rd::PageFrame frame = decodeFrameAt(0);
    CHECK(rd::themeWord(frame.theme, 0) == rd::toRgb565(200, 30, 30));
    CHECK(rd::themeWord(frame.theme, 1) == rd::toRgb565(30, 200, 30));
    CHECK(rd::themeWord(frame.theme, 2) == rd::toRgb565(30, 30, 200));
    CHECK(rd::themeWord(frame.theme, 3) == rd::toRgb565(180, 180, 40));
    CHECK(rd::themeWord(frame.theme, 4) == rd::toRgb565(255, 255, 255));
    CHECK(rd::themeWord(frame.theme, 5) == rd::toRgb565(8, 8, 16));
    CHECK(rd::themeWord(frame.theme, 6) == rd::toRgb565(240, 240, 240));
}

TEST_CASE("the GateLength body carries the voice and its gate step count",
          "[round_display]")
{
    TestRig rig;
    rig.ui.selectedVoiceIndex = 2;
    rig.ui.gateSeqLengthMode = true;
    rig.seq3.setParameterStepCount(ParamId::Gate, 12);

    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 1);
    const rd::PageFrame frame = decodeFrameAt(0);
    REQUIRE(frame.page == rd::PageId::GateLength);
    rd::GateLengthBody body{};
    std::memcpy(&body, frame.body, sizeof(body));
    CHECK(body.voice == 2);
    CHECK(body.length == 12);
}

TEST_CASE("the VoiceEditor body carries the voice, its changed flag and the "
          "cursor parameter",
          "[round_display]")
{
    TestRig rig;
    rig.ui.selectedVoiceIndex = 1;
    rig.ui.voiceEditor.active = true;
    rig.ui.voiceEditor.changed[1] = true;
    rig.ui.voiceEditor.cursor[1] = VoiceEdit::Id::Cutoff;

    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 1);
    const rd::PageFrame frame = decodeFrameAt(0);
    REQUIRE(frame.page == rd::PageId::VoiceEditor);
    rd::VoiceEditorBody body{};
    std::memcpy(&body, frame.body, sizeof(body));
    CHECK(body.voice == 1);
    CHECK(body.changed == 1);
    // No voice manager in the rig: the codebase's "--" placeholder.
    CHECK(body.paramName == std::string("--"));
    CHECK(body.paramValue == std::string("--"));
}

TEST_CASE("the Notice body mirrors the OLED wording for all notice kinds",
          "[round_display]")
{
    struct NoticeCase
    {
        UIState::OledNoticeKind kind;
        const char *word;
        const char *sub;
        uint8_t voice;
        uint16_t value;
    };
    const NoticeCase cases[] = {
        {UIState::OledNoticeKind::Randomized, "RANDOMIZED", "Voice 3", 2, 0},
        {UIState::OledNoticeKind::Saved, "SAVED", "", 0, 0},
        {UIState::OledNoticeKind::Loaded, "LOADED", "", 0, 0},
        {UIState::OledNoticeKind::LoadError, "LOAD ERR", "", 0, 0},
        {UIState::OledNoticeKind::VoiceCleared, "CLEARED", "Voice 1", 0, 0},
        {UIState::OledNoticeKind::AllCleared, "ALL CLEAR", "", 0, 0},
        {UIState::OledNoticeKind::DelayMix, "DELAY MIX", "45 %", 0, 45},
        {UIState::OledNoticeKind::DelayTime, "DELAY TIME", "250 ms", 0, 250},
    };

    for (const auto &notice : cases)
    {
        DYNAMIC_SECTION("kind " << static_cast<int>(notice.kind))
        {
            TestRig rig;
            rig.ui.oledNoticeUntil = 1000 + 100;
            rig.ui.oledNoticeKind = notice.kind;
            rig.ui.oledNoticeVoice = notice.voice;
            rig.ui.oledNoticeValue = notice.value;

            rig.update(rig.ui, rig.view, nullptr);
            REQUIRE(frameCount() == 1);
            const rd::PageFrame frame = decodeFrameAt(0);
            REQUIRE(frame.page == rd::PageId::Notice);
            rd::NoticeBody body{};
            std::memcpy(&body, frame.body, sizeof(body));
            CHECK(body.kind == static_cast<uint8_t>(notice.kind));
            CHECK(body.voice == notice.voice);
            CHECK(body.value == notice.value);
            CHECK(bodyString(body.word, sizeof(body.word)) == notice.word);
            CHECK(bodyString(body.sub, sizeof(body.sub)) == notice.sub);
        }
    }
}

TEST_CASE("the ModeBanner body distinguishes PARAM from UTIL", "[round_display]")
{
    TestRig rig;
    rig.ui.alchemyModeBannerUntil = 1100;

    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 1);
    {
        const rd::PageFrame frame = decodeFrameAt(0);
        REQUIRE(frame.page == rd::PageId::ModeBanner);
        rd::ModeBannerBody body{};
        std::memcpy(&body, frame.body, sizeof(body));
        CHECK(body.kind == 0); // UIState::AlchemyMode::Param is the default
    }

    // A UTIL flip is a content change: a new frame, kind 1.
    rig.ui.alchemyMode = UIState::AlchemyMode::Utility;
    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 2);
    const rd::PageFrame frame = decodeFrameAt(1);
    rd::ModeBannerBody body{};
    std::memcpy(&body, frame.body, sizeof(body));
    CHECK(body.kind == 1);
}

TEST_CASE("the HeldParam body carries mode, step, distance and hand state",
          "[round_display]")
{
    TestRig rig;
    rig.ui.selectedVoiceIndex = 1;
    rig.ui.parameterButtonHeld[static_cast<uint8_t>(ParamId::Velocity)] = true;

    // No hand, no step selected: LIVE mode, default invalid distance.
    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 1);
    {
        const rd::PageFrame frame = decodeFrameAt(0);
        REQUIRE(frame.page == rd::PageId::HeldParam);
        rd::HeldParamBody body{};
        std::memcpy(&body, frame.body, sizeof(body));
        CHECK(body.paramId == static_cast<uint8_t>(ParamId::Velocity));
        CHECK(body.voice == 1);
        CHECK(body.mode == 1); // LIVE
        CHECK(body.step == 0); // the lane's playing step
        CHECK(body.distanceMm == -1);
        CHECK(body.handPresent == 0);
    }

    // A selected step flips the mode to STEP.
    rig.ui.selectedStepForEdit = 6;
    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 2);
    rd::HeldParamBody body{};
    std::memcpy(&body, decodeFrameAt(1).body, sizeof(body));
    CHECK(body.mode == 2); // STEP
    CHECK(body.step == 6);
}

TEST_CASE("the StepEnv body carries the four ADSR lanes and the fader lane",
          "[round_display]")
{
    TestRig rig;
    rig.ui.selectedStepForEdit = 2;
    rig.ui.envFaderLane = ParamId::Decay;
    rig.seq1.setStepParameterValue(ParamId::Attack, 2, 1.0f);
    // The patch-default sentinel: a negative stored value means "follow the
    // voice's patch" (SequencerDefs followsPatch).
    rig.seq1.setStepParameterValue(ParamId::Decay, 2,
                                   SequencerConstants::LANE_FOLLOWS_PATCH);
    rig.seq1.setStepParameterValue(ParamId::Sustain, 2, 0.5f);
    rig.seq1.setStepParameterValue(ParamId::Release, 2, 0.0f);

    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 1);
    const rd::PageFrame frame = decodeFrameAt(0);
    REQUIRE(frame.page == rd::PageId::StepEnv);
    rd::StepEnvBody body{};
    std::memcpy(&body, frame.body, sizeof(body));
    CHECK(body.voice == 0);
    CHECK(body.step == 2);
    CHECK(body.lastLane == static_cast<uint8_t>(ParamId::Decay));
    CHECK(body.laneValue[0] == 255); // Attack fully on
    CHECK(body.laneValue[1] == 0);   // Decay: sentinel clamps off the byte
    CHECK(body.laneValue[2] == 128); // Sustain 0.5, rounded to a byte
    CHECK(body.laneValue[3] == 0);   // Release off
    CHECK(body.laneFollows[0] == 0); // explicit value: plays its own step
    CHECK(body.laneFollows[1] == 1); // the sentinel lane follows the patch
    CHECK(body.laneFollows[2] == 0);
    CHECK(body.laneFollows[3] == 0);
}

TEST_CASE("the SettingsPresets body carries the bank snapshot",
          "[round_display]")
{
    TestRig rig;
    rig.ui.settingsMode = true; // PRESET_SELECTION is the default sub-mode
    rig.ui.selectedVoiceIndex = 3;
    rig.ui.voicePresetIndices[3] = 9;
    rig.ui.voiceEditor.changed[3] = true;

    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 1);
    const rd::PageFrame frame = decodeFrameAt(0);
    REQUIRE(frame.page == rd::PageId::SettingsPresets);
    rd::SettingsPresetsBody body{};
    std::memcpy(&body, frame.body, sizeof(body));
    CHECK(body.selectedVoice == 3);
    CHECK(body.count == VoicePresets::getPresetCount());
    CHECK(body.count == 29); // the preset bank the OLED prints today
    for (uint8_t i = 0; i < 4; ++i)
        CHECK(body.presetIdx[i] == rig.ui.voicePresetIndices[i]);
    CHECK(body.changed[3] == 1);
    CHECK(bodyString(body.selName, sizeof(body.selName)) ==
          VoicePresets::getPresetName(9));
}

TEST_CASE("the SettingsToggles body carries the notice snapshot fields",
          "[round_display]")
{
    TestRig rig;
    rig.ui.settingsMode = true;
    rig.ui.currentSubMode = UIState::SettingsSubMode::VOICE_PARAMETER;
    rig.ui.voiceParameterNoticeVoice = 2;
    snprintf(rig.ui.voiceParameterNoticeName,
             sizeof(rig.ui.voiceParameterNoticeName), "Glide");
    snprintf(rig.ui.voiceParameterNoticeValue,
             sizeof(rig.ui.voiceParameterNoticeValue), "Off");

    rig.update(rig.ui, rig.view, nullptr);
    REQUIRE(frameCount() == 1);
    const rd::PageFrame frame = decodeFrameAt(0);
    REQUIRE(frame.page == rd::PageId::SettingsToggles);
    rd::SettingsTogglesBody body{};
    std::memcpy(&body, frame.body, sizeof(body));
    CHECK(body.voice == 2);
    CHECK(bodyString(body.name, sizeof(body.name)) == "Glide");
    CHECK(bodyString(body.value, sizeof(body.value)) == "Off");
}

TEST_CASE("every serialized frame fits the 64-byte I2C budget", "[round_display]")
{
    TestRig rig;
    rig.update(rig.ui, rig.view, nullptr); // Status: the largest body
    for (const auto &t : Wire.transmissions)
    {
        if (t.address == rd::kDisplayAddress && t.bytes.size() >= 2 &&
            t.bytes[0] == rd::kRegFrame)
            CHECK(t.bytes.size() <= 1 + rd::kMaxFrameBytes);
    }
    // Status is exactly 1 register byte + 59 frame bytes.
    REQUIRE(frameCount() == 1);
    CHECK(rawFrameAt(0).size() == 1 + 59);
}
