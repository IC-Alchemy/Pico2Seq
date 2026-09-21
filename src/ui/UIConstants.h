#ifndef UI_CONSTANTS_H
#define UI_CONSTANTS_H

#include <Arduino.h>

// UIConstants.h — legacy matrix button IDs + shared UI timing windows.
// The 32 pads are all step pads now; parameter/utility buttons live on the
// Alchemy tiles. IDs below survive for the handlers both surfaces share.
// Add new timing windows here, not per-file.

// =======================
// Button mappings
// =======================
// Primary control buttons: what the performer gets per press.
constexpr uint8_t BUTTON_SLIDE_MODE = 22;           // Pads toggle legato per step
// 23 was BUTTON_TOGGLE_DELAY; removed with the delay effect (2026-09-11)
constexpr uint8_t BUTTON_VOICE_SWITCH = 24;         // Step through voices 1..4
constexpr uint8_t BUTTON_ENCODER_CONTROL = 25;      // Tap: next encoder lane; hold: Gate-length entry
constexpr uint8_t BUTTON_PLAY_STOP = 26;            // Run/stop; stopping opens the preset browser
constexpr uint8_t BUTTON_CHANGE_SCALE = 27;         // Next scale (performer hears the key change)
constexpr uint8_t BUTTON_CHANGE_THEME = 28;         // Next LED palette (stage feedback only)
constexpr uint8_t BUTTON_CHANGE_SWING_PATTERN = 29; // Next shuffle groove

// Randomize buttons per voice: tap shuffles the pattern, hold wipes it.
constexpr uint8_t BUTTON_RANDOMIZE_SEQ1 = 30;
constexpr uint8_t BUTTON_RANDOMIZE_SEQ2 = 31;


// Step pads (MPR121): all 32 indices address steps via PadBank (low/high bank
// = voice pair around the selected voice). No pad index means "parameter" now.
constexpr uint8_t NUMBER_OF_STEP_PADS = 32;

// Per-voice step count: each voice owns 16 steps inside the 32-pad bank pair.
constexpr uint8_t NUMBER_OF_STEP_BUTTONS = 16;

// =======================
// Timing values
// =======================
// Timing values: tap/hold split lives in ButtonManager (single source).
// Windows below are OLED/mode feel only.
constexpr unsigned long ENCODER_DOUBLE_PRESS_WINDOW_MS = 300;      // ms: window for double press on encoder control
constexpr unsigned long OLED_NOTICE_DURATION_MS = 800;             // ms: transient OLED confirmation notice
constexpr unsigned long ENCODER_BASE_VIEW_MS = 1500;               // ms: OLED shows the edited base after an encoder turn
constexpr unsigned long VOICE_PARAMETER_DISPLAY_TIMEOUT_MS = 2000; // ms: how long to show voice parameter changes
constexpr unsigned long SETTINGS_MODE_TIMEOUT_MS = 30000;          // ms: auto-exit settings mode after inactivity

// =======================
// UI Layout / Behavior
// =======================
// Reserved slot for future layout constants if needed

#endif // UI_CONSTANTS_H