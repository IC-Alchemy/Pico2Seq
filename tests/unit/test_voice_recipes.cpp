#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "voice/Voice.h"
#include "voice/VoiceManager.h"
#include "voice/VoicePresets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

using namespace Catch::Matchers;

namespace {
VoiceState seededState(const VoiceConfig &config)
{
    Sequencer seq;
    seq.initializeParameters();
    VoiceParameters::seedTracks(seq, config);
    VoiceState s;
    s.noteIndex = 9.0f;
    s.velocityLevel = 0.8f;
    s.filterCutoff = seq.getStepParameterValue(ParamId::Filter, 0);
    s.attackTimeSeconds = seq.getStepParameterValue(ParamId::Attack, 0);
    s.decayTimeSeconds = seq.getStepParameterValue(ParamId::Decay, 0);
    s.isGateHigh = true;
    return s;
}
float *lane(VoiceState &s, ParamId id)
{
    if (id == ParamId::Filter) return &s.filterCutoff;
    if (id == ParamId::Attack) return &s.attackTimeSeconds;
    return &s.decayTimeSeconds;
}
// A custom recipe tests the shared pitch/glide path using its emitted increment.
float pitchProbe(float inc, const VoiceConfig &, float *) noexcept { return inc; }
float negativeProbe(float inc, const VoiceConfig &, float *) noexcept { return -inc; }
constexpr auto kPitchProbe = makeVoiceRecipe<0>(pitchProbe);
constexpr auto kNegativeProbe = makeVoiceRecipe<0>(negativeProbe);
void configureProbe(float rate, const VoiceConfig &c, float *s) noexcept { s[0] = c.macro1 / rate; }
float configuredProbe(float, const VoiceConfig &, float *s) noexcept { return s[0]; }
constexpr auto kConfiguredProbe = makeVoiceRecipe<1>(configuredProbe, true, configureProbe);
VoiceConfig probeConfig()
{
    VoiceConfig c = VoicePresets::getPresetConfigByName("FMGlass");
    c.recipe = &kPitchProbe;
    c.hasEnvelope = false;
    c.highPassFreq = 0.0f;
    c.outputLevel = 1.0f;
    return c;
}
}

TEST_CASE("Preset names share one case-insensitive registry", "[voice][presets]")
{
    for (uint8_t p = 0; p < VoicePresets::getPresetCount(); ++p) {
        const std::string name = VoicePresets::getPresetName(p);
        REQUIRE(VoicePresets::findPreset(name) == p);
        REQUIRE(&VoicePresets::getPresetConfigByName(name) == &VoicePresets::getPresetConfig(p));
    }
    REQUIRE(VoicePresets::findPreset("fMgLaSs") == static_cast<int>(VoicePresets::Id::FmGlass));
    REQUIRE(VoicePresets::findPreset("") == -1);
    REQUIRE(&VoicePresets::getPresetConfigByName("missing") == &VoicePresets::getAnalogVoice());
    REQUIRE(VoiceManager::getAvailablePresets().size() == VoicePresets::getPresetCount());
    for (const auto &name : VoiceManager::getAvailablePresets()) REQUIRE(VoicePresets::findPreset(name) >= 0);
    VoiceManager manager(1);
    const auto id = manager.addVoice("spectral");
    REQUIRE(id != 0);
    REQUIRE(manager.getVoiceConfig(id)->recipe ==
            VoicePresets::getPresetConfigByName("Spectral").recipe);
}

TEST_CASE("Preset pages reach the whole bank without addressing nonexistent pads", "[voice][presets]")
{
    for (uint8_t count : {0, 1, 24, 25, 65, 255}) {
        unsigned visited = 0;
        for (uint8_t page = 0; page < VoicePresets::presetPageCount(count); ++page) {
            unsigned onPage = 0;
            for (uint8_t pad = 0; pad < 64; ++pad) {
                const int index = VoicePresets::presetIndexForPad(pad, count, page);
                if (index < 0) continue;
                REQUIRE(pad >= 8);
                REQUIRE(pad < 32);
                REQUIRE(index == visited++);
                ++onPage;
            }
            REQUIRE(onPage == VoicePresets::presetCountOnPage(count, page));
        }
        REQUIRE(visited == count);
    }
    REQUIRE(VoicePresets::changePresetPage(0, -1, 65) == 0);
    REQUIRE(VoicePresets::changePresetPage(1, 1, 65) == 2);
    REQUIRE(VoicePresets::changePresetPage(2, 1, 65) == 2);
    REQUIRE(VoicePresets::changePresetPage(2, 1, 0) == 0);
}

