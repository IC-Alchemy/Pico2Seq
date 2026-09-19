#include "ui/ParameterEditing.h"
#include "ui/UITransitions.h"
#include "AlchemyUI/src/AlchemyProto.h"
#include "AlchemyUI/src/TileButton.h"
#include "app/AppState.h"
#include "app/StepPlayback.h"
#include "app/VoicePlayback.h"
#include "voice/VoicePresets.h"
#include "voice/MusicalValues.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstring>
#include <cmath>
using Catch::Approx;
using namespace ParameterEditing;
using Domain = Sequencer::ValueDomain;
using Intent = Sequencer::EditIntent;
using Status = Sequencer::WriteStatus;

namespace {
void button(UIState &s, ParamId id, bool pressed = true) {
    parameterButton(s, static_cast<uint8_t>(id), pressed);
}
struct EditFixture {
    UIState state;
    Sequencer sequence;
    VoiceConfig config = VoicePresets::getDigitalVoice();
    EditFixture() {
        VoiceEdit::enablePatch(config);
        VoiceEdit::seedModifiers(sequence);
        sequence.setPlaybackTransform(VoiceEdit::composeLane, &config, VoiceEdit::mapOctave);
    }
};
}

TEST_CASE("Every record button decodes to its lane, base and OLED page", "[editing][recording]") {
    for (uint8_t bit = 0; bit < 6; ++bit) {
        EditFixture f;
        const uint8_t frame[] = {static_cast<uint8_t>(1u << bit), static_cast<uint8_t>(1u << bit), 0};
        const auto offset = alchemy::buttonBlockOffset(alchemy::kTypeButton4);
        const auto block = alchemy::decodeButtonBlock(frame[offset], frame[offset + 1], frame[offset + 2]);
        TileButton physical;
        physical.begin({}, 0);
        physical.update((block.level & (1u << bit)) != 0, true, false, 100);
        REQUIRE(physical.held());
        parameterButton(f.state, bit, physical.held());
        const auto lane = static_cast<ParamId>(bit);
        CHECK(focus(f.state) == lane);
        CHECK(parameterForEncoderMode(f.state.currentEncoderParameter) == lane);
        CHECK(encoderTarget(f.state) == static_cast<VoiceEdit::Id>(bit));
        const auto display = view(f.state, f.sequence, &f.config, 100);
        CHECK(display.page == Page::Parameter);
        CHECK(display.lane == lane);
    }
}

TEST_CASE("Physical focus wins over latch and releases restore press order", "[editing]") {
    UIState s;
    s.shiftHeld = true;
    button(s, ParamId::Note);
    button(s, ParamId::Note, false);
    s.shiftHeld = false;
    button(s, ParamId::Filter);
    REQUIRE(focus(s) == ParamId::Filter);
    CHECK(s.parameterButtonHeld[0]);
    CHECK(s.parameterButtonHeld[2]);
    button(s, ParamId::Decay);
    CHECK(focus(s) == ParamId::Decay);
    button(s, ParamId::Decay, false);
    CHECK(focus(s) == ParamId::Filter);
    button(s, ParamId::Filter, false);
    CHECK(focus(s) == ParamId::Note);
    CHECK(encoderTarget(s) == VoiceEdit::Id::Note);
}

TEST_CASE("Transitions clear latch history without resurrecting old holds", "[editing]") {
    for (int transition = 0; transition < 5; ++transition) {
        UIState s;
        s.shiftHeld = true;
        button(s, ParamId::Note);
        button(s, ParamId::Note, false);
        s.shiftHeld = false;
        button(s, ParamId::Filter);
        switch (transition) {
        case 0: UITransitions::clearParameterHolds(s); s.alchemyMode = UIState::AlchemyMode::Utility; break;
        case 1: UITransitions::toggleSlide(s); UITransitions::toggleSlide(s); break;
        case 2: UITransitions::selectPerformanceVoice(s, 1); break;
        case 3: UITransitions::closeSettings(s); s.voiceEditor.active = true; break;
        case 4: UITransitions::focusPad(s, 1, 3); break;
        }
        s.voiceEditor.active = false;
        button(s, ParamId::Filter, false);
        CHECK(focus(s) == ParamId::Count);
        CHECK(s.latchedParameter == -1);
        for (bool armed : s.parameterButtonHeld) CHECK_FALSE(armed);
        button(s, ParamId::Attack);
        CHECK(focus(s) == ParamId::Attack);
        CHECK_FALSE(s.parameterButtonHeld[0]);
    }
}

