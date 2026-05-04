#ifndef TEMPLATE_INPUT_H
#define TEMPLATE_INPUT_H

#include <Arduino.h>
#include "../sequencer/SequencerDefs.h"

enum class TemplateInputAction : uint8_t {
    None,
    ToggleRun,
    ToggleGate,
    SetParameter,
    SetTrackLength,
    Randomize,
    SetTempo,
    SetScale
};

struct TemplateInputEvent {
    TemplateInputAction action = TemplateInputAction::None;
    uint8_t sequencerIndex = 0;
    uint8_t stepIndex = 0;
    ParamId parameter = ParamId::Gate;
    float value = 0.0f;
};

void readHardwareInput(TemplateInputEvent &event);

#endif // TEMPLATE_INPUT_H
