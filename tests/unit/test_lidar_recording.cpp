#include "app/AppState.h"
#include "app/StepPlayback.h"
#include "app/VoicePlayback.h"
#include "ui/ControlSurfaceLogic.h"
#include "voice/MusicalValues.h"
#include "voice/VoicePresets.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>
#include <vector>

using Catch::Approx;

namespace {
struct LidarFixture {
    ControlSurface::ShiftLatch latch;

    explicit LidarFixture(const char *preset) {
        uiState = UIState{};
        voiceSystem = VoiceSystem{};
        AppState::performanceInput = {};
        voiceManager = std::make_unique<VoiceManager>(VoiceSystem::MAX_VOICES);
        auto config = VoicePresets::getPresetConfig(VoicePresets::findPreset(preset));
        VoiceEdit::enablePatch(config);
        for (uint8_t v = 0; v < VoiceSystem::MAX_VOICES; ++v) {
            auto &seq = *AppState::sequencers[v];
            seq = Sequencer{};
            const uint8_t id = voiceManager->addVoice(config);
            voiceSystem.setVoiceId(v, id);
            seq.setPlaybackTransform(VoiceEdit::composeLane, voiceManager->getVoiceConfig(id),
                                     VoiceEdit::mapOctave);
            VoiceEdit::seedModifiers(seq);
            seq.start();
        }
        voiceManager->init(48000);
        voiceManager->setTransportMuted(false);
        isClockRunning = true;
        render(960);
    }
    ~LidarFixture() {
        voiceManager.reset();
        uiState = UIState{};
        isClockRunning = false;
        AppState::performanceInput = {};
    }
    Sequencer &seq() { return *AppState::sequencers[uiState.selectedVoiceIndex]; }
    const VoiceConfig &config() {
        return *voiceManager->getVoiceConfig(voiceSystem.getVoiceId(uiState.selectedVoiceIndex));
    }
    const VoiceState &requested() {
        return *voiceManager->getVoiceState(voiceSystem.getVoiceId(uiState.selectedVoiceIndex));
    }
    void press(uint8_t bit, bool shifted = false) {
        const auto lane = ControlSurface::recordParamForButtonBit(bit);
        REQUIRE(lane != ParamId::Count);
        latch.onParamButton(static_cast<uint8_t>(lane), true, shifted);
        latch.applyTo(uiState.parameterButtonHeld, PARAM_ID_COUNT);
    }
    void release(uint8_t bit) {
        latch.onParamButton(static_cast<uint8_t>(ControlSurface::recordParamForButtonBit(bit)), false, false);
        latch.applyTo(uiState.parameterButtonHeld, PARAM_ID_COUNT);
    }
    void hand(float position) {
        constexpr int lo = SensorConstants::DistanceSensor::MIN_DISTANCE_HEIGHT_MM;
        constexpr int hi = SensorConstants::DistanceSensor::MAX_DISTANCE_HEIGHT_MM;
        AppState::performanceInput.observeDistance(lo + static_cast<int>(std::lround(position * (hi - lo))));
    }
    std::string oled(ParamId lane, uint8_t step = UINT8_MAX) {
        char out[48];
        MusicalValues::format(lane, seq().getPlaybackStep(step), config(), nullptr, 120.0f, out, sizeof(out));
        return out;
    }
    std::vector<float> render(int count) {
        std::vector<float> out(count);
        voiceManager->processBlock(out.data(), static_cast<uint32_t>(count));
        return out;
    }
};

double energy(const std::vector<float> &samples, bool brightness = false) {
    double sum = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        const double x = brightness ? samples[i] - samples[i - 1] : samples[i];
        sum += x * x;
    }
    return sum / samples.size();
}
}

TEST_CASE("Lidar button lanes reach stored steps, OLED values and published voices", "[lidar][app]") {
    for (const char *preset : {"Digital", "Square", "WgPluck"}) {
        INFO(preset);
        LidarFixture f(preset);
        uiState.selectedVoiceIndex = 2;
        f.seq().setParameterStepCount(ParamId::Filter, 3);
        f.seq().setParameterStepCount(ParamId::Release, 5);
        f.press(2);
        f.press(4, true); // Shift latch must also retain the appended Release ID.
        f.release(4);
        f.hand(0.0f);
        processSequencerStep(7);
        CHECK(f.seq().getCurrentStepForParameter(ParamId::Filter) == 1);
        CHECK(f.seq().getCurrentStepForParameter(ParamId::Release) == 2);
        const auto filterLow = f.oled(ParamId::Filter);
        const auto releaseLow = f.oled(ParamId::Release);

        f.hand(1.0f);
        recordHeldParameters(); // Real between-step application path, on a rest too.
        CHECK(f.seq().getStepParameterValue(ParamId::Filter, 1) == 1.0f);
        CHECK(f.seq().getStepParameterValue(ParamId::Release, 2) == 1.0f);
        CHECK(f.requested().filterCutoff == 1.0f);
        CHECK(f.requested().releaseTimeSeconds == 1.0f);
        CHECK_FALSE(voiceSystem.getVoiceState(2).shouldRetrigger);
        CHECK(f.oled(ParamId::Filter) != filterLow);
        CHECK(f.oled(ParamId::Release) != releaseLow);
        if (f.config().engine == ENGINE_OSC) {
            CHECK(releaseLow == "10.0ms");
            CHECK(f.oled(ParamId::Release) == "8.00s");
        }
        for (uint8_t other : {0, 1, 3}) {
            CHECK(followsPatch(AppState::sequencers[other]->getStepParameterValue(ParamId::Filter, 1)));
            CHECK(followsPatch(AppState::sequencers[other]->getStepParameterValue(ParamId::Release, 2)));
        }

        f.hand(0.25f);
        processSequencerStep(8); // Real clock-step application path, same calibrated height.
        CHECK(f.requested().filterCutoff == Approx(AppState::performanceInput.recordingValue()));
        CHECK(f.requested().releaseTimeSeconds == Approx(AppState::performanceInput.recordingValue()));
        const auto previous = f.requested().releaseTimeSeconds;
        AppState::performanceInput.observeDistance(-1);
        recordHeldParameters();
        CHECK(f.requested().releaseTimeSeconds == previous);
    }
}