TEST_CASE("Preset seeding survives the first audio update and preserves musical tracks", "[voice][presets]")
{
    for (uint8_t p = 0; p < VoicePresets::getPresetCount(); ++p) {
        INFO(VoicePresets::getPresetName(p));
        const auto &config = VoicePresets::getPresetConfig(p);
        Sequencer seq;
        seq.initializeParameters();
        seq.setParameterStepCount(ParamId::Filter, 7);
        seq.setParameterStepCount(ParamId::Attack, 11);
        seq.setParameterStepCount(ParamId::Decay, 5);
        const std::array<ParamId, 5> shared = {ParamId::Note, ParamId::Octave,
            ParamId::GateLength, ParamId::Gate, ParamId::Slide};
        for (auto id : shared) seq.setStepParameterValue(id, 0, 1.0f);
        std::array<float, 5> before{};
        for (size_t i = 0; i < shared.size(); ++i) before[i] = seq.getStepParameterValue(shared[i], 0);
        VoiceParameters::seedTracks(seq, config);
        for (size_t i = 0; i < shared.size(); ++i) REQUIRE(seq.getStepParameterValue(shared[i], 0) == before[i]);
        REQUIRE(seq.getParameterStepCount(ParamId::Filter) == 7);
        for (uint8_t step = 0; step < 7; ++step)
            REQUIRE(seq.getStepParameterValue(ParamId::Filter, step) == seq.getStepParameterValue(ParamId::Filter, 0));
        Voice voice(0, config);
        voice.init(48000.0f);
        voice.updateParameters(seededState(config));
        voice.process();
        for (const auto &b : VoiceParameters::layout(config).slots) {
            if (b.seed && b.target)
                REQUIRE_THAT(voice.getConfig().*(b.target), WithinAbs(config.*(b.target), 0.0001f));
        }
    }
    char text[24]{};
    REQUIRE(VoiceParameters::formatValue(VoicePresets::getWaveguidePluckVoice(), ParamId::Decay, 1.0f, text, sizeof(text)));
    REQUIRE(std::string(text) == "10.00s");
    REQUIRE_FALSE(VoiceParameters::formatValue(VoicePresets::getAnalogVoice(), ParamId::Note, 10.0f, text, sizeof(text)));
}

TEST_CASE("Each recipe timbre lane changes the emitted waveform", "[voice][recipes]")
{
    for (uint8_t p = 0; p < VoicePresets::getPresetCount(); ++p) {
        const auto &config = VoicePresets::getPresetConfig(p);
        if (config.engine != ENGINE_RECIPE) continue;
        for (auto id : {ParamId::Filter, ParamId::Attack, ParamId::Decay}) {
            INFO(VoicePresets::getPresetName(p) << " lane " << static_cast<int>(id));
            Voice low(0, config), high(0, config);
            low.init(48000.0f);
            high.init(48000.0f);
            auto lo = seededState(config), hi = lo;
            *lane(lo, id) = 0.15f;
            *lane(hi, id) = 0.85f;
            low.updateParameters(lo);
            high.updateParameters(hi);
            double difference = 0.0;
            for (int i = 0; i < 4096; ++i) {
                const float a = low.process(), b = high.process();
                REQUIRE(std::isfinite(a));
                REQUIRE(std::isfinite(b));
                difference += std::abs(a - b);
            }
            REQUIRE(difference / 4096 > 0.001);
        }
    }
}

