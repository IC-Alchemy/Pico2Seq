#pragma once

#include "UIState.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../voice/VoiceEditParameters.h"

// Portable production decisions. Hardware callers only acquire input, publish
// changed requests, and draw the returned snapshot. No DSP or sensor access.
namespace ParameterEditing {
ParamId focus(const UIState &state);
ParamId stepTarget(const UIState &state);
VoiceEdit::Id encoderTarget(const UIState &state);
void parameterButton(UIState &state, uint8_t bit, bool pressed);
void syncGesture(UIState &state);
Sequencer::WriteResult record(UIState &state, Sequencer &sequence, int step,
                              float normalized, bool handPresent);
Sequencer::WriteResult fader(UIState &state, Sequencer &sequence, ParamId lane,
                             float normalized);
struct EncoderResult {
    Sequencer::WriteResult write;
    bool patchChanged = false;
};
EncoderResult encoder(UIState &state, Sequencer &sequence, VoiceConfig &config,
                       float delta);

// Called by the clock bridge for each voice. Keeps the selected-voice and
// modal recording gate in the same production path exercised on the host.
void advance(Sequencer &sequence, uint8_t voice, uint32_t clockStep,
             const UIState &state, float normalized, bool handPresent,
             VoiceState &requested);

enum class Page : uint8_t { Editor, Banner, Notice, Parameter, Settings, GateLength, Overview };
enum class ValueSource : uint8_t { Cursors, SelectedStep, Base };
struct View {
    Page page = Page::Overview;
    ValueSource source = ValueSource::Cursors;
    ParamId lane = ParamId::Count;
    VoiceEdit::Id baseId = VoiceEdit::Id::Count;
    uint8_t step = 0;
    Step values;
    bool showDistance = false;
};
View view(const UIState &state, const Sequencer &sequence,
          const VoiceConfig *config, uint32_t now);
} // namespace ParameterEditing
