// Sitar timbre lanes: while ENGINE_SITAR is active the sequencer's
// Filter/Attack/Decay lanes become JAWARI/PICK/TARAF (VoiceParameterLayout),
// the OLED lane names follow the bindings, and every other engine's lane
// behavior stays unchanged. The DSP model is covered by test_sitar_voice.cpp
// ([rpdsp][sitar]); the engine wiring by test_sitar_engine.cpp
// ([sitar_engine]).
#include <catch2/catch_test_macros.hpp>
#include "voice/Voice.h"
#include "voice/VoiceEditParameters.h"
#include <cmath>
#include <cstring>
#include <string>

namespace {
constexpr float kSampleRate = 48000.0f;

// A raw sitar-engine config using the legacy (paramSet-derived) lane set —
// the factory preset is a later task.
VoiceConfig sitarLaneConfig()
{
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_SITAR;
    c.paramSet = PARAMSET_SITAR;
    c.hasEnvelope = false; // natural string/jawari/body decay instead of a VCA
    c.hasFilter = false;
    c.highPassFreq = 0.0f;
    c.highPassRes = 0.0f;
    c.outputLevel = 1.0f;
    return c;
}

VoiceState laneState(float filter, float attack, float decay, float velocity = 0.8f)
{
    VoiceState s{};
    s.noteIndex = 9.0f;
    s.velocityLevel = velocity;
    s.filterCutoff = filter;
    s.attackTimeSeconds = attack;
    s.decayTimeSeconds = decay;
    s.isGateHigh = true;
    return s;
}
} // namespace

TEST_CASE("Sitar engine remaps Filter/Attack/Decay to JAWARI/PICK/TARAF",
          "[sitar_lanes]")
{
    const auto c = sitarLaneConfig();
    const auto &layout = VoiceParameters::layout(c);
    // The lanes stop driving the ADSR (sitar rings naturally) and velocity
    // must live only in the pluck excitation, never in the output VCA.
    REQUIRE_FALSE(layout.envelopeFromTracks);
    REQUIRE_FALSE(layout.velocityToAmplitude);
    REQUIRE_FALSE(VoiceParameters::velocityToAmplitude(c));

    struct Lane
    {
        ParamId id;
        const char *name;
        float VoiceConfig::*target;
    };
    const Lane lanes[] = {
        {ParamId::Filter, "JAWARI", &VoiceConfig::sitarJawari},
        {ParamId::Attack, "PICK", &VoiceConfig::sitarPickHardness},
        {ParamId::Decay, "TARAF", &VoiceConfig::sitarTarafAmount},
    };
    for (const auto &lane : lanes)
    {
        const auto &b = VoiceParameters::binding(c, lane.id);
        INFO(lane.name);
        REQUIRE(b.name != nullptr);
        REQUIRE(std::string(b.name) == lane.name);
        REQUIRE(b.target == lane.target);
        REQUIRE(b.seed);
        REQUIRE(b.minimum == 0.0f);
        REQUIRE(b.maximum == 1.0f);
    }

    // The other lanes keep their shared musical meaning: no binding target,
    // no custom label (Note -> pitch, Velocity -> pluck energy, Octave,
    // GateLength -> duration, Slide -> meend, Gate -> pluck/retrigger).
    for (ParamId id : {ParamId::Note, ParamId::Velocity, ParamId::Octave,
                       ParamId::GateLength, ParamId::Gate, ParamId::Slide})
    {
        const auto &b = VoiceParameters::binding(c, id);
        INFO(static_cast<int>(id));
        REQUIRE(b.target == nullptr);
        REQUIRE(b.name == nullptr);
    }
}

TEST_CASE("Sitar lane values land in the sitar VoiceConfig fields",
          "[sitar_lanes]")
{
    auto c = sitarLaneConfig();
    VoiceParameters::apply(c, laneState(0.0f, 0.0f, 0.0f));
    REQUIRE(c.sitarJawari == 0.0f);
    REQUIRE(c.sitarPickHardness == 0.0f);
    REQUIRE(c.sitarTarafAmount == 0.0f);

    VoiceParameters::apply(c, laneState(1.0f, 1.0f, 1.0f));
    REQUIRE(c.sitarJawari == 1.0f);
    REQUIRE(c.sitarPickHardness == 1.0f);
    REQUIRE(c.sitarTarafAmount == 1.0f);

    // Lanes are independent: each drives only its own model parameter.
    VoiceParameters::apply(c, laneState(0.25f, 0.75f, 0.1f));
    REQUIRE(c.sitarJawari == 0.25f);        // clean string .. aggressive buzz
    REQUIRE(c.sitarPickHardness == 0.75f);  // soft/round .. hard/bright pluck
    REQUIRE(c.sitarTarafAmount == 0.1f);    // dry string .. strong sympathetic
}

