#ifndef BUTTON_HANDLERS_H
#define BUTTON_HANDLERS_H

#include <Arduino.h>

// Forward declarations to avoid circular deps
class UIState;
class Sequencer;
class MidiNoteManager;

// Specialized button handler functions extracted from UIEventHandler
// These functions encapsulate matrix button handling for clarity and reuse

// Handle Randomize button for a given voice index (0..3)
// Maintains short-press randomize and long-press reset behavior
void handleRandomizeButton(int voiceIndex, UIState& state);

// Handle parameter button for a specific voice and parameter index
void handleVoiceParameterButton(int voiceIndex, int paramIndex, UIState& state);

// Handle generic control buttons by button id
void handleControlButton(int buttonId, UIState& state);

// Button state utility helpers
void beginRandomizePress(int voiceIndex, UIState& state);
void endRandomizePress(int voiceIndex, UIState& state);

#endif // BUTTON_HANDLERS_H