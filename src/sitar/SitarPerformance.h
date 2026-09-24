#pragma once

#include "SitarControls.h"
#include "SitarInstrument.h"
#include "../matrix/Matrix.h"
#include "../ui/UIState.h"

#include <cstdint>

class LEDMatrix;

// SitarPerformance.h — the Core 0 half of Sitar Explorer (firmware glue).
// Musical role: every physical surface of the panel is pointed at the sitar for
// as long as the mode owns the box — the two lower touch rows play the raga's
// frets (a slide across them is a meend), the row above is the right hand
// (chikari strokes, jhala, tanpura, shimmers, palm), the row above that jumps
// between the sitar.h lane groups, the faders are the four stage macros, the
// knob walks and edits all thirteen lanes, the voice buttons pick the raga, the
// hand over the distance sensor is the pluck force, and the LEDs/OLED/serial
// console report all of it.
// Technical role: translate hardware edges into `Sitar::Controls` decisions and
// `Sitar::Instrument` publications. No DSP here — this file may call I2C/OLED
// helpers, the instrument never does the reverse.
namespace Sitar::Performance
{
// Boot (Core 0, after the voices exist): prepare the courses and hang the
// instrument on the master bus so the master fader, delay and compressor apply.
void begin();

// Touch pads (from matrixEventHandler).
void onPadEvent(const MatrixButtonEvent &event, UIState &uiState, uint32_t nowMs);

// Tile buttons and voice buttons (raw levels from AlchemyControlBridge).
void pollTiles(UIState &uiState, uint8_t buttons, uint8_t voices, uint32_t nowMs);

// One fader channel moved (normalized 0..1, already deadbanded by the caller).
void onFader(UIState &uiState, uint8_t channel, float normalized, uint32_t nowMs);

// Hand height from the distance sensor, refreshed every control pass.
void observeHand(UIState &uiState, bool handPresent, float hand01);

// Encoder travel for the focused lane.
void nudgeFocused(UIState &uiState, float travel);

// One transport sixteenth: jhala and tanpura strokes.
void onClockStep(UIState &uiState, uint8_t sixteenthInBar);

// The 8x4 matrix: raga map, blooms, stroke flashes, group cursor.
void renderLeds(LEDMatrix &ledMatrix, const UIState &uiState, uint32_t nowMs);

// Console: the mode's legend plus every lane's current value. Printed on entry
// and by the explore row's dump pad, so the console is a real part of the mode.
void printLegend(const UIState &uiState);

// Bench/console access: 'S' on the serial console toggles the mode, so the
// example can be explored without the panel.
void toggleFromConsole(UIState &uiState, uint32_t nowMs);

// Entry/exit side effects the UI policy cannot own (instrument lanes, notices).
void onEntered(UIState &uiState, uint32_t nowMs);
void onExited(UIState &uiState, uint32_t nowMs);

} // namespace Sitar::Performance
