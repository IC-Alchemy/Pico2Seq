#include "app/AppState.h"
#include "app/ReverbEditor.h"
#include "ui/ControlSurfaceLogic.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

// ReverbEditor applies the page's fader positions and Freeze switch to the master
// reverb's lock-free targets. The real VoiceManager is used, so a published value is
// exactly what the audio thread would pick up.

using ControlSurface::ReverbControl;
using Catch::Approx;

namespace {
struct EditorFixture {
    EditorFixture() {
        uiState = {};
        voiceManager = std::make_unique<VoiceManager>(1);
        voiceManager->init(48000.0f);
    }
    ~EditorFixture() { voiceManager.reset(); }
};
} // namespace

TEST_CASE("Fader positions publish each setting and mark it for the OLED", "[reverb_page][reverb_editor]") {
    EditorFixture fixture;
    struct Case { ReverbControl control; float (*read)(const ReverbSettings &); };
    const Case cases[] = {
        {ReverbControl::Mix, [](const ReverbSettings &s) { return s.mix; }},
        {ReverbControl::Decay, [](const ReverbSettings &s) { return s.decaySeconds; }},
        {ReverbControl::Damping, [](const ReverbSettings &s) { return s.dampingHz; }},
        {ReverbControl::LowCut, [](const ReverbSettings &s) { return s.lowCutHz; }},
        {ReverbControl::Diffusion, [](const ReverbSettings &s) { return s.diffusion; }},
        {ReverbControl::ModDepth, [](const ReverbSettings &s) { return s.modDepth; }},
        {ReverbControl::Width, [](const ReverbSettings &s) { return s.width; }},
    };
    for (const Case &c : cases) {
        CAPTURE(int(c.control));
        for (const float position : {0.0f, 0.25f, 0.5f, 0.9f, 1.0f}) {
            REQUIRE(ReverbEditor::setFromFader(c.control, position));
            CHECK(c.read(voiceManager->getReverbSettings()) ==
                  ControlSurface::reverbValueForFader(c.control, position));
            CHECK(uiState.reverbPage.lastControl == static_cast<uint8_t>(c.control));
        }
    }
}

TEST_CASE("Only the moved setting changes, and hostile positions apply nothing", "[reverb_page][reverb_editor]") {
    EditorFixture fixture;
    const ReverbSettings before = voiceManager->getReverbSettings();
    REQUIRE(ReverbEditor::setFromFader(ReverbControl::Decay, 0.5f));
    ReverbSettings expected = before;
    expected.decaySeconds = ControlSurface::reverbValueForFader(ReverbControl::Decay, 0.5f);
    CHECK(voiceManager->getReverbSettings() == expected);

    uiState.reverbPage.lastControl = 255;
    CHECK_FALSE(ReverbEditor::setFromFader(ReverbControl::Count, 0.5f)); // an unassigned fader
    CHECK_FALSE(ReverbEditor::setFromFader(ReverbControl::Mix, std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(ReverbEditor::setFromFader(ReverbControl::Mix, std::numeric_limits<float>::infinity()));
    CHECK(voiceManager->getReverbSettings() == expected);
    CHECK(uiState.reverbPage.lastControl == 255);
    // Out-of-range but finite positions clamp to the fader's ends.
    CHECK(ReverbEditor::setFromFader(ReverbControl::Mix, 3.0f));
    CHECK(voiceManager->getReverbSettings().mix == 1.0f);
    CHECK(ReverbEditor::setFromFader(ReverbControl::Mix, -3.0f));
    CHECK(voiceManager->getReverbSettings().mix == 0.0f);

    // No manager (boot before the voices exist): nothing applied, nothing crashes.
    voiceManager.reset();
    CHECK_FALSE(ReverbEditor::setFromFader(ReverbControl::Mix, 0.5f));
    CHECK_FALSE(ReverbEditor::toggleFreeze());
}

TEST_CASE("Freeze toggles the published switch", "[reverb_page][reverb_editor]") {
    EditorFixture fixture;
    CHECK_FALSE(voiceManager->getReverbSettings().freeze);
    CHECK(ReverbEditor::toggleFreeze());
    CHECK(voiceManager->getReverbSettings().freeze);
    CHECK_FALSE(ReverbEditor::toggleFreeze());
    CHECK_FALSE(voiceManager->getReverbSettings().freeze);
    // Fader moves never disturb the switch.
    ReverbEditor::toggleFreeze();
    ReverbEditor::setFromFader(ReverbControl::Mix, 0.7f);
    CHECK(voiceManager->getReverbSettings().freeze);
}

TEST_CASE("Moving a fader while audio renders reaches the sound without a click", "[reverb_page][reverb_editor]") {
    EditorFixture fixture;
    VoiceConfig config;
    config.oscillatorCount = 1;
    config.oscWaveforms[0] = WAVE_SIN;
    config.hasEnvelope = false;
    config.hasFilter = false;
    const uint8_t id = voiceManager->addVoice(config);
    VoiceState note;
    note.noteIndex = 24.0f;
    note.velocityLevel = 1.0f;
    note.isGateHigh = true;
    note.shouldRetrigger = true;
    voiceManager->updateVoiceState(id, note);
    std::array<float, 256> left{}, right{};
    for (int block = 0; block < 100; ++block) voiceManager->processStereoBlock(left.data(), right.data(), 256);
    // Dry so far: the two channels are identical (mix 0).
    for (unsigned i = 0; i < 256; ++i) REQUIRE(left[i] == right[i]);

    // Sweep the Mix fader from 0 to 1 over a second of audio in fader-sized steps,
    // as the page does. The output stays finite and continuous (no per-step jump
    // beyond what the tone itself does), and the wet image opens up.
    float worstStep = 0.0f, worstStepReference = 0.0f, previous = 0.0f;
    bool first = true;
    double difference = 0.0;
    for (int step = 0; step <= 187; ++step) {
        ReverbEditor::setFromFader(ReverbControl::Mix, static_cast<float>(step) / 187.0f);
        voiceManager->processStereoBlock(left.data(), right.data(), 256);
        for (unsigned i = 0; i < 256; ++i) {
            REQUIRE(std::isfinite(left[i]));
            REQUIRE(std::isfinite(right[i]));
            if (!first) worstStep = std::max(worstStep, std::fabs(left[i] - previous));
            first = false;
            previous = left[i];
            if (step > 150) difference += std::fabs(left[i] - right[i]);
        }
        if (step == 0) worstStepReference = worstStep;
    }
    CHECK(worstStep < 0.5f); // a 1-sample step to a wet mix would be a cliff; this stays a glide
    CHECK(worstStep < 3.0f * worstStepReference + 0.02f);
    CHECK(difference > 0.1);
}
