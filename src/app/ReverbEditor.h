#pragma once
#include "../ui/ControlSurfaceLogic.h"

// ReverbEditor: Core 0 glue between the live Reverb page (Shift + 6 + 2) and the
// master-bus reverb. The page's gestures live in ui/ReverbPageControls.h and its
// fader curves in ControlSurface::reverbValueForFader; this applies the result to
// the VoiceManager's lock-free reverb targets. Nothing here touches audio-owned
// state, so it is safe to call every control pass while Core 1 renders.
namespace ReverbEditor {
// One fader position (0..1) -> its setting on the master reverb, remembered as the
// control the OLED highlights. False when nothing was applied: no manager, an
// unassigned fader (ReverbControl::Count) or a non-finite position.
bool setFromFader(ControlSurface::ReverbControl control, float normalized);
// Toggle the Freeze switch (page button 1); returns the new state. The audio side
// eases in and out of a freeze (see MasterReverb), so this never clicks.
bool toggleFreeze();
} // namespace ReverbEditor