TEST_CASE("Shared writes validate indices, quantize and compare final storage", "[editing][recording]") {
    Sequencer seq;
    for (uint8_t id = 0; id < 6; ++id) {
        const auto lane = static_cast<ParamId>(id);
        seq.setStepParameterValue(ParamId::Gate, 3, 1);
        for (float value : {0.0f, 0.5f, 1.0f}) {
            const auto result = seq.writeParameter(lane, 3, value, Domain::Normalized, Intent::Recording, 3);
            REQUIRE(result.accepted());
            CHECK(result.storedValue == Approx(id == 0 ? value * 36.0f : value));
            CHECK(seq.writeParameter(lane, 3, value, Domain::Normalized, Intent::Recording, 3).status == Status::Unchanged);
        }
    }
    CHECK(seq.writeParameter(ParamId::Velocity, 0, 2, Domain::Normalized, Intent::Explicit).changed());
    CHECK(seq.writeParameter(ParamId::Velocity, 0, 7, Domain::Normalized, Intent::Explicit).status == Status::Unchanged);
    CHECK(seq.writeParameter(ParamId::Note, 0, 3.1f, Domain::Stored, Intent::Explicit).storedValue == 3);
    CHECK(seq.writeParameter(ParamId::Note, 0, 3.4f, Domain::Stored, Intent::Explicit).status == Status::Unchanged);
    CHECK(seq.writeParameter(ParamId::Gate, 0, 0.4f, Domain::Stored, Intent::Explicit).status == Status::Unchanged);
    CHECK(seq.writeParameter(ParamId::Gate, 0, 0.6f, Domain::Stored, Intent::Explicit).storedValue == 1);
    for (const auto lane : {ParamId::Count, static_cast<ParamId>(255)})
        CHECK_FALSE(seq.writeParameter(lane, 0, 1, Domain::Stored, Intent::Explicit).accepted());
    for (int step : {-1, 64, 256})
        CHECK_FALSE(seq.writeParameter(ParamId::Filter, step, 1, Domain::Stored, Intent::Explicit).accepted());
    CHECK(mapNormalizedValueToParamRange(ParamId::Count, 0.5f) == 0);
    CHECK(seq.getCurrentStepForParameter(ParamId::Count) == 0);
}

TEST_CASE("Recording protects rest pitch but explicit edits and other lanes do not", "[editing][recording]") {
    EditFixture f;
    f.state.selectedStepForEdit = 0;
    button(f.state, ParamId::Note);
    CHECK_FALSE(record(f.state, f.sequence, 0, 1, true).accepted());
    CHECK(view(f.state, f.sequence, &f.config, 1).values.noteIndex == f.config.baseNote);
    REQUIRE(encoder(f.state, f.sequence, f.config, 0.2f).write.changed());
    CHECK(f.sequence.getStepParameterValue(ParamId::Note, 0) > 0);
    for (uint8_t lane = 1; lane < 6; ++lane) {
        UITransitions::clearParameterHolds(f.state);
        button(f.state, static_cast<ParamId>(lane));
        REQUIRE(record(f.state, f.sequence, 0, 1, true).changed());
        REQUIRE_FALSE(record(f.state, f.sequence, 0, 0, false).accepted());
        CHECK(f.sequence.getStepParameterValue(static_cast<ParamId>(lane), 0) == 1);
    }
}

TEST_CASE("Clock recording keeps armed lanes, selected voice and unequal cursors", "[editing][recording]") {
    UIState s;
    s.selectedVoiceIndex = 2;
    button(s, ParamId::Note);
    button(s, ParamId::Filter);
    for (uint8_t voice = 0; voice < 4; ++voice) {
        Sequencer seq;
        seq.setParameterStepCount(ParamId::Note, 3);
        seq.setParameterStepCount(ParamId::Gate, 5);
        seq.setParameterStepCount(ParamId::Filter, 7);
        seq.setStepParameterValue(ParamId::Gate, 2, 1); // 257 % 5, not Note's 257 % 3
        seq.start();
        VoiceState requested;
        advance(seq, voice, 257, s, 1, true, requested);
        CHECK(seq.getCurrentStepForParameter(ParamId::Note) == 2);
        CHECK(seq.getCurrentStepForParameter(ParamId::Filter) == 5);
        CHECK(requested.noteIndex == (voice == 2 ? 36 : 0));
        CHECK(requested.filterCutoff == (voice == 2 ? 1.0f : 0.5f));
        seq.setStepParameterValue(ParamId::Gate, 3, 1); // 258 % 5; Note cursor is 0 (a rest)
        advance(seq, voice, 258, s, 0.5f, true, requested);
        CHECK(requested.noteIndex == (voice == 2 ? 18 : 0));
        seq.setStepParameterValue(ParamId::Gate, 4, 0);
        seq.setStepParameterValue(ParamId::Gate, 1, 1); // Note cursor at 259 is 1
        advance(seq, voice, 259, s, 1, true, requested);
        CHECK(seq.getStepParameterValue(ParamId::Note, 1) == 0);
    }
}

