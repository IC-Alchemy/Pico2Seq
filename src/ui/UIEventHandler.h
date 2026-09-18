#ifndef UI_EVENT_HANDLER_H
#define UI_EVENT_HANDLER_H

#include <Arduino.h>
#include <stddef.h>
#include "../matrix/Matrix.h"
#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include "../LEDMatrix/LEDMatrixFeedback.h"
#include "ButtonManager.h"
#include "UIState.h"
#include "ButtonHandlers.h"
#include "UIConstants.h"

// Forward declarations to prevent circular dependencies
class Sequencer;
class SequencerView;

// =======================
//   CONSTANTS
// =======================
// Moved to UIConstants.h to centralize button mappings, timing, and layout.

// =======================
//   FUNCTION DECLARATIONS
// =======================

/**
 * @brief Main matrix event handler (Arduino-friendly consolidated signature).
 *        Accepts the fixed voice routing table view shared by all UI consumers.
 *
 * @param evt             Matrix button event (button index and press/release type).
 * @param uiState         Central UI state object (mutable).
 * @param sequencers      Fixed voice-order view of the four voice sequencers.
 */
void matrixEventHandler(const MatrixButtonEvent &evt,
                        UIState &uiState,
                        const SequencerView &sequencers);

/**
 * Poll UI-held buttons (long-press detection) using the fixed voice routing table.
 */
void pollUIHeldButtons(UIState &uiState, const SequencerView &sequencers);

// =======================
//   ALCHEMY TILE BRIDGE ENTRY POINTS
// =======================
// The Alchemy tiles carry the parameter/utility buttons that used to live at
// matrix indices 16-31. AlchemyControlBridge translates tile edges into calls
// to these shared entry points so both surfaces run identical logic (keyed by
// ParamId / explicit voice, never by matrix index).

/**
 * @brief Parameter button edge keyed by ParamId (Alchemy tile path).
 *
 * Updates parameterButtonHeld[] (already Shift-latch resolved by the bridge)
 * semantics: auto-selects the encoder parameter on press and toggles step
 * edit parameter when a step is in edit. Blocked while slide mode is active,
 * exactly like the old matrix path.
 */
void handleParameterButtonById(uint8_t paramId, bool pressed, UIState &uiState);

/** @brief Slide tile button press: toggles slide mode, clearing conflicting modes. */
void handleSlideModePress(UIState &uiState);

/**
 * @brief Encoder-control tile button hold tracking (gate seq length mode).
 * begin on press; pollUIHeldButtons promotes a long hold into
 * gateSeqLengthMode; call end on release.
 */
void beginEncoderControlHold(UIState &uiState);
void endEncoderControlHold(UIState &uiState);

/**
 * @brief Direct voice selection (SliderModule Voice1..4 buttons, both modes).
 * Mirrors the old cycling voice-switch behavior minus the cycling.
 */
void selectVoice(UIState &uiState, uint8_t voiceIndex);

/**
 * @brief Open or close Settings (the preset browser).
 * Every control that opens or closes Settings goes through these, so the
 * sub-mode and its legacy mirror flags always agree. Opening starts in preset
 * selection for the selected voice.
 */
void openSettingsMode(UIState &uiState);
void closeSettingsMode(UIState &uiState);

/**
 * @brief Shift + step pad action: clear one step (gate off, params reset to
 * defaults) on the given voice's sequencer.
 */
void clearSequencerStep(Sequencer &sequencer, uint8_t stepIdx);

/**
 * @brief Clear one voice's whole pattern back to fresh state (Shift +
 *        Randomize tap chord): every stored step value, gate and slide flag
 *        wiped, track lengths back to their defaults, sounding note ended.
 *        Voice presets, transport and tempo are untouched.
 */
void clearSequencerVoice(UIState &uiState, Sequencer &sequencer, uint8_t voiceIndex);

/**
 * @brief Clear every sequencer the way clearSequencerVoice clears one
 *        (Shift + Randomize long-press chord): all voices, no values, no
 *        gates — the whole project starts fresh.
 */
void clearAllSequencerVoices(UIState &uiState, const SequencerView &sequencers);

/**
 * @brief Firmware-side bridge that unpacks UIState button/edit-step fields and
 *        forwards them to Sequencer::advanceStep's primitive-argument overload.
 *
 * Sequencer (src/pico2seq-core) no longer depends on UIState so it stays
 * reusable outside this firmware; this adapter keeps the StepPlayback.cpp
 * call site simple.
 */
void advanceSequencerStep(Sequencer &seq, uint32_t current_uclock_step, int mm_distance,
                          const UIState &uiState, VoiceState *voiceState);

#endif // UI_EVENT_HANDLER_H