TEST_CASE("Recipe parameter extremes remain bounded across pitches and sample rates", "[voice][recipes]")
{
    for (uint8_t p = 0; p < VoicePresets::getPresetCount(); ++p) {
        const auto &config = VoicePresets::getPresetConfig(p);
        if (config.engine != ENGINE_RECIPE) continue;
        for (float rate : {44100.0f, 48000.0f, 96000.0f}) {
            Voice v(0, config);
            v.init(rate);
            auto state = seededState(config);
            for (unsigned combination = 0; combination < 8; ++combination) {
                INFO(VoicePresets::getPresetName(p) << " rate " << rate << " corner " << combination);
                state.filterCutoff = (combination & 1) ? 1.0f : 0.0f;
                state.attackTimeSeconds = (combination & 2) ? 1.0f : 0.0f;
                state.decayTimeSeconds = (combination & 4) ? 1.0f : 0.0f;
                state.noteIndex = combination * 3.0f;
                state.shouldRetrigger = true;
                v.updateParameters(state);
                for (int i = 0; i < 1024; ++i) {
                    const float sample = v.process();
                    REQUIRE(std::isfinite(sample));
                    REQUIRE(std::abs(sample) < 2.0f);
                }
            }
        }
    }
}

TEST_CASE("Recipe voices share note octave velocity slide and queued switching", "[voice][recipes]")
{
    auto config = probeConfig();
    Voice voice(0, config);
    voice.init(48000.0f);
    auto state = seededState(config);
    state.velocityLevel = 1.0f;
    voice.updateParameters(state);
    const float initial = voice.process();
    REQUIRE(initial > 0.0f);
    state.octaveOffset = 12; // sequencer converts the octave lane to semitones
    voice.updateParameters(state);
    REQUIRE_THAT(voice.process(), WithinRel(initial * 2.0f, 0.001f));
    state.velocityLevel = 0.25f;
    voice.updateParameters(state);
    REQUIRE_THAT(voice.process(), WithinRel(initial * 0.5f, 0.001f));
    state.velocityLevel = 1.0f;
    state.octaveOffset = 0;
    state.hasSlide = true;
    voice.updateParameters(state);
    const float sliding = voice.process();
    REQUIRE(sliding > initial);
    REQUIRE(sliding < initial * 2.0f);
    for (int i = 0; i < 24000; ++i) voice.process();
    REQUIRE_THAT(voice.process(), WithinRel(initial, 0.002f));
    config.recipe = &kNegativeProbe;
    voice.setConfig(config);
    REQUIRE(voice.process() > 0.0f); // source selection waits for gate-low
    voice.setGate(false);
    REQUIRE(voice.process() < 0.0f);
    voice.setGate(true);
    REQUIRE(voice.process() < 0.0f);
}

TEST_CASE("Recipe envelope responds to gate-off and retrigger", "[voice][recipes]")
{
    auto config = VoicePresets::getPresetConfigByName("FMBass");
    Voice v(0, config);
    v.init(48000.0f);
    auto s = seededState(config);
    v.updateParameters(s);
    for (int i = 0; i < 4096; ++i) v.process();
    v.setGate(false);
    for (int i = 0; i < 48000; ++i) v.process();
    REQUIRE(std::abs(v.process()) < 0.0001f);
    v.setGate(true);
    float peak = 0.0f;
    for (int i = 0; i < 2048; ++i) peak = std::max(peak, std::abs(v.process()));
    REQUIRE(peak > 0.01f);
    s.shouldRetrigger = true;
    v.updateParameters(s);
    v.process();
    REQUIRE_FALSE(v.getState().shouldRetrigger);
}

TEST_CASE("Recipe coefficients follow sample rate and controls and survive trigger resets", "[voice][recipes]")
{
    auto config = probeConfig();
    config.recipe = &kConfiguredProbe;
    Voice voice(0, config);
    voice.init(96000.0f);
    auto state = seededState(config);
    state.velocityLevel = 1.0f;
    state.filterCutoff = 0.5f; // square curve: Index = 0.25
    voice.updateParameters(state);
    REQUIRE_THAT(voice.process(), WithinAbs(0.25f / 96000.0f, 1.0e-9f));
    state.filterCutoff = 1.0f;
    state.shouldRetrigger = true;
    voice.updateParameters(state);
    REQUIRE_THAT(voice.process(), WithinAbs(1.0f / 96000.0f, 1.0e-9f));
    voice.setGate(false);
    voice.process();
    voice.setGate(true);
    REQUIRE_THAT(voice.process(), WithinAbs(1.0f / 96000.0f, 1.0e-9f));
}
