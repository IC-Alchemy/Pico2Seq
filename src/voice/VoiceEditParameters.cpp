#include "VoiceEditParameters.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "presets/RecipePresets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <type_traits>

namespace VoiceEdit {
namespace {
constexpr Parameter kParameters[] = {
    {Id::Note, "Note", Group::Sequenced, Unit::Number, 0.0f, 36.0f, false,
     nullptr, nullptr},
    {Id::Velocity, "Velocity", Group::Sequenced, Unit::Percent, 0.0f, 1.0f,
     false, nullptr, nullptr},
    {Id::Cutoff, "Cutoff", Group::Sequenced, Unit::Percent, 0.0f, 1.0f, false,
     nullptr, nullptr},
    {Id::Attack, "Attack", Group::Sequenced, Unit::Seconds, 0.001f, 10.0f, true,
     nullptr, nullptr},
    {Id::Decay, "Decay", Group::Sequenced, Unit::Seconds, 0.001f, 10.0f, true,
     nullptr, nullptr},
    {Id::Octave, "Octave", Group::Sequenced, Unit::Semitones, -24.0f, 24.0f,
     false, nullptr, nullptr},
    {Id::GateLength, "Gate length", Group::Sequenced, Unit::Percent, 0.001f,
     1.0f, false, nullptr, nullptr},
    {Id::Gate, "Gate enable", Group::Sequenced, Unit::Toggle, 0.0f, 1.0f, false,
     nullptr, nullptr},
    {Id::Slide, "Slide enable", Group::Sequenced, Unit::Toggle, 0.0f, 1.0f,
     false, nullptr, nullptr},
    {Id::Engine, "Engine", Group::Source, Unit::Choice, 0.0f, 4.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.engine); },
     +[](VoiceConfig &c, float v) {
       c.engine = static_cast<std::remove_reference_t<decltype(c.engine)>>(v);
     }},
    {Id::Recipe, "Recipe", Group::Source, Unit::Choice, 0.0f, 3.0f, false,
     nullptr, nullptr},
    {Id::OscCount, "Oscillators", Group::Source, Unit::Number, 0.0f, 3.0f,
     false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.oscillatorCount);
     },
     +[](VoiceConfig &c, float v) {
       c.oscillatorCount =
           static_cast<std::remove_reference_t<decltype(c.oscillatorCount)>>(v);
     }},
    {Id::Wave1, "Waveform", Group::Osc1, Unit::Choice, 0.0f, 7.0f, false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.oscWaveforms[0]);
     },
     +[](VoiceConfig &c, float v) {
       c.oscWaveforms[0] =
           static_cast<std::remove_reference_t<decltype(c.oscWaveforms[0])>>(v);
     }},
    {Id::Level1, "Level", Group::Osc1, Unit::Percent, 0.0f, 1.0f, false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.oscAmplitudes[0]);
     },
     +[](VoiceConfig &c, float v) {
       c.oscAmplitudes[0] =
           static_cast<std::remove_reference_t<decltype(c.oscAmplitudes[0])>>(
               v);
     }},
    {Id::Detune1, "Detune", Group::Osc1, Unit::Semitones, -12.0f, 12.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.oscDetuning[0]); },
     +[](VoiceConfig &c, float v) {
       c.oscDetuning[0] =
           static_cast<std::remove_reference_t<decltype(c.oscDetuning[0])>>(v);
     }},
    {Id::Pulse1, "Pulse width", Group::Osc1, Unit::Percent, 0.01f, 0.99f, false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.oscPulseWidth[0]);
     },
     +[](VoiceConfig &c, float v) {
       c.oscPulseWidth[0] =
           static_cast<std::remove_reference_t<decltype(c.oscPulseWidth[0])>>(
               v);
     }},
    {Id::Harmony1, "Harmony", Group::Osc1, Unit::Number, -12.0f, 12.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.harmony[0]); },
     +[](VoiceConfig &c, float v) {
       c.harmony[0] =
           static_cast<std::remove_reference_t<decltype(c.harmony[0])>>(v);
     }},
    {Id::Wave2, "Waveform", Group::Osc2, Unit::Choice, 0.0f, 7.0f, false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.oscWaveforms[1]);
     },
     +[](VoiceConfig &c, float v) {
       c.oscWaveforms[1] =
           static_cast<std::remove_reference_t<decltype(c.oscWaveforms[1])>>(v);
     }},
    {Id::Level2, "Level", Group::Osc2, Unit::Percent, 0.0f, 1.0f, false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.oscAmplitudes[1]);
     },
     +[](VoiceConfig &c, float v) {
       c.oscAmplitudes[1] =
           static_cast<std::remove_reference_t<decltype(c.oscAmplitudes[1])>>(
               v);
     }},
    {Id::Detune2, "Detune", Group::Osc2, Unit::Semitones, -12.0f, 12.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.oscDetuning[1]); },
     +[](VoiceConfig &c, float v) {
       c.oscDetuning[1] =
           static_cast<std::remove_reference_t<decltype(c.oscDetuning[1])>>(v);
     }},
    {Id::Pulse2, "Pulse width", Group::Osc2, Unit::Percent, 0.01f, 0.99f, false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.oscPulseWidth[1]);
     },
     +[](VoiceConfig &c, float v) {
       c.oscPulseWidth[1] =
           static_cast<std::remove_reference_t<decltype(c.oscPulseWidth[1])>>(
               v);
     }},
    {Id::Harmony2, "Harmony", Group::Osc2, Unit::Number, -12.0f, 12.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.harmony[1]); },
     +[](VoiceConfig &c, float v) {
       c.harmony[1] =
           static_cast<std::remove_reference_t<decltype(c.harmony[1])>>(v);
     }},
    {Id::Wave3, "Waveform", Group::Osc3, Unit::Choice, 0.0f, 7.0f, false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.oscWaveforms[2]);
     },
     +[](VoiceConfig &c, float v) {
       c.oscWaveforms[2] =
           static_cast<std::remove_reference_t<decltype(c.oscWaveforms[2])>>(v);
     }},
    {Id::Level3, "Level", Group::Osc3, Unit::Percent, 0.0f, 1.0f, false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.oscAmplitudes[2]);
     },
     +[](VoiceConfig &c, float v) {
       c.oscAmplitudes[2] =
           static_cast<std::remove_reference_t<decltype(c.oscAmplitudes[2])>>(
               v);
     }},
    {Id::Detune3, "Detune", Group::Osc3, Unit::Semitones, -12.0f, 12.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.oscDetuning[2]); },
     +[](VoiceConfig &c, float v) {
       c.oscDetuning[2] =
           static_cast<std::remove_reference_t<decltype(c.oscDetuning[2])>>(v);
     }},
    {Id::Pulse3, "Pulse width", Group::Osc3, Unit::Percent, 0.01f, 0.99f, false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.oscPulseWidth[2]);
     },
     +[](VoiceConfig &c, float v) {
       c.oscPulseWidth[2] =
           static_cast<std::remove_reference_t<decltype(c.oscPulseWidth[2])>>(
               v);
     }},
    {Id::Harmony3, "Harmony", Group::Osc3, Unit::Number, -12.0f, 12.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.harmony[2]); },
     +[](VoiceConfig &c, float v) {
       c.harmony[2] =
           static_cast<std::remove_reference_t<decltype(c.harmony[2])>>(v);
     }},
    {Id::EnvelopeOn, "Envelope", Group::Envelope, Unit::Toggle, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.hasEnvelope); },
     +[](VoiceConfig &c, float v) {
       c.hasEnvelope =
           static_cast<std::remove_reference_t<decltype(c.hasEnvelope)>>(v);
     }},
    {Id::EnvAttack, "Attack", Group::Envelope, Unit::Seconds, 0.001f, 10.0f,
     true,
     +[](const VoiceConfig &c) { return static_cast<float>(c.defaultAttack); },
     +[](VoiceConfig &c, float v) {
       c.defaultAttack =
           static_cast<std::remove_reference_t<decltype(c.defaultAttack)>>(v);
     }},
    {Id::EnvDecay, "Decay", Group::Envelope, Unit::Seconds, 0.001f, 10.0f, true,
     +[](const VoiceConfig &c) { return static_cast<float>(c.defaultDecay); },
     +[](VoiceConfig &c, float v) {
       c.defaultDecay =
           static_cast<std::remove_reference_t<decltype(c.defaultDecay)>>(v);
     }},
    {Id::Sustain, "Sustain", Group::Envelope, Unit::Percent, 0.0f, 1.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.defaultSustain); },
     +[](VoiceConfig &c, float v) {
       c.defaultSustain =
           static_cast<std::remove_reference_t<decltype(c.defaultSustain)>>(v);
     }},
    {Id::Release, "Release", Group::Envelope, Unit::Seconds, 0.001f, 10.0f,
     true,
     +[](const VoiceConfig &c) { return static_cast<float>(c.defaultRelease); },
     +[](VoiceConfig &c, float v) {
       c.defaultRelease =
           static_cast<std::remove_reference_t<decltype(c.defaultRelease)>>(v);
     }},
    {Id::FilterOn, "Main filter", Group::Filter, Unit::Toggle, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.hasFilter); },
     +[](VoiceConfig &c, float v) {
       c.hasFilter =
           static_cast<std::remove_reference_t<decltype(c.hasFilter)>>(v);
     }},
    {Id::FilterType, "Topology", Group::Filter, Unit::Choice, 0.0f, 1.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.filterType); },
     +[](VoiceConfig &c, float v) {
       c.filterType =
           static_cast<std::remove_reference_t<decltype(c.filterType)>>(v);
     }},
    {Id::FilterMode, "Response", Group::Filter, Unit::Choice, 0.0f, 5.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.filterMode); },
     +[](VoiceConfig &c, float v) {
       c.filterMode =
           static_cast<std::remove_reference_t<decltype(c.filterMode)>>(v);
     }},
    {Id::StaticCutoff, "Cutoff", Group::Filter, Unit::Percent, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.filterCutoffBase);
     },
     +[](VoiceConfig &c, float v) {
       c.filterCutoffBase =
           static_cast<std::remove_reference_t<decltype(c.filterCutoffBase)>>(
               v);
     }},
    {Id::Resonance, "Resonance", Group::Filter, Unit::Percent, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.filterRes); },
     +[](VoiceConfig &c, float v) {
       c.filterRes =
           static_cast<std::remove_reference_t<decltype(c.filterRes)>>(v);
     }},
    {Id::FilterDrive, "Input drive", Group::Filter, Unit::Number, 0.0f, 4.0f,
     false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.filterDrive); },
     +[](VoiceConfig &c, float v) {
       c.filterDrive =
           static_cast<std::remove_reference_t<decltype(c.filterDrive)>>(v);
     }},
    {Id::Passband, "Passband gain", Group::Filter, Unit::Percent, 0.0f, 0.5f,
     false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.filterPassbandGain);
     },
     +[](VoiceConfig &c, float v) {
       c.filterPassbandGain =
           static_cast<std::remove_reference_t<decltype(c.filterPassbandGain)>>(
               v);
     }},
    {Id::HighPassFreq, "HP cutoff", Group::HighPass, Unit::Hertz, 20.0f,
     20000.0f, true,
     +[](const VoiceConfig &c) { return static_cast<float>(c.highPassFreq); },
     +[](VoiceConfig &c, float v) {
       c.highPassFreq =
           static_cast<std::remove_reference_t<decltype(c.highPassFreq)>>(v);
     }},
    {Id::HighPassRes, "HP resonance", Group::HighPass, Unit::Percent, 0.0f,
     1.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.highPassRes); },
     +[](VoiceConfig &c, float v) {
       c.highPassRes =
           static_cast<std::remove_reference_t<decltype(c.highPassRes)>>(v);
     }},
    {Id::DriveOn, "Overdrive", Group::Drive, Unit::Toggle, 0.0f, 1.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.hasOverdrive); },
     +[](VoiceConfig &c, float v) {
       c.hasOverdrive =
           static_cast<std::remove_reference_t<decltype(c.hasOverdrive)>>(v);
     }},
    {Id::Drive, "Drive", Group::Drive, Unit::Percent, 0.0f, 1.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.overdriveDrive); },
     +[](VoiceConfig &c, float v) {
       c.overdriveDrive =
           static_cast<std::remove_reference_t<decltype(c.overdriveDrive)>>(v);
     }},
    {Id::DriveGain, "Drive gain", Group::Drive, Unit::Number, 0.0f, 2.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.overdriveGain); },
     +[](VoiceConfig &c, float v) {
       c.overdriveGain =
           static_cast<std::remove_reference_t<decltype(c.overdriveGain)>>(v);
     }},
    {Id::T60, "String T60", Group::Engine, Unit::Seconds, 0.05f, 10.0f, true,
     +[](const VoiceConfig &c) { return static_cast<float>(c.wgT60); },
     +[](VoiceConfig &c, float v) {
       c.wgT60 = static_cast<std::remove_reference_t<decltype(c.wgT60)>>(v);
     }},
    {Id::Brightness, "Brightness", Group::Engine, Unit::Percent, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.wgBrightness); },
     +[](VoiceConfig &c, float v) {
       c.wgBrightness =
           static_cast<std::remove_reference_t<decltype(c.wgBrightness)>>(v);
     }},
    {Id::PickPosition, "Pick position", Group::Engine, Unit::Percent, 0.02f,
     0.5f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.wgPickPosition); },
     +[](VoiceConfig &c, float v) {
       c.wgPickPosition =
           static_cast<std::remove_reference_t<decltype(c.wgPickPosition)>>(v);
     }},
    {Id::PickHardness, "Pick hardness", Group::Engine, Unit::Percent, 0.0f,
     1.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.wgPickHardness); },
     +[](VoiceConfig &c, float v) {
       c.wgPickHardness =
           static_cast<std::remove_reference_t<decltype(c.wgPickHardness)>>(v);
     }},
    {Id::Stiffness, "Stiffness", Group::Engine, Unit::Percent, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.wgStiffness); },
     +[](VoiceConfig &c, float v) {
       c.wgStiffness =
           static_cast<std::remove_reference_t<decltype(c.wgStiffness)>>(v);
     }},
    {Id::StringDetune, "String detune", Group::Engine, Unit::Cents, 0.0f, 30.0f,
     false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.wgDetune); },
     +[](VoiceConfig &c, float v) {
       c.wgDetune =
           static_cast<std::remove_reference_t<decltype(c.wgDetune)>>(v);
     }},
    {Id::SawDetune, "Saw detune", Group::Engine, Unit::Percent, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.hypersawDetune); },
     +[](VoiceConfig &c, float v) {
       c.hypersawDetune =
           static_cast<std::remove_reference_t<decltype(c.hypersawDetune)>>(v);
     }},
    {Id::SawMix, "Saw mix", Group::Engine, Unit::Percent, 0.0f, 1.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.hypersawMix); },
     +[](VoiceConfig &c, float v) {
       c.hypersawMix =
           static_cast<std::remove_reference_t<decltype(c.hypersawMix)>>(v);
     }},
    {Id::DiffuseSize, "Diffuse size", Group::Engine, Unit::Percent, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.noiseDiffuseSize);
     },
     +[](VoiceConfig &c, float v) {
       c.noiseDiffuseSize =
           static_cast<std::remove_reference_t<decltype(c.noiseDiffuseSize)>>(
               v);
     }},
    {Id::DiffuseMix, "Diffuse mix", Group::Engine, Unit::Percent, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.noiseDiffuseMix);
     },
     +[](VoiceConfig &c, float v) {
       c.noiseDiffuseMix =
           static_cast<std::remove_reference_t<decltype(c.noiseDiffuseMix)>>(v);
     }},
    {Id::SwarmColor, "Swarm color", Group::Engine, Unit::Percent, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.noiseSwarmColor);
     },
     +[](VoiceConfig &c, float v) {
       c.noiseSwarmColor =
           static_cast<std::remove_reference_t<decltype(c.noiseSwarmColor)>>(v);
     }},
    {Id::SwarmRegen, "Swarm regen", Group::Engine, Unit::Number, 0.0f, 1.2f,
     false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.noiseSwarmRegen);
     },
     +[](VoiceConfig &c, float v) {
       c.noiseSwarmRegen =
           static_cast<std::remove_reference_t<decltype(c.noiseSwarmRegen)>>(v);
     }},
    {Id::ChaosLevel, "Chaos level", Group::Engine, Unit::Percent, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.noiseChaosLevel);
     },
     +[](VoiceConfig &c, float v) {
       c.noiseChaosLevel =
           static_cast<std::remove_reference_t<decltype(c.noiseChaosLevel)>>(v);
     }},
    {Id::Macro1, "Macro 1", Group::Engine, Unit::Percent, 0.0f, 1.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.macro1); },
     +[](VoiceConfig &c, float v) {
       c.macro1 = static_cast<std::remove_reference_t<decltype(c.macro1)>>(v);
     }},
    {Id::Macro2, "Macro 2", Group::Engine, Unit::Number, 0.0f, 8.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.macro2); },
     +[](VoiceConfig &c, float v) {
       c.macro2 = static_cast<std::remove_reference_t<decltype(c.macro2)>>(v);
     }},
    {Id::Macro3, "Macro 3", Group::Engine, Unit::Percent, 0.0f, 1.0f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.macro3); },
     +[](VoiceConfig &c, float v) {
       c.macro3 = static_cast<std::remove_reference_t<decltype(c.macro3)>>(v);
     }},
    {Id::SlideTime, "Glide time", Group::Sequenced, Unit::Seconds, 0.001f,
     10.0f, true,
     +[](const VoiceConfig &c) { return static_cast<float>(c.slideSeconds); },
     +[](VoiceConfig &c, float v) {
       c.slideSeconds =
           static_cast<std::remove_reference_t<decltype(c.slideSeconds)>>(v);
     }},
    {Id::Enabled, "Voice enabled", Group::Output, Unit::Toggle, 0.0f, 1.0f,
     false, +[](const VoiceConfig &c) { return static_cast<float>(c.enabled); },
     +[](VoiceConfig &c, float v) {
       c.enabled = static_cast<std::remove_reference_t<decltype(c.enabled)>>(v);
     }},
    {Id::Output, "Output level", Group::Output, Unit::Percent, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.outputLevel); },
     +[](VoiceConfig &c, float v) {
       c.outputLevel =
           static_cast<std::remove_reference_t<decltype(c.outputLevel)>>(v);
     }},
    {Id::FmModFeedback, "Mod feedback", Group::Engine, Unit::Percent, 0.0f,
     0.95f, false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.fmModFeedback); },
     +[](VoiceConfig &c, float v) {
       c.fmModFeedback = static_cast<decltype(c.fmModFeedback)>(v);
     }},
    {Id::PhaseFold, "Triangle fold", Group::Engine, Unit::Percent, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.phaseTriangleFold);
     },
     +[](VoiceConfig &c, float v) {
       c.phaseTriangleFold = static_cast<decltype(c.phaseTriangleFold)>(v);
     }},
    {Id::SubRatio, "Sub ratio", Group::Engine, Unit::Number, 0.125f, 2.0f,
     false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.spectralSubRatio);
     },
     +[](VoiceConfig &c, float v) {
       c.spectralSubRatio = static_cast<decltype(c.spectralSubRatio)>(v);
     }},
    {Id::SubShape, "Sub shape", Group::Engine, Unit::Percent, 0.0f, 1.0f, false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.spectralSubShape);
     },
     +[](VoiceConfig &c, float v) {
       c.spectralSubShape = static_cast<decltype(c.spectralSubShape)>(v);
     }},
    {Id::DriftChaos, "Drift chaos", Group::Engine, Unit::Percent, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.prismDriftChaos);
     },
     +[](VoiceConfig &c, float v) {
       c.prismDriftChaos = static_cast<decltype(c.prismDriftChaos)>(v);
     }},
    {Id::RecipeRetrigger, "Reset on gate", Group::Engine, Unit::Toggle, 0.0f,
     1.0f, false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.recipeRetrigger);
     },
     +[](VoiceConfig &c, float v) {
       c.recipeRetrigger = static_cast<decltype(c.recipeRetrigger)>(v);
     }},
    {Id::NoiseLevel, "Noise level", Group::Engine, Unit::Percent, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.noiseSourceLevel);
     },
     +[](VoiceConfig &c, float v) {
       c.noiseSourceLevel = static_cast<decltype(c.noiseSourceLevel)>(v);
     }},
    {Id::ChaosRate, "Chaos rate", Group::Engine, Unit::Number, 0.125f, 4.0f,
     false,
     +[](const VoiceConfig &c) { return static_cast<float>(c.noiseChaosRate); },
     +[](VoiceConfig &c, float v) {
       c.noiseChaosRate = static_cast<decltype(c.noiseChaosRate)>(v);
     }},
    {Id::FilterEnvAmount, "Env amount", Group::Filter, Unit::Number, 0.0f, 2.0f,
     false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.filterEnvelopeAmount);
     },
     +[](VoiceConfig &c, float v) {
       c.filterEnvelopeAmount =
           static_cast<decltype(c.filterEnvelopeAmount)>(v);
     }},
    {Id::FilterEnvFloor, "Env floor", Group::Filter, Unit::Number, 0.0f, 1.0f,
     false,
     +[](const VoiceConfig &c) {
       return static_cast<float>(c.filterEnvelopeFloor);
     },
     +[](VoiceConfig &c, float v) {
       c.filterEnvelopeFloor = static_cast<decltype(c.filterEnvelopeFloor)>(v);
     }},
};
static_assert(std::size(kParameters) == static_cast<size_t>(Id::Count));
static_assert(static_cast<uint8_t>(Id::Slide) ==
              static_cast<uint8_t>(ParamId::Slide));