TEST_CASE("Base turns preserve modifiers, selected-step turns preserve bases", "[editing]") {
    for (uint8_t lane = 0; lane < 6; ++lane) {
        EditFixture f;
        const auto id = static_cast<ParamId>(lane);
        button(f.state, id);
        const float stored = f.sequence.getStepParameterValue(id, 0);
        REQUIRE(encoder(f.state, f.sequence, f.config, 0.2f).patchChanged);
        for (uint8_t step = 0; step < 16; ++step)
            CHECK(f.sequence.getStepParameterValue(id, step) == stored);
        const auto baseId = encoderTarget(f.state);
        const float base = VoiceEdit::value(baseId, f.config);
        f.state.selectedStepForEdit = 0;
        REQUIRE(encoder(f.state, f.sequence, f.config, 0.2f).write.changed());
        CHECK(VoiceEdit::value(baseId, f.config) == base);
    }
}

TEST_CASE("Small encoder motion accumulates and target changes discard remainder", "[editing]") {
    EditFixture f;
    f.state.selectedStepForEdit = 0;
    f.state.currentEncoderParameter = EncoderParameterMode::Note;
    for (int i = 0; i < 9; ++i) encoder(f.state, f.sequence, f.config, 0.01f);
    CHECK(f.sequence.getStepParameterValue(ParamId::Note, 0) > 0);
    encoder(f.state, f.sequence, f.config, 0.0001f);
    f.state.selectedStepForEdit = 1;
    syncGesture(f.state);
    CHECK(f.state.editGesture.motion.pending() == 0);
    f.state.controlsWaitRelease = true;
    CHECK_FALSE(encoder(f.state, f.sequence, f.config, 1).write.accepted());
    CHECK(f.state.editGesture.motion.pending() == 0);
}

TEST_CASE("Manual step edits survive stationary hand until rearmed", "[editing][recording]") {
    EditFixture f;
    f.state.selectedStepForEdit = 3;
    button(f.state, ParamId::Filter);
    REQUIRE(fader(f.state, f.sequence, ParamId::Filter, 0.3f).changed());
    REQUIRE(encoder(f.state, f.sequence, f.config, 0.2f).write.changed());
    const auto manual = f.sequence.getStepParameterValue(ParamId::Filter, 3);
    REQUIRE(manual > 0.3f); // fader first, encoder wins
    for (int i = 0; i < 5; ++i) CHECK_FALSE(record(f.state, f.sequence, 3, 1, true).accepted());
    f.sequence.start();
    VoiceState requested;
    advance(f.sequence, 0, 257, f.state, 1, true, requested);
    CHECK_FALSE(record(f.state, f.sequence, 3, 1, true).accepted());
    CHECK(f.sequence.getStepParameterValue(ParamId::Filter, 3) == manual);
    record(f.state, f.sequence, 3, 1, false);
    CHECK(record(f.state, f.sequence, 3, 1, true).changed());
    CHECK(encoder(f.state, f.sequence, f.config, 0.2f).write.status == Status::Unchanged);
    CHECK_FALSE(record(f.state, f.sequence, 3, 0, true).accepted()); // limit still owned
}

