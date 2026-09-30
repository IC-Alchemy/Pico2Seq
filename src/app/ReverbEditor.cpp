#include "ReverbEditor.h"
#include "AppState.h"
#include "../voice/ReverbSettings.h"

using ControlSurface::ReverbControl;

bool ReverbEditor::setFromFader(ReverbControl control, float normalized)
{
    if (!voiceManager || control == ReverbControl::Count || !ReverbParams::finite(normalized))
        return false;
    const float value = ControlSurface::reverbValueForFader(control, normalized);
    switch (control)
    {
    case ReverbControl::Mix: voiceManager->setReverbMix(value); break;
    case ReverbControl::Decay: voiceManager->setReverbDecaySeconds(value); break;
    case ReverbControl::Damping: voiceManager->setReverbDampingHz(value); break;
    case ReverbControl::LowCut: voiceManager->setReverbLowCutHz(value); break;
    case ReverbControl::Diffusion: voiceManager->setReverbDiffusion(value); break;
    case ReverbControl::ModDepth: voiceManager->setReverbModDepth(value); break;
    case ReverbControl::Width: voiceManager->setReverbWidth(value); break;
    case ReverbControl::Count: return false;
    }
    uiState.reverbPage.lastControl = static_cast<uint8_t>(control);
    return true;
}

bool ReverbEditor::toggleFreeze()
{
    if (!voiceManager)
        return false;
    const bool next = !voiceManager->getReverbSettings().freeze;
    voiceManager->setReverbFreeze(next);
    return next;
}
