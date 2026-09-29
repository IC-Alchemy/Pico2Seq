#ifndef BUTTON_HANDLERS_H
#define BUTTON_HANDLERS_H

#include <Arduino.h>

// ButtonHandlers.h — performer gestures behind the matrix/tile buttons.
//
// Short-press vs long-press splits one physical control into audition vs
// commit (randomize vs wipe, tap vs step-edit). All state lives in UIState;
// these helpers only interpret edges. See docs/ButtonHandlers.md.

// Forward declarations to avoid circular deps
class UIState;
class Sequencer;

// Specialized button handler functions extracted from UIEventHandler.
// Each maps a physical gesture to what the performer hears: a fresh variation,
// a timbre switch, or transport. Long-press paths are polled, never blocking.

// Tap: shuffle this voice's pattern (OLED confirms). Hold: wipe it to empty.
// Maintains short-press randomize and long-press reset behavior
void handleRandomizeButton(int voiceIndex, UIState &state);

// Toggle a voice timbre switch (envelope/drive/filter) on the live config copy.
// Handle parameter button for a specific voice and parameter index
void handleVoiceParameterButton(int voiceIndex, int paramIndex, UIState &state);

// Transport/mode buttons (play, scale, theme, swing, slide, encoder target).
// Handle generic control buttons by button id
void handleControlButton(int buttonId, UIState &state);

// Press-time bookkeeping for the tap/hold split; polled by pollUIHeldButtons.
// Button state utility helpers
void beginRandomizePress(int voiceIndex, UIState &state);
void endRandomizePress(int voiceIndex, UIState &state);

#endif // BUTTON_HANDLERS_H