TEST_CASE("Manual ownership is specific to target and gesture", "[editing]") {
    for (int change = 0; change < 6; ++change) {
        EditFixture f;
        f.state.selectedStepForEdit = 2;
        button(f.state, ParamId::Filter);
        encoder(f.state, f.sequence, f.config, 0.1f);
        REQUIRE_FALSE(f.state.editGesture.permitsLidar(ParamId::Filter));
        CHECK(f.state.editGesture.permitsLidar(ParamId::Attack));
        switch (change) {
        case 0: f.state.selectedVoiceIndex = 1; break;
        case 1: f.state.selectedStepForEdit = 4; break;
        case 2: button(f.state, ParamId::Attack); break;
        case 3: f.state.alchemyMode = UIState::AlchemyMode::Utility; break;
        case 4: f.state.voiceEditor.active = true; break;
        case 5: button(f.state, ParamId::Filter, false); break;
        }
        syncGesture(f.state);
        CHECK(f.state.editGesture.permitsLidar(ParamId::Filter));
    }
    EditFixture base;
    button(base.state, ParamId::Filter);
    REQUIRE(encoder(base.state, base.sequence, base.config, 0.2f).patchChanged);
    CHECK(record(base.state, base.sequence, 0, 0.3f, true).changed());
}

TEST_CASE("OLED snapshot chooses base feedback before holds and shares step fallback", "[editing]") {
    EditFixture f;
    button(f.state, ParamId::Filter);
    f.sequence.setStepParameterValue(ParamId::Filter, 0, 0);
    encoder(f.state, f.sequence, f.config, 0.2f);
    f.state.encoderBaseViewUntil = 1000;
    auto display = view(f.state, f.sequence, &f.config, 100);
    CHECK(display.source == ValueSource::Base);
    CHECK(display.values.filterCutoff == Approx(VoiceEdit::composeLane(ParamId::Filter, 0.5f, &f.config)));
    display = view(f.state, f.sequence, &f.config, 1001);
    CHECK(display.source == ValueSource::Cursors);
    CHECK(display.values.filterCutoff == 0);
    UITransitions::clearParameterHolds(f.state);
    f.state.selectedStepForEdit = 4;
    f.state.currentEncoderParameter = EncoderParameterMode::Decay;
    display = view(f.state, f.sequence, &f.config, 1002);
    CHECK(display.page == Page::Parameter);
    CHECK(display.lane == ParamId::Decay);
    CHECK(display.source == ValueSource::SelectedStep);
    CHECK(display.step == 4);
    CHECK(display.values.decayTimeSeconds == f.sequence.getPlaybackStep(4).decayTimeSeconds);
    f.state.voiceEditor.active = true;
    CHECK(view(f.state, f.sequence, &f.config, 1002).page == Page::Editor);
}

TEST_CASE("Display reads cannot mutate storage, cursors or note duration", "[editing]") {
    EditFixture f;
    button(f.state, ParamId::Note);
    f.sequence.setStepParameterValue(ParamId::Gate, 0, 1);
    f.sequence.setStepParameterValue(ParamId::GateLength, 0, 1.0f / 120);
    f.sequence.start();
    VoiceState requested;
    f.sequence.playStepNow(0, &requested);
    for (int i = 0; i < 100; ++i) view(f.state, f.sequence, &f.config, i);
    CHECK(f.sequence.getCurrentStep() == 0);
    CHECK(f.sequence.getStepParameterValue(ParamId::Note, 0) == 0);
    // Patch base composes gate duration; use an untransformed duration below.
    f.sequence.setPlaybackTransform(nullptr, nullptr);
    f.sequence.playStepNow(0, &requested);
    for (int i = 0; i < 100; ++i) view(f.state, f.sequence, &f.config, i);
    REQUIRE(f.sequence.tickNoteDuration(&requested));
    CHECK_FALSE(requested.isGateHigh);
}

TEST_CASE("Accepted app edits publish once without extending or reviving gates", "[editing][voice_playback]") {
    uiState = UIState{};
    voiceSystem = VoiceSystem{};
    voiceManager = std::make_unique<VoiceManager>(4);
    for (uint8_t i = 0; i < 4; ++i) {
        *AppState::sequencers[i] = Sequencer{};
        voiceSystem.setVoiceId(i, voiceManager->addVoice(VoicePresets::getDigitalVoice()));
    }
    voiceManager->init(48000);
    unsigned updates = 0;
    VoiceState last;
    voiceManager->setVoiceUpdateCallback([&](uint8_t, const VoiceState &s) { ++updates; last = s; });
    auto &seq = *AppState::sequencers[0];
    seq.start();
    seq.setStepParameterValue(ParamId::Gate, 0, 1);
    seq.setStepParameterValue(ParamId::GateLength, 0, 3.0f / 120);
    seq.playStepNow(0, &voiceSystem.getVoiceState(0));
    publishVoiceState(0, voiceSystem.getVoiceState(0));
    button(uiState, ParamId::Filter);
    isClockRunning = true;
    updateParametersForStepNormalized(0, 0.2f);
    REQUIRE(updates == 2);
    CHECK(last.isGateHigh);
    CHECK_FALSE(last.shouldRetrigger);
    CHECK(last.gateLengthTicks == 3);
    updateParametersForStepNormalized(0, 0.2f);
    CHECK(updates == 2);
    updateParametersForStepNormalized(1, 0.9f);
    CHECK(last.filterCutoff == 0.2f); // a different stored step is not played early
    for (int i = 0; i < 3; ++i) tickSequencerVoices();
    CHECK_FALSE(last.isGateHigh);
    updateParametersForStepNormalized(0, 0.3f);
    CHECK_FALSE(last.isGateHigh);
    CHECK_FALSE(last.shouldRetrigger);
    isClockRunning = false;
    updateParametersForStepNormalized(1, 0.8f);
    CHECK(last.filterCutoff == 0.8f);
    CHECK_FALSE(last.isGateHigh);
    const unsigned before = updates;
    uiState.selectedVoiceIndex = 255;
    updateParametersForStepNormalized(0, 1);
    CHECK(updates == before);
    voiceManager.reset();
    uiState = UIState{};
}

TEST_CASE("Restored bypass settings remain truthful and preserved", "[editing]") {
    EditFixture f;
    f.config.hasFilter = false;
    f.config.hasEnvelope = false;
    char value[32];
    for (auto lane : {ParamId::Filter, ParamId::Attack, ParamId::Decay}) {
        UITransitions::clearParameterHolds(f.state);
        button(f.state, lane);
        record(f.state, f.sequence, 0, 1, true);
        const auto display = view(f.state, f.sequence, &f.config, 1);
        MusicalValues::format(lane, display.values, f.config, nullptr, 120, value, sizeof(value));
        CHECK(std::strcmp(value, lane == ParamId::Filter ? "Bypass" : "Off") == 0);
    }
    CHECK_FALSE(f.config.hasFilter);
    CHECK_FALSE(f.config.hasEnvelope);
}

namespace {
// Render the real queued Voice path. Compare energy over a useful envelope
// segment rather than requiring phase-identical waveforms.
double renderedEnergy(VoiceConfig config, ParamId lane, float normalized,
                      int firstSample, int lastSample) {
    VoiceEdit::enablePatch(config);
    config.hasOverdrive = false;
    config.highPassFreq = 20;
    config.highPassRes = 0;
    config.hasFilter = lane == ParamId::Filter;
    config.hasEnvelope = lane == ParamId::Attack || lane == ParamId::Decay;
    config.defaultSustain = lane == ParamId::Decay ? 0.0f : 1.0f;
    config.defaultAttack = 0.001f;
    config.defaultDecay = 1;
    config.baseNote = 12;
    config.baseVelocity = 0.8f;
    Sequencer seq;
    VoiceEdit::seedModifiers(seq);
    seq.setPlaybackTransform(VoiceEdit::composeLane, &config, VoiceEdit::mapOctave);
    seq.setStepParameterValue(ParamId::Gate, 0, 1);
    UIState s;
    button(s, lane);
    REQUIRE(record(s, seq, 0, normalized, true).accepted());
    Voice voice(0, config);
    voice.init(48000);
    voice.setScaleTable(nullptr, 0);
    VoiceState request;
    seq.playStepNow(0, &request);
    voice.updateParameters(request);
    double energy = 0;
    for (int i = 0; i < lastSample; ++i) {
        const float sample = voice.process();
        REQUIRE(std::isfinite(sample));
        if (i >= firstSample) energy += double(sample) * sample;
    }
    CHECK_FALSE(voice.getState().shouldRetrigger);
    return energy / (lastSample - firstSample);
}
}

