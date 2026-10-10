#pragma once

#include "../ui/UIState.h"

namespace DisplayPolicy {
inline bool pending(uint32_t until, uint32_t now) noexcept {
    return until != 0 && static_cast<int32_t>(until - now) > 0;
}

// Existing editing modes and confirmations always remain visible. This policy
// does not mutate the chosen overview: it resumes when the contextual view ends.
inline LaneDisplay::Page effectivePage(const UIState &state, uint32_t now) noexcept {
    if (state.voiceEditor.active || state.voiceEnvelope.active ||
        state.reverbPage.active || state.tuningPage.active ||
        state.settingsMode || state.arp.active() || state.copyLane.active ||
        state.gateSeqLengthMode || state.modGateParamSeqLengthsMode ||
        state.selectedStepForEdit >= 0 || state.hasVoiceParameterFeedback(now) ||
        pending(state.alchemyModeBannerUntil, now) ||
        pending(state.encoderBaseViewUntil, now) || pending(state.envViewUntil, now) ||
        (state.oledNoticeKind != UIState::OledNoticeKind::None &&
         pending(state.oledNoticeUntil, now)) ||
        state.copyLane.pasteNoticeShowing(now))
        return LaneDisplay::Page::Focus;
    return state.display.page;
}
}