constexpr const char *kGroups[] = {
    "Sequenced bases", "Source",   "Oscillator 1", "Oscillator 2",
    "Oscillator 3",    "Envelope", "Main filter",  "High-pass",
    "Overdrive",       "Engine",   "Output"};
constexpr const VoiceRecipe *kRecipes[] = {
    &VoiceRecipes::kFeedbackFm, &VoiceRecipes::kPhaseMorph,
    &VoiceRecipes::kSpectralDsf, &VoiceRecipes::kPrism};
constexpr const VoiceParameterLayout *kLayouts[] = {
    &VoicePresets::kFmParameters, &VoicePresets::kPhaseParameters,
    &VoicePresets::kDsfParameters, &VoicePresets::kPrismParameters};
constexpr const char *kRecipeNames[] = {"Feedback FM", "Phase morph",
                                        "Spectral DSF", "Prism"};
constexpr float kTimeMin = 0.001f, kTimeMax = 10.0f;
float timeNormalize(float seconds) {
  return std::log(std::clamp(seconds, kTimeMin, kTimeMax) / kTimeMin) /
         std::log(kTimeMax / kTimeMin);
}
float timeMap(float n) {
  return kTimeMin * std::pow(kTimeMax / kTimeMin, std::clamp(n, 0.0f, 1.0f));
}
int recipeIndex(const VoiceConfig &c) {
  for (int i = 0; i < 4; ++i)
    if (c.recipe == kRecipes[i])
      return i;
  return 0;
}
void selectRecipe(VoiceConfig &c, int i) {
  i = std::clamp(i, 0, 3);
  c.recipe = kRecipes[i];
  c.parameters = kLayouts[i];
  // Each recipe owns macro units; normalize old values through the new ranges.
  for (ParamId lane : {ParamId::Filter, ParamId::Attack, ParamId::Decay}) {
    const auto &b = VoiceParameters::binding(c, lane);
    c.*(b.target) = std::clamp(c.*(b.target), b.minimum, b.maximum);
  }
}
float laneBase(ParamId id, const VoiceConfig &c) {
  const auto &b = VoiceParameters::binding(c, id);
  if (b.target)
    return b.normalize(c.*(b.target));
  switch (id) {
  case ParamId::Note:
    return c.baseNote / 36.0f;
  case ParamId::Velocity:
    return c.baseVelocity;
  case ParamId::Filter:
    return c.filterCutoffBase;
  case ParamId::Attack:
    return timeNormalize(c.defaultAttack);
  case ParamId::Decay:
    return timeNormalize(c.defaultDecay);
  case ParamId::Octave:
    return (c.baseOctave + 24.0f) / 48.0f;
  case ParamId::GateLength:
    return (c.baseGateLength - 0.001f) / 0.999f;
  default:
    return 0.5f;
  }
}
void setLaneBase(ParamId id, VoiceConfig &c, float v) {
  const auto &b = VoiceParameters::binding(c, id);
  if (b.target) {
    c.*(b.target) = std::clamp(v, b.minimum, b.maximum);
    return;
  }
  switch (id) {
  case ParamId::Note:
    c.baseNote = std::round(std::clamp(v, 0.0f, 36.0f));
    break;
  case ParamId::Velocity:
    c.baseVelocity = b.unit == VoiceParameterUnit::Semitones
                         ? b.normalize(v)
                         : std::clamp(v, 0.0f, 1.0f);
    break;
  case ParamId::Filter:
    c.filterCutoffBase = std::clamp(v, 0.0f, 1.0f);
    break;
  case ParamId::Attack:
    c.defaultAttack = std::clamp(v, kTimeMin, kTimeMax);
    break;
  case ParamId::Decay:
    c.defaultDecay = std::clamp(v, kTimeMin, kTimeMax);
    break;
  case ParamId::Octave:
    c.baseOctave = std::round(std::clamp(v, -24.0f, 24.0f) / 12.0f) * 12.0f;
    break;
  case ParamId::GateLength:
    c.baseGateLength = std::clamp(v, 0.001f, 1.0f);
    break;
  case ParamId::Gate:
    c.baseGate = v >= 0.5f;
    break;
  case ParamId::Slide:
    c.baseSlide = v >= 0.5f;
    break;
  default:
    break;
  }
}
const VoiceParameterBinding *bindingFor(Id id, const VoiceConfig &c) {
  ParamId lane = sequenceLane(id, c);
  if (lane == ParamId::Count)
    return nullptr;
  const auto &b = VoiceParameters::binding(c, lane);
  return (b.target || b.unit != VoiceParameterUnit::Standard) ? &b : nullptr;
}
} // namespace
const Parameter &parameter(Id id) noexcept {
  return kParameters[static_cast<size_t>(id) < std::size(kParameters)
                         ? static_cast<size_t>(id)
                         : 0];
}
const char *groupName(Group group) noexcept {
  return kGroups[static_cast<size_t>(group) < std::size(kGroups)
                     ? static_cast<size_t>(group)
                     : 0];
}
ParamId sequenceLane(Id id, const VoiceConfig &c) noexcept {
  if (id <= Id::Slide)
    return static_cast<ParamId>(id);
  float VoiceConfig::*target = nullptr;
  switch (id) {
  case Id::T60:
    target = &VoiceConfig::wgT60;
    break;
  case Id::Brightness:
    target = &VoiceConfig::wgBrightness;
    break;
  case Id::PickHardness:
    target = &VoiceConfig::wgPickHardness;
    break;
  case Id::SawDetune:
    target = &VoiceConfig::hypersawDetune;
    break;
  case Id::SawMix:
    target = &VoiceConfig::hypersawMix;
    break;
  case Id::SwarmColor:
    target = &VoiceConfig::noiseSwarmColor;
    break;
  case Id::SwarmRegen:
    target = &VoiceConfig::noiseSwarmRegen;
    break;
  case Id::ChaosLevel:
    target = &VoiceConfig::noiseChaosLevel;
    break;
  case Id::Macro1:
    target = &VoiceConfig::macro1;
    break;
  case Id::Macro2:
    target = &VoiceConfig::macro2;
    break;
  case Id::Macro3:
    target = &VoiceConfig::macro3;
    break;
  case Id::EnvAttack:
    return VoiceParameters::layout(c).envelopeFromTracks ? ParamId::Attack
                                                         : ParamId::Count;
  case Id::EnvDecay:
    return VoiceParameters::layout(c).envelopeFromTracks ? ParamId::Decay
                                                         : ParamId::Count;
  case Id::StaticCutoff:
    return VoiceParameters::binding(c, ParamId::Filter).target
               ? ParamId::Count
               : ParamId::Filter;
  default:
    break;
  }
  if (target)
    for (ParamId lane :
         {ParamId::Velocity, ParamId::Filter, ParamId::Attack, ParamId::Decay})
      if (VoiceParameters::binding(c, lane).target == target)
        return lane;
  return ParamId::Count;
}
const char *name(Id id, const VoiceConfig &c) noexcept {
  if (id <= Id::Slide) {
    const auto &b = VoiceParameters::binding(c, static_cast<ParamId>(id));
    if (b.name)
      return b.name;
  }
  if (id >= Id::Macro1 && id <= Id::Macro3) {
    const auto *b = bindingFor(id, c);
    if (b && b->name)
      return b->name;
  }
  return parameter(id).name;
}
bool available(Id id, const VoiceConfig &c) noexcept {
  if (id >= Id::Count)
    return false;
  if (id == Id::FmModFeedback)
    return c.engine == ENGINE_RECIPE && c.recipe == &VoiceRecipes::kFeedbackFm;
  if (id == Id::PhaseFold)
    return c.engine == ENGINE_RECIPE && c.recipe == &VoiceRecipes::kPhaseMorph;
  if (id == Id::SubRatio || id == Id::SubShape)
    return c.engine == ENGINE_RECIPE && c.recipe == &VoiceRecipes::kSpectralDsf;
  if (id == Id::DriftChaos)
    return c.engine == ENGINE_RECIPE && c.recipe == &VoiceRecipes::kPrism;
  if (id == Id::RecipeRetrigger)
    return c.engine == ENGINE_RECIPE;
  if (id == Id::NoiseLevel || id == Id::ChaosRate)
    return c.engine == ENGINE_NOISEFX;
  const auto g = parameter(id).group;
  if (g >= Group::Osc1 && g <= Group::Osc3) {
    const int osc = static_cast<int>(g) - static_cast<int>(Group::Osc1);
    // Harmony 1 also tunes the alternate pitched engines.
    if (id == Id::Harmony1 && c.engine != ENGINE_OSC)
      return true;
    if (c.engine != ENGINE_OSC || osc >= c.oscillatorCount)
      return false;
    if (id == Id::Pulse1 || id == Id::Pulse2 || id == Id::Pulse3)
      return c.oscWaveforms[osc] == WAVE_SQUARE ||
             c.oscWaveforms[osc] == WAVE_BSP_SQUARE;
  }
  if (id == Id::OscCount)
    return c.engine == ENGINE_OSC;
  if (id == Id::Recipe || (id >= Id::Macro1 && id <= Id::Macro3))
    return c.engine == ENGINE_RECIPE;
  if (id >= Id::T60 && id <= Id::StringDetune)
    return c.engine == ENGINE_WAVEGUIDE;
  if (id == Id::SawDetune || id == Id::SawMix)
    return c.engine == ENGINE_HYPERSAW;
  if (id >= Id::DiffuseSize && id <= Id::ChaosLevel)
    return c.engine == ENGINE_NOISEFX;
  if (id == Id::FilterDrive || id == Id::Passband)
    return c.hasFilter && c.filterType == FILTER_LADDER;
  if (g == Group::Filter && id != Id::FilterOn)
    return c.hasFilter;
  if (g == Group::Drive && id != Id::DriveOn)
    return c.hasOverdrive;
  if (g == Group::Envelope && id != Id::EnvelopeOn)
    return c.hasEnvelope;
  if (id == Id::Attack || id == Id::Decay)
    return VoiceParameters::binding(c, static_cast<ParamId>(id)).target ||
           (c.hasEnvelope && VoiceParameters::layout(c).envelopeFromTracks);
  if (id == Id::Cutoff)
    return VoiceParameters::binding(c, ParamId::Filter).target || c.hasFilter;
  return true;
}
float value(Id id, const VoiceConfig &c) noexcept {
  if (id <= Id::Slide) {
    const auto lane = static_cast<ParamId>(id);
    const auto &b = VoiceParameters::binding(c, lane);
    if (b.target)
      return c.*(b.target);
    switch (id) {
    case Id::Note:
      return c.baseNote;
    case Id::Velocity:
      return b.unit == VoiceParameterUnit::Semitones ? b.map(c.baseVelocity)
                                                     : c.baseVelocity;
    case Id::Cutoff:
      return c.filterCutoffBase;
    case Id::Attack:
      return c.defaultAttack;
    case Id::Decay:
      return c.defaultDecay;
    case Id::Octave:
      return c.baseOctave;
    case Id::GateLength:
      return c.baseGateLength;
    case Id::Gate:
      return c.baseGate;
    case Id::Slide:
      return c.baseSlide;
    default:
      break;
    }
  }
  if (id == Id::Recipe)
    return static_cast<float>(recipeIndex(c));
  float v = parameter(id).get(c);
  if ((id == Id::Wave1 || id == Id::Wave2 || id == Id::Wave3) &&
      v == WAVE_NOISE)
    return 7;
  if (id == Id::FilterMode && c.filterType == FILTER_SVF)
    return static_cast<float>(static_cast<int>(v) / 2);
  return v;
}
void setValue(Id id, VoiceConfig &c, float v) noexcept {
  if (id >= Id::Count || !std::isfinite(v))
    return;
  if (id <= Id::Slide) {
    setLaneBase(static_cast<ParamId>(id), c, v);
    return;
  }
  const auto &p = parameter(id);
  const auto *b = bindingFor(id, c);
  v = std::clamp(v, b ? b->minimum : p.minimum, b ? b->maximum : p.maximum);
  if (p.unit == Unit::Toggle || p.unit == Unit::Choice || id == Id::OscCount ||
      id == Id::Harmony1 || id == Id::Harmony2 || id == Id::Harmony3)
    v = std::round(v);
  if (id == Id::Engine) {
    c.engine = static_cast<uint8_t>(v);
    c.parameters = nullptr;
    c.recipe = nullptr;
    c.paramSet = c.engine == ENGINE_WAVEGUIDE  ? PARAMSET_WAVEGUIDE
                 : c.engine == ENGINE_HYPERSAW ? PARAMSET_HYPERSAW
                 : c.engine == ENGINE_NOISEFX  ? PARAMSET_NOISESTORM
                                               : PARAMSET_STANDARD;
    if (c.engine == ENGINE_RECIPE)
      selectRecipe(c, 0);
    if (c.engine == ENGINE_OSC && c.oscillatorCount == 0)
      c.oscillatorCount = 1;
    if (c.engine == ENGINE_OSC) {
      for (uint8_t i = 0; i < c.oscillatorCount; ++i)
        if (c.oscWaveforms[i] == WAVE_HARDSYNC_SAW)
          c.paramSet = PARAMSET_HARDSYNC;
    }
    return;
  }
  if (id == Id::Recipe) {
    selectRecipe(c, static_cast<int>(v));
    return;
  }
  if (id == Id::Wave1 || id == Id::Wave2 || id == Id::Wave3) {
    if (v == 7)
      v = WAVE_NOISE;
  }
  if (id == Id::FilterMode && c.filterType == FILTER_SVF)
    v = std::clamp(v, 0.0f, 2.0f) * 2;
  p.set(c, v);
  if (c.engine == ENGINE_OSC && (id == Id::Wave1 || id == Id::Wave2 ||
                                 id == Id::Wave3 || id == Id::OscCount)) {
    bool sync = false;
    for (uint8_t i = 0; i < c.oscillatorCount; ++i)
      sync = sync || c.oscWaveforms[i] == WAVE_HARDSYNC_SAW;
    c.paramSet = sync ? PARAMSET_HARDSYNC : PARAMSET_STANDARD;
  }
}
void adjust(Id id, VoiceConfig &c, float delta) noexcept {
  if (!available(id, c) || !std::isfinite(delta) || delta == 0)
    return;
  const auto &p = parameter(id);
  const auto *b = bindingFor(id, c);
  const float lo = b ? b->minimum : p.minimum, hi = b ? b->maximum : p.maximum;
  float v = value(id, c);
  if (p.unit == Unit::Toggle || p.unit == Unit::Choice || id == Id::OscCount ||
      id == Id::Harmony1 || id == Id::Harmony2 || id == Id::Harmony3 ||
      id == Id::Note || id == Id::Octave) {
    setValue(id, c,
             v + (delta > 0 ? 1 : -1) * (id == Id::Octave ? 12.0f : 1.0f));
    return;
  }
  if (b)
    v = b->map(std::clamp(b->normalize(v) + delta, 0.0f, 1.0f));
  else if (p.logarithmic)
    v = lo * std::pow(hi / lo, std::clamp(std::log(std::clamp(v, lo, hi) / lo) /
                                                  std::log(hi / lo) +
                                              delta,
                                          0.0f, 1.0f));
  else
    v += delta * (hi - lo);
  setValue(id, c, v);
}
void format(Id id, const VoiceConfig &c, char *out, size_t capacity) noexcept {
  if (!out || !capacity)
    return;
  const float v = value(id, c);
  const auto &p = parameter(id);
  const auto *b = bindingFor(id, c);
  if (id == Id::Engine) {
    const char *n[] = {"Oscillators", "Waveguide", "Noise FX", "Hypersaw",
                       "Recipe"};
    std::snprintf(out, capacity, "%s",
                  n[std::clamp(static_cast<int>(v), 0, 4)]);
    return;
  }
  if (id == Id::Recipe) {
    std::snprintf(out, capacity, "%s", kRecipeNames[recipeIndex(c)]);
    return;
  }
  if (id == Id::Wave1 || id == Id::Wave2 || id == Id::Wave3) {
    const char *n[] = {"Sine",   "Triangle",  "Saw",       "Square",
                       "BS Saw", "BS Square", "Hard sync", "Noise"};
    std::snprintf(out, capacity, "%s",
                  n[std::clamp(static_cast<int>(v), 0, 7)]);
    return;
  }
  if (id == Id::FilterType) {
    std::snprintf(out, capacity, "%s",
                  c.filterType == FILTER_LADDER ? "Ladder" : "SVF");
    return;
  }
  if (id == Id::FilterMode) {
    const char *n[] = {"Low-pass", "Band-pass", "High-pass"};
    std::snprintf(out, capacity, "%s",
                  c.filterType == FILTER_SVF
                      ? n[std::clamp(static_cast<int>(v), 0, 2)]
                      : voiceui::kFilterModeNames[std::clamp(
                            static_cast<int>(c.filterMode), 0, 5)]);
    return;
  }
  if ((id == Id::Cutoff && !b) || id == Id::StaticCutoff) {
    const auto &l = VoiceParameters::layout(c);
    std::snprintf(out, capacity, "%.0f Hz",
                  dspmap::fmap(v, l.cutoffMinimum, l.cutoffMaximum,
                               dspmap::Mapping::EXP));
    return;
  }
  if (b && b->unit == VoiceParameterUnit::Ratio) {
    std::snprintf(out, capacity, "%.2fx", v);
    return;
  }
  Unit unit = p.unit;
  if (b) {
    if (b->unit == VoiceParameterUnit::Seconds)
      unit = Unit::Seconds;
    else if (b->unit == VoiceParameterUnit::Percent)
      unit = Unit::Percent;
    else if (b->unit == VoiceParameterUnit::Semitones)
      unit = Unit::Semitones;
    else
      unit = Unit::Number;
  }
  switch (unit) {
  case Unit::Toggle:
    std::snprintf(out, capacity, "%s", v >= 0.5f ? "On" : "Off");
    break;
  case Unit::Percent:
    std::snprintf(out, capacity, "%.1f %%", v * 100);
    break;
  case Unit::Seconds:
    std::snprintf(out, capacity, "%.3f s", v);
    break;
  case Unit::Hertz:
    std::snprintf(out, capacity, "%.0f Hz", v);
    break;
  case Unit::Cents:
    std::snprintf(out, capacity, "%.1f ct", v);
    break;
  case Unit::Semitones:
    std::snprintf(out, capacity, "%+.1f st", v);
    break;
  default:
    std::snprintf(out, capacity, "%.2f", v);
    break;
  }
}
Id nextParameter(Id current, int direction, const VoiceConfig &c,
                 bool changeGroup) noexcept {
  const int step = direction < 0 ? -1 : 1, count = static_cast<int>(Id::Count);
  const auto group = parameter(current).group;
  if (changeGroup) {
    int g = static_cast<int>(group);
    for (int i = 0; i < static_cast<int>(Group::Count); ++i) {
      g = (g + step + static_cast<int>(Group::Count)) %
          static_cast<int>(Group::Count);
      for (int j = 0; j < count; ++j)
        if (parameter(static_cast<Id>(j)).group == static_cast<Group>(g) &&
            available(static_cast<Id>(j), c))
          return static_cast<Id>(j);
    }
  } else {
    int j = static_cast<int>(current);
    for (int i = 0; i < count; ++i) {
      j = (j + step + count) % count;
      auto id = static_cast<Id>(j);
      if (parameter(id).group == group && available(id, c))
        return id;
    }
  }
  return current;
}
float composeLane(ParamId id, float stored, const void *context) noexcept {
  if (!context)
    return stored;
  const auto &c = *static_cast<const VoiceConfig *>(context);
  if (!c.usePatchBases)
    return stored;
  if (id == ParamId::Gate)
    return c.baseGate && stored > 0.5f ? 1.0f : 0.0f;
  if (id == ParamId::Slide)
    return c.baseSlide || stored > 0.5f ? 1.0f : 0.0f;
  if (id >= ParamId::Count)
    return stored;
  if (id == ParamId::Note)
    return std::round(std::clamp(stored + c.baseNote, 0.0f, 36.0f));
  const float n = id == ParamId::GateLength ? (stored - 0.001f) / 0.999f : stored;
  const float effective = std::clamp(
      laneBase(id, c) + std::clamp(n, 0.0f, 1.0f) - 0.5f, 0.0f, 1.0f);
  if (id == ParamId::GateLength)
    return 0.001f + effective * 0.999f;
  return effective;
}
int8_t mapOctave(float n) noexcept {
  return static_cast<int8_t>(
      std::round((std::clamp(n, 0.0f, 1.0f) - 0.5f) * 4) * 12);
}
void seedModifiers(Sequencer &seq) {
  for (uint8_t i = 0; i < PARAM_ID_COUNT; ++i) {
    const auto id = static_cast<ParamId>(i);
    if (id == ParamId::Gate || id == ParamId::Slide)
      continue;
    seq.fillModulationTrack(id, id == ParamId::Note ? 0.0f : mapNormalizedValueToParamRange(id, 0.5f));
  }
}
void enablePatch(VoiceConfig &c) noexcept { c.usePatchBases = true; }
} // namespace VoiceEdit
