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
class MidiNoteManager; // Forward declare MidiNoteManager

// =======================
//   CONSTANTS
// =======================
// Moved to UIConstants.h to centralize button mappings, timing, and layout.

// =======================
//   FUNCTION DECLARATIONS
// =======================

/**
 * @brief Main matrix event handler (Arduino-friendly consolidated signature).
 *        Accepts an array of Sequencer* plus count to support any number of voices.
 *
 * @param evt             Matrix button event (button index and press/release type).
 * @param uiState         Central UI state object (mutable).
 * @param sequencers      Array of non-owning Sequencer* pointers. Must have at least two
 *                        entries for full functionality; additional entries are allowed.
 * @param sequencerCount  Number of entries in the sequencers array.
 * @param midiNoteManager MIDI note lifecycle manager for note on/off and CC handling.
 */
void matrixEventHandler(const MatrixButtonEvent &evt,
                        UIState &uiState,
                        Sequencer *const *sequencers,
                        size_t sequencerCount,
                        MidiNoteManager &midiNoteManager);

/**
 * Poll UI-held buttons (long-press detection) using the supplied sequencer array.
 *
 * The canonical implementation accepts a sequencer pointer array and its length.
 * Convenience overloads forward to this signature.
 */
void pollUIHeldButtons(UIState &uiState, Sequencer *const *sequencers, size_t sequencerCount);

// Backwards-compatible convenience overloads for existing call-sites
void pollUIHeldButtons(UIState &uiState, Sequencer &seq1, Sequencer &seq2);
void pollUIHeldButtons(UIState &uiState, Sequencer &seq1, Sequencer &seq2,
                       Sequencer &seq3, Sequencer &seq4);

// Compatibility overloads for matrixEventHandler (will forward to consolidated handler)
void matrixEventHandler(const MatrixButtonEvent &evt, UIState &uiState, Sequencer &seq1, Sequencer &seq2, MidiNoteManager &midiNoteManager);
void matrixEventHandler(const MatrixButtonEvent &evt, UIState &uiState, Sequencer &seq1, Sequencer &seq2, Sequencer &seq3, Sequencer &seq4, MidiNoteManager &midiNoteManager);

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
void selectVoice(UIState &uiState, MidiNoteManager &midiNoteManager, uint8_t voiceIndex);

/**
 * @brief Shift + step pad action: clear one step (gate off, params reset to
 * defaults) on the given voice's sequencer.
 */
void clearSequencerStep(Sequencer &sequencer, uint8_t stepIdx);

/**
 * @brief Firmware-side bridge that unpacks UIState button/edit-step fields and
 *        forwards them to Sequencer::advanceStep's primitive-argument overload.
 *
 * Sequencer (src/pico2seq-core) no longer depends on UIState so it stays
 * reusable outside this firmware; this adapter keeps call sites in
 * Pico2Seq.ino unchanged.
 */
void advanceSequencerStep(Sequencer &seq, uint32_t current_uclock_step, int mm_distance,
                          const UIState &uiState, VoiceState *voiceState);

#endif // UI_EVENT_HANDLER_H