TEST_CASE("Sitar lanes stage through the Voice control queue", "[sitar_lanes]")
{
    Voice voice(0, sitarLaneConfig());
    voice.init(kSampleRate);
    voice.updateParameters(laneState(0.2f, 0.3f, 0.4f));
    voice.process(); // one applied control update per sample
    REQUIRE(voice.getConfig().sitarJawari == 0.2f);
    REQUIRE(voice.getConfig().sitarPickHardness == 0.3f);
    REQUIRE(voice.getConfig().sitarTarafAmount == 0.4f);
}

TEST_CASE("Sitar velocity drives the pluck, not the output gain",
          "[sitar_lanes]")
{
    // With velocityToAmplitude == false a mid-ring velocity step must not
    // touch the output: both voices render bit-identical.
    Voice a(0, sitarLaneConfig());
    Voice b(0, sitarLaneConfig());
    a.init(kSampleRate);
    b.init(kSampleRate);
    const VoiceState first = laneState(0.37f, 0.01f, 0.1f, 0.8f);
    a.updateParameters(first);
    b.updateParameters(first);
    for (int i = 0; i < 480; ++i) // gate rise plucks both voices equally
    {
        const float x = a.process(), y = b.process();
        REQUIRE(x == y);
    }
    const VoiceState quieter = laneState(0.37f, 0.01f, 0.1f, 0.1f);
    b.updateParameters(quieter); // velocity changes on a ringing string
    for (int i = 0; i < 960; ++i)
    {
        const float x = a.process(), y = b.process();
        REQUIRE(x == y);
        REQUIRE(std::isfinite(x));
    }
}

TEST_CASE("OLED lane names show JAWARI/PICK/TARAF for the sitar engine",
          "[sitar_lanes]")
{
    const auto c = sitarLaneConfig();
    // The OLED parameter page prints VoiceEdit::name(lane cast to edit Id,
    // config) — src/OLED/oled.cpp displayParameterInfo. ParamId::Filter
    // shares the Id::Cutoff slot value.
    REQUIRE(std::string(VoiceEdit::name(VoiceEdit::Id::Cutoff, c)) == "JAWARI");
    REQUIRE(std::string(VoiceEdit::name(VoiceEdit::Id::Attack, c)) == "PICK");
    REQUIRE(std::string(VoiceEdit::name(VoiceEdit::Id::Decay, c)) == "TARAF");

    // Lane values format through the binding units (percents, not Hz/ms).
    char text[24]{};
    REQUIRE(VoiceParameters::formatValue(c, ParamId::Filter, 0.45f, text, sizeof(text)));
    REQUIRE(std::string(text) == "45%");
    REQUIRE(VoiceParameters::formatValue(c, ParamId::Attack, 0.9f, text, sizeof(text)));
    REQUIRE(std::string(text) == "90%");
    REQUIRE(VoiceParameters::formatValue(c, ParamId::Decay, 0.35f, text, sizeof(text)));
    REQUIRE(std::string(text) == "35%");

    // The editor's engine choice labels the new engine "Sitar".
    VoiceEdit::format(VoiceEdit::Id::Engine, c, text, sizeof(text));
    REQUIRE(std::string(text) == "Sitar");
}

TEST_CASE("Editor engine selection reaches the sitar lanes", "[sitar_lanes]")
{
    VoiceConfig c{}; // default oscillator engine
    REQUIRE(VoiceEdit::available(VoiceEdit::Id::Engine, c));
    VoiceEdit::setValue(VoiceEdit::Id::Engine, c, 5.0f);
    REQUIRE(c.engine == ENGINE_SITAR);
    REQUIRE(c.paramSet == PARAMSET_SITAR);
    REQUIRE(c.parameters == nullptr); // lanes come from the legacy sitar set
    REQUIRE(std::string(VoiceParameters::binding(c, ParamId::Filter).name) == "JAWARI");

    // Cycling up from the top of the old range reaches the new engine.
    VoiceEdit::setValue(VoiceEdit::Id::Engine, c, ENGINE_RECIPE);
    VoiceEdit::adjust(VoiceEdit::Id::Engine, c, 100.0f);
    REQUIRE(c.engine == ENGINE_SITAR);

    // Switching back keeps the established engine lane sets untouched.
    VoiceEdit::setValue(VoiceEdit::Id::Engine, c, 1.0f);
    REQUIRE(c.paramSet == PARAMSET_WAVEGUIDE);
    REQUIRE(std::string(VoiceParameters::binding(c, ParamId::Filter).name) == "Bright");
    VoiceEdit::setValue(VoiceEdit::Id::Engine, c, 0.0f);
    REQUIRE(c.paramSet == PARAMSET_STANDARD);
    REQUIRE(VoiceParameters::binding(c, ParamId::Filter).name == nullptr);
}