TEST_CASE("Oscillator DSP responds to recorded continuous lanes after queue consumption", "[editing][editing_audio]") {
    for (const auto config : {VoicePresets::getDigitalVoice(), VoicePresets::getSquareVoice()}) {
        const auto quiet = renderedEnergy(config, ParamId::Velocity, 0.15f, 4800, 9600);
        const auto loud = renderedEnergy(config, ParamId::Velocity, 0.85f, 4800, 9600);
        CAPTURE(quiet, loud);
        REQUIRE(loud > quiet * 4);
        const auto dark = renderedEnergy(config, ParamId::Filter, 0.05f, 4800, 9600);
        const auto bright = renderedEnergy(config, ParamId::Filter, 0.95f, 4800, 9600);
        CAPTURE(dark, bright);
        REQUIRE(bright > dark * 1.5);
        const auto fastAttack = renderedEnergy(config, ParamId::Attack, 0.05f, 0, 2400);
        const auto slowAttack = renderedEnergy(config, ParamId::Attack, 0.95f, 0, 2400);
        CAPTURE(fastAttack, slowAttack);
        REQUIRE(fastAttack > slowAttack * 4);
        const auto shortDecay = renderedEnergy(config, ParamId::Decay, 0.05f, 9600, 14400);
        const auto longDecay = renderedEnergy(config, ParamId::Decay, 0.95f, 9600, 14400);
        CAPTURE(shortDecay, longDecay);
        REQUIRE(longDecay > shortDecay * 4);
    }
}

TEST_CASE("Analog Slave lane still controls pitch after recording", "[editing][editing_audio]") {
    auto config = VoicePresets::getAnalogVoice();
    VoiceEdit::enablePatch(config);
    Sequencer sequence;
    VoiceEdit::seedModifiers(sequence);
    sequence.setPlaybackTransform(VoiceEdit::composeLane, &config, VoiceEdit::mapOctave);
    sequence.setStepParameterValue(ParamId::Gate, 0, 1);
    UIState s;
    button(s, ParamId::Velocity);
    Voice voice(0, config);
    voice.init(48000);
    VoiceState requested;
    sequence.playStepNow(0, &requested);
    voice.updateParameters(requested);
    for (int i = 0; i < 64; ++i) voice.process();
    const float master = voice.getCachedFrequency(0);
    const float slave = voice.getCachedSlaveFrequency(0);
    REQUIRE(record(s, sequence, 0, 1, true).changed());
    sequence.refreshVoiceParameters(&requested);
    voice.updateParameters(requested);
    for (int i = 0; i < 64; ++i) voice.process();
    CHECK(voice.getCachedFrequency(0) == Approx(master));
    CHECK(voice.getCachedSlaveFrequency(0) > slave * 1.5f);
    CHECK_FALSE(VoiceParameters::velocityToAmplitude(config));
}

TEST_CASE("Application clock routing suppresses missing hands and modal input", "[editing][recording]") {
    voiceManager.reset();
    voiceSystem = VoiceSystem{};
    uiState = UIState{};
    isClockRunning = true;
    uiState.selectedVoiceIndex = 3;
    button(uiState, ParamId::Velocity);
    button(uiState, ParamId::Filter);
    button(uiState, ParamId::Attack);
    button(uiState, ParamId::Decay);
    for (auto *seq : AppState::sequencers) { *seq = Sequencer{}; seq->start(); }
    AppState::performanceInput.observeDistance(SensorConstants::DistanceSensor::MAX_DISTANCE_HEIGHT_MM);
    processSequencerStep(0);
    for (uint8_t voice = 0; voice < 4; ++voice) {
        for (uint8_t lane = 1; lane < 5; ++lane) {
            const auto id = static_cast<ParamId>(lane);
            CHECK(AppState::sequencers[voice]->getStepParameterValue(id, 0) ==
                (voice == 3 ? 1.0f : parameterValueAsFloat(parameterDefinition(id)->defaultValue)));
        }
    }
    AppState::performanceInput.observeDistance(-1);
    processSequencerStep(1);
    CHECK(AppState::sequencers[3]->getStepParameterValue(ParamId::Filter, 1) == 0.5f);
    AppState::performanceInput.observeDistance(SensorConstants::DistanceSensor::MAX_DISTANCE_HEIGHT_MM);
    uiState.controlsWaitRelease = true;
    processSequencerStep(2);
    CHECK(AppState::sequencers[3]->getStepParameterValue(ParamId::Filter, 2) == 0.5f);
    uiState.voiceEditor.active = true;
    processSequencerStep(3);
    CHECK(AppState::sequencers[3]->getCurrentStep() == 2);
    uiState = UIState{};
    isClockRunning = false;
    AppState::performanceInput = {};
}
