#include "ParameterEditing.h"
#include "../sensors/SensorConstants.h"
#include "../voice/MusicalValues.h"
#include <cstdlib>

namespace ParameterEditing {
static_assert(static_cast<uint8_t>(VoiceEdit::Id::Note) == static_cast<uint8_t>(ParamId::Note) &&
              static_cast<uint8_t>(VoiceEdit::Id::Velocity) == static_cast<uint8_t>(ParamId::Velocity) &&
              static_cast<uint8_t>(VoiceEdit::Id::Cutoff) == static_cast<uint8_t>(ParamId::Filter) &&
              static_cast<uint8_t>(VoiceEdit::Id::Attack) == static_cast<uint8_t>(ParamId::Attack) &&
              static_cast<uint8_t>(VoiceEdit::Id::Decay) == static_cast<uint8_t>(ParamId::Decay) &&
              static_cast<uint8_t>(VoiceEdit::Id::Octave) == static_cast<uint8_t>(ParamId::Octave),
              "Record lanes must match their patch base IDs");
namespace {
bool suppressed(const UIState &s) { return s.voiceEditor.active || s.controlsWaitRelease || s.slideMode; }
bool selected(const UIState &s) {
    return s.selectedStepForEdit >= 0 && s.selectedStepForEdit < SequencerConstants::MAX_STEPS_COUNT;
}
bool pending(uint32_t until, uint32_t now) {
    return until != 0 && static_cast<int32_t>(until - now) > 0;
}
}

ParamId focus(const UIState &state) { return state.parameterFocus.focused(); }
ParamId stepTarget(const UIState &state) {
    return ControlSurface::stepEditParameter(focus(state), state.currentEditParameter,
                                            state.currentEncoderParameter);
}
VoiceEdit::Id encoderTarget(const UIState &state) {
    if (state.voiceEditor.active && state.selectedVoiceIndex < UIState::MAX_VOICES)
        return state.voiceEditor.cursor[state.selectedVoiceIndex];
    // Physical focus outranks the last encoder selection, including on release
    // back to another held button or the Shift latch.
    const ParamId held = focus(state);
    if (held != ParamId::Count) return static_cast<VoiceEdit::Id>(held);
    if (state.currentEncoderParameter == EncoderParameterMode::SlideTime)
        return VoiceEdit::Id::SlideTime;
    const ParamId lane = parameterForEncoderMode(state.currentEncoderParameter);
    return lane == ParamId::Count ? VoiceEdit::Id::Count : static_cast<VoiceEdit::Id>(lane);
}

void parameterButton(UIState &state, uint8_t bit, bool pressed) {
    const auto *definition = parameterDefinition(static_cast<ParamId>(bit));
    if (!definition || !definition->recordable || suppressed(state)) return;
    state.parameterFocus.onParamButton(bit, pressed, state.shiftHeld);
    state.parameterFocus.applyTo(state.parameterButtonHeld, PARAM_ID_COUNT);
    state.latchedParameter = state.parameterFocus.latched();
    if (pressed) {
        state.currentEncoderParameter = definition->encoderMode;
        if (selected(state))
            state.currentEditParameter = state.currentEditParameter == static_cast<ParamId>(bit)
                ? ParamId::Count : static_cast<ParamId>(bit);
    }
    state.encoderBaseViewUntil = 0;
    syncGesture(state);
}

void syncGesture(UIState &s) {
    const uint16_t modes = static_cast<uint16_t>(s.alchemyMode) |
        (s.slideMode << 1) | (s.settingsMode << 2) | (s.voiceEditor.active << 3) |
        (s.controlsWaitRelease << 4) | (s.voiceEditor.waitRelease << 5) |
        (s.gateSeqLengthMode << 6) | (s.modGateParamSeqLengthsMode << 7) |
        (s.voiceEditor.fine << 8);
    if (s.editGesture.sync({s.selectedVoiceIndex, s.selectedStepForEdit, focus(s),
                           stepTarget(s), static_cast<uint8_t>(encoderTarget(s)), modes}))
        s.encoderInputChanged = true;
}

Sequencer::WriteResult record(UIState &s, Sequencer &sequence, int step,
                              float normalized, bool handPresent) {
    syncGesture(s);
    s.editGesture.observeHand(handPresent);
    const ParamId lane = focus(s);
    if (!handPresent || suppressed(s) || s.selectedVoiceIndex >= UIState::MAX_VOICES ||
        lane == ParamId::Count || !s.editGesture.permitsLidar(lane)) return {};
    return sequence.writeParameter(lane, step, normalized, Sequencer::ValueDomain::Normalized,
                                   Sequencer::EditIntent::Recording, step);
}

Sequencer::WriteResult fader(UIState &s, Sequencer &sequence, ParamId lane, float normalized) {
    syncGesture(s);
    if (suppressed(s) || !selected(s) || s.selectedVoiceIndex >= UIState::MAX_VOICES) return {};
    const auto held = focus(s);
    if (held != lane && (held != ParamId::Count ||
        (s.currentEditParameter != lane && s.currentEditParameter != ParamId::Count))) return {};
    const auto result = sequence.writeParameter(lane, s.selectedStepForEdit, normalized,
        Sequencer::ValueDomain::Normalized, Sequencer::EditIntent::Explicit);
    if (result.accepted()) s.editGesture.takeManual(lane);
    return result;
}

EncoderResult encoder(UIState &s, Sequencer &sequence, VoiceConfig &config, float delta) {
    syncGesture(s);
    if (s.controlsWaitRelease || s.selectedVoiceIndex >= UIState::MAX_VOICES ||
        (s.voiceEditor.active && s.voiceEditor.waitRelease)) return {};
    auto &motion = s.editGesture.motion;
    if (!s.voiceEditor.active && s.selectedStepForEdit >= 0) {
        if (!selected(s)) return {};
        const ParamId lane = stepTarget(s);
        const auto *definition = parameterDefinition(lane);
        if (!definition) return {};
        motion.add(delta);
        float increment;
        if (definition->editKind == ParameterEditKind::Stepped)
            increment = motion.takeSteps(SensorConstants::MagneticEncoder::STEPPED_VALUE_DETENT) *
                (lane == ParamId::Octave ? 0.25f : 1.0f);
        else
            increment = motion.takeContinuous(SensorConstants::MagneticEncoder::MINIMUM_INCREMENT_THRESHOLD) *
                (parameterValueAsFloat(definition->maxValue) - parameterValueAsFloat(definition->minValue));
        if (increment == 0.0f) return {};
        const auto result = sequence.writeParameter(lane, s.selectedStepForEdit,
            sequence.getStepParameterValue(lane, static_cast<uint8_t>(s.selectedStepForEdit)) + increment,
            Sequencer::ValueDomain::Stored, Sequencer::EditIntent::Explicit);
        // Intentional movement at a limit still owns this target.
        if (result.accepted()) s.editGesture.takeManual(lane);
        return {result, false};
    }
    const auto id = encoderTarget(s);
    if (!VoiceEdit::available(id, config)) { motion.reset(); return {}; }
    motion.add(s.voiceEditor.active && s.voiceEditor.fine ? delta * 0.1f : delta);
    const float before = VoiceEdit::value(id, config);
    if (VoiceEdit::stepped(id)) {
        const int steps = motion.takeSteps(SensorConstants::MagneticEncoder::STEPPED_VALUE_DETENT);
        for (int i = 0; i < std::abs(steps); ++i)
            VoiceEdit::adjust(id, config, steps > 0 ? 1.0f : -1.0f);
    } else {
        VoiceEdit::adjust(id, config, motion.takeContinuous(
            SensorConstants::MagneticEncoder::MINIMUM_INCREMENT_THRESHOLD));
    }
    return {{}, VoiceEdit::value(id, config) != before};
}

void advance(Sequencer &sequence, uint8_t voice, uint32_t clockStep,
             const UIState &state, float normalized, bool handPresent, VoiceState &requested) {
    const bool recording = handPresent && voice < UIState::MAX_VOICES &&
        voice == state.selectedVoiceIndex && !suppressed(state);
    sequence.setRecordingInput(normalized);
    sequence.advanceStep(clockStep, recording ? 0 : -1,
        state.parameterButtonHeld[0], state.parameterButtonHeld[1], state.parameterButtonHeld[2],
        state.parameterButtonHeld[3], state.parameterButtonHeld[4], state.parameterButtonHeld[5],
        state.selectedStepForEdit, &requested);
}

View view(const UIState &s, const Sequencer &sequence, const VoiceConfig *config, uint32_t now) {
    View out;
    if (s.voiceEditor.active) { out.page = Page::Editor; return out; }
    if (pending(s.alchemyModeBannerUntil, now)) { out.page = Page::Banner; return out; }
    if (pending(s.oledNoticeUntil, now) && s.oledNoticeKind != UIState::OledNoticeKind::None) {
        out.page = Page::Notice; return out;
    }
    out.baseId = encoderTarget(s);
    const auto held = focus(s);
    const bool base = !selected(s) && config && pending(s.encoderBaseViewUntil, now);
    if (held != ParamId::Count || base) out.page = Page::Parameter;
    else if (s.settingsMode) { out.page = Page::Settings; return out; }
    else if (s.gateSeqLengthMode) { out.page = Page::GateLength; return out; }
    else if (selected(s)) out.page = Page::Parameter;
    out.lane = selected(s) ? stepTarget(s) : held != ParamId::Count ? held :
        config ? VoiceEdit::sequenceLane(out.baseId, *config) : parameterForEncoderMode(s.currentEncoderParameter);
    // Slide time has no sequencer lane and uses the overview's patch formatter.
    if (out.page == Page::Parameter && out.lane == ParamId::Count) out.page = Page::Overview;
    out.step = selected(s) ? static_cast<uint8_t>(s.selectedStepForEdit) :
        sequence.getCurrentStepForParameter(out.lane);
    out.source = selected(s) ? ValueSource::SelectedStep : base ? ValueSource::Base : ValueSource::Cursors;
    out.values = base ? MusicalValues::baseStep(*config) :
        sequence.getPlaybackStep(selected(s) ? out.step : UINT8_MAX);
    out.showDistance = held != ParamId::Count;
    return out;
}
} // namespace ParameterEditing