TEST_CASE("Non-sitar engines keep their lane bindings and velocity behavior",
          "[sitar_lanes]")
{
    const auto byParamSet = [](uint8_t set)
    {
        VoiceConfig c{};
        c.paramSet = set;
        return c;
    };
    const auto rawDefaults = VoiceConfig{};

    // STANDARD: all four mapped lanes keep null targets (velocity + cutoff +
    // ADSR times) and velocity keeps scaling the output.
    {
        auto c = byParamSet(PARAMSET_STANDARD);
        const auto &l = VoiceParameters::layout(c);
        REQUIRE(l.velocityToAmplitude);
        REQUIRE(l.envelopeFromTracks);
        for (ParamId id : {ParamId::Velocity, ParamId::Filter, ParamId::Attack,
                           ParamId::Decay})
            REQUIRE(VoiceParameters::binding(c, id).target == nullptr);
        const auto before = c;
        VoiceParameters::apply(c, laneState(1.0f, 1.0f, 1.0f));
        REQUIRE(c.sitarJawari == before.sitarJawari);
        REQUIRE(c.sitarPickHardness == before.sitarPickHardness);
        REQUIRE(c.sitarTarafAmount == before.sitarTarafAmount);
    }

    // WAVEGUIDE: the Bright/Pick/T60 remap is untouched, including its span.
    {
        auto c = byParamSet(PARAMSET_WAVEGUIDE);
        const auto &f = VoiceParameters::binding(c, ParamId::Filter);
        REQUIRE(std::string(f.name) == "Bright");
        REQUIRE(f.target == &VoiceConfig::wgBrightness);
        const auto &a = VoiceParameters::binding(c, ParamId::Attack);
        REQUIRE(std::string(a.name) == "Pick");
        REQUIRE(a.target == &VoiceConfig::wgPickHardness);
        const auto &d = VoiceParameters::binding(c, ParamId::Decay);
        REQUIRE(std::string(d.name) == "T60");
        REQUIRE(d.target == &VoiceConfig::wgT60);
        REQUIRE(d.minimum == 0.05f);
        REQUIRE(d.maximum == 10.0f);
        REQUIRE_FALSE(VoiceParameters::velocityToAmplitude(c));
        VoiceParameters::apply(c, laneState(1.0f, 1.0f, 1.0f));
        REQUIRE(c.wgBrightness == 1.0f);
        REQUIRE(c.wgPickHardness == 1.0f);
        REQUIRE(c.wgT60 == 10.0f);
        REQUIRE(c.sitarJawari == rawDefaults.sitarJawari);
        REQUIRE(c.sitarPickHardness == rawDefaults.sitarPickHardness);
        REQUIRE(c.sitarTarafAmount == rawDefaults.sitarTarafAmount);
    }

    // HYPERSAW / NOISESTORM / HARDSYNC keep their remaps exactly.
    {
        auto c = byParamSet(PARAMSET_HYPERSAW);
        REQUIRE(std::string(VoiceParameters::binding(c, ParamId::Attack).name) == "Detune");
        REQUIRE(std::string(VoiceParameters::binding(c, ParamId::Decay).name) == "Mix");
        REQUIRE(VoiceParameters::binding(c, ParamId::Filter).target == nullptr);
        REQUIRE(VoiceParameters::velocityToAmplitude(c));
    }
    {
        auto c = byParamSet(PARAMSET_NOISESTORM);
        REQUIRE(std::string(VoiceParameters::binding(c, ParamId::Filter).name) == "Color");
        REQUIRE(std::string(VoiceParameters::binding(c, ParamId::Attack).name) == "Regen");
        REQUIRE(std::string(VoiceParameters::binding(c, ParamId::Decay).name) == "Chaos");
    }
    {
        auto c = byParamSet(PARAMSET_HARDSYNC);
        REQUIRE(std::string(VoiceParameters::binding(c, ParamId::Note).name) == "Master");
        REQUIRE(std::string(VoiceParameters::binding(c, ParamId::Velocity).name) == "Slave");
        REQUIRE_FALSE(VoiceParameters::velocityToAmplitude(c));
    }
}

TEST_CASE("Sitar lane seeding fills tracks from the resting values",
          "[sitar_lanes]")
{
    auto c = sitarLaneConfig(); // model defaults: jawari 0.45, pick 0.9, taraf 0.35
    Sequencer seq;
    seq.initializeParameters();
    const float noteBefore = seq.getStepParameterValue(ParamId::Note, 0);
    VoiceParameters::seedTracks(seq, c);
    const uint8_t filterSteps = seq.getParameterStepCount(ParamId::Filter);
    REQUIRE(filterSteps > 0);
    for (uint8_t step = 0; step < filterSteps; ++step)
    {
        REQUIRE(seq.getStepParameterValue(ParamId::Filter, step) == c.sitarJawari);
        REQUIRE(seq.getStepParameterValue(ParamId::Attack, step) == c.sitarPickHardness);
        REQUIRE(seq.getStepParameterValue(ParamId::Decay, step) == c.sitarTarafAmount);
    }
    // Musical lanes are not seeded by engine bindings.
    REQUIRE(seq.getStepParameterValue(ParamId::Note, 0) == noteBefore);
}
