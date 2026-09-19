// OledView.cpp — see OledView.h for the contract. Pure C++ (no Arduino, no
// display hardware) so the host test suite links it directly.

#include "OledView.h"
#include "ControlSurfaceLogic.h"
#include "../voice/MusicalValues.h"

namespace OledView
{

Route route(const UIState &ui, const Sequencer &seq, const VoiceConfig *config,
            unsigned long nowMs, ParamId encoderLane)
{
    Route r;

    if (ui.voiceEditor.active)
    {
        r.page = Page::VoiceEditor;
        return r;
    }
    if (ui.alchemyModeBannerUntil != 0 && nowMs < ui.alchemyModeBannerUntil)
    {
        r.page = Page::ModeBanner;
        return r;
    }
    if (ui.oledNoticeUntil != 0 && nowMs < ui.oledNoticeUntil &&
        ui.oledNoticeKind != UIState::OledNoticeKind::None)
    {
        r.page = Page::Notice;
        return r;
    }

    const bool selected = ui.selectedStepForEdit >= 0;
    const bool baseWindow = config && ui.encoderBaseViewUntil != 0 &&
                            nowMs < ui.encoderBaseViewUntil;

    // Base feedback outranks everything below: a turn must show the base it
    // just changed, explicitly labeled BASE — even while a parameter button
    // is held, and even when the turn hit a limit without changing the value.
    if (baseWindow && encoderLane != ParamId::Count)
    {
        r.page = Page::Parameter;
        r.param = encoderLane;
        r.base = true;
        r.showBase = true;
        r.stepSelected = selected;
        r.step = selected ? static_cast<uint8_t>(ui.selectedStepForEdit)
                          : seq.getCurrentStepForParameter(encoderLane);
        r.values = MusicalValues::baseStep(*config);
        return r;
    }

    // A focused parameter (newest physical hold, then the Shift latch)
    // outranks the settings and length screens. Values come from the
    // authoritative composed snapshot: the selected step when one is in
    // edit, otherwise each lane's own playing cursor — never a sensor
    // prediction (a gate-rejected Note keeps showing the retained value).
    const ParamId focused = focusedParameterId(ui);
    if (focused != ParamId::Count)
    {
        r.page = Page::Parameter;
        r.param = focused;
        r.stepSelected = selected;
        r.step = selected ? static_cast<uint8_t>(ui.selectedStepForEdit)
                          : seq.getCurrentStepForParameter(focused);
        r.values = seq.getPlaybackStep(selected ? r.step : UINT8_MAX);
        r.showDistance = true;
        return r;
    }

    if (ui.settingsMode)
    {
        r.page = Page::SettingsMenu;
        return r;
    }
    if (ui.gateSeqLengthMode)
    {
        r.page = Page::GateLength;
        return r;
    }

    // No focus: mirror the encoder's selected-step targeting (toggled edit
    // parameter, then the encoder-target lane) so the screen always shows
    // the value a turn changes — no more "Hold parameter" placeholder while
    // the encoder is already editing the encoder-target lane.
    const ParamId editing = ControlSurface::stepEditParameter(
        ParamId::Count, ui.currentEditParameter, ui.currentEncoderParameter);
    if (selected && editing != ParamId::Count)
    {
        r.page = Page::Parameter;
        r.param = editing;
        r.stepSelected = true;
        r.step = static_cast<uint8_t>(ui.selectedStepForEdit);
        r.values = seq.getPlaybackStep(r.step);
        return r;
    }
    if (selected)
    {
        r.page = Page::StepPlaceholder;
        return r;
    }

    // Resting screen: the encoder target's value (its lane's composed
    // playing value, or the base while the turn window is open).
    r.page = Page::EncoderDefault;
    r.showBase = baseWindow;
    r.param = encoderLane;
    r.values = baseWindow ? MusicalValues::baseStep(*config) : seq.getPlaybackStep();
    return r;
}

} // namespace OledView