TEST_CASE("Selected-step lidar edits publish the selected Filter and Release while running or stopped", "[lidar][app]") {
    for (bool running : {true, false}) {
        LidarFixture f("Digital");
        f.seq().setStepParameterValue(ParamId::Gate, 0, 1.0f);
        processSequencerStep(0);
        isClockRunning = running;
        uiState.selectedStepForEdit = 6;
        f.press(2);
        f.press(4);
        f.hand(1.0f);
        recordHeldParameters();
        CHECK(f.seq().getStepParameterValue(ParamId::Filter, 6) == 1.0f);
        CHECK(f.seq().getStepParameterValue(ParamId::Release, 6) == 1.0f);
        CHECK(followsPatch(f.seq().getStepParameterValue(ParamId::Filter, 0)));
        CHECK(followsPatch(f.seq().getStepParameterValue(ParamId::Release, 0)));
        CHECK(f.requested().filterCutoff == 1.0f);
        CHECK(f.requested().releaseTimeSeconds == 1.0f);
        CHECK(f.oled(ParamId::Release, 6) == "8.00s");
        CHECK(resetStepToPatch(ParamId::Release));
        CHECK(MusicalValues::releaseSeconds(f.requested().releaseTimeSeconds) == Approx(f.config().defaultRelease));
    }
}

TEST_CASE("Panel envelope targets resolve through the voice's lane bindings", "[lidar][encoder]") {
    auto digital = VoicePresets::getDigitalVoice();
    const auto release = VoiceEdit::baseParameterForLane(ParamId::Release, digital);
    REQUIRE(release == VoiceEdit::Id::Release);
    VoiceEdit::setValue(release, digital, 1.5f);
    CHECK(digital.defaultRelease == 1.5f);
    CHECK(MusicalValues::releaseSeconds(VoiceEdit::laneBase(ParamId::Release, digital)) == Approx(1.5f));
    auto string = VoicePresets::getPresetConfig(VoicePresets::findPreset("WgPluck"));
    CHECK(VoiceEdit::baseParameterForLane(ParamId::Sustain, string) == VoiceEdit::Id::PickPosition);
    const auto stiffness = VoiceEdit::baseParameterForLane(ParamId::Release, string);
    REQUIRE(stiffness == VoiceEdit::Id::Stiffness);
    VoiceEdit::setValue(stiffness, string, 0.8f);
    CHECK(string.wgStiffness == 0.8f);
    CHECK(VoiceEdit::baseParameterForLane(ParamId::Count, digital) == VoiceEdit::Id::Count);
}

TEST_CASE("Real lidar recording changes Digital and Square filter contours and release tails", "[lidar][audio]") {
    for (const char *preset : {"Digital", "Square"}) {
        INFO(preset);
        std::vector<float> contours[2], tails[2];
        for (int high = 0; high < 2; ++high) {
            LidarFixture f(preset);
            f.seq().setStepParameterValue(ParamId::Gate, 0, 1.0f);
            f.seq().setStepParameterValue(ParamId::Attack, 0, 0.0f);
            f.seq().setStepParameterValue(ParamId::Sustain, 0, 0.7f);
            f.press(2);
            f.hand(static_cast<float>(high));
            processSequencerStep(0);
            f.render(2400);
            contours[high] = f.render(4800);
            f.release(2);
            f.press(4);
            recordHeldParameters(); // Set release on the sounding note without retriggering.
            f.render(480);
            // Let the actual sequencer gate-duration path publish note-off.
            for (int tick = 0; tick < 120; ++tick)
                tickSequencerVoices();
            REQUIRE_FALSE(f.requested().isGateHigh);
            f.render(4800); // Skip the short tail and filter ringing.
            tails[high] = f.render(4800);
        }
        double difference = 0;
        for (size_t i = 0; i < contours[0].size(); ++i)
            difference += std::pow(contours[1][i] - contours[0][i], 2);
        CHECK(difference / contours[0].size() > 1e-5);
        if (std::string(preset) == "Digital")
            CHECK(energy(contours[1], true) > energy(contours[0], true));
        CHECK(energy(tails[0]) < 1e-6);
        CHECK(energy(tails[1]) > 1e-4);
    }
}
