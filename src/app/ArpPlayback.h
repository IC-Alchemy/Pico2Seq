#pragma once

// Arpeggiator mode's audio glue (Core 0), the counterpart of StepPlayback.
//
// The note decisions live in the portable engine (src/pico2seq-core/arpeggiator/
// Arpeggiator.h, held in UIState::arp); this file maps the engine's note slots
// onto the four voices and publishes VoiceState the way the sequencer path does.
// Nothing here runs on Core 1: the audio core only ever sees the VoiceState
// updates the control core publishes.

#include <cstdint>

struct UIState;

/**
 * @brief Enter or leave Arpeggiator mode (Shift + hold the Voice 4 tile button).
 *
 * Both edges silence all four voices first: the note lifecycle that would have
 * ended a sounding note (sequencer duration ticks, arp gate ticks) switches over
 * with the mode, so without this an old note would hang. Entering also ends the
 * sequencers' note tracking, and leaving restarts them if the clock is still
 * running, so a flip during playback cannot come back to silent sequencers.
 * The modal cleanup and the OLED notice go through
 * UITransitions::enterArpMode/exitArpMode.
 */
void arpModeToggle(UIState &uiState);

/**
 * @brief Advance the arpeggiator by one uClock PPQN tick and play what it reports.
 *
 * Called from processPendingGateTicks() instead of tickSequencerVoices() while
 * the mode is active, so arp note timing is the transport's own 480 PPQN grid.
 */
void arpTick();

/**
 * @brief Transport start: re-sync the arp to the chord's first note.
 *
 * A hand-held chord survives the transport stop (the engine's chord is entered
 * by the pads, not by the clock), so pressing play with a chord held starts from
 * the root on the downbeat.
 */
void arpTransportStart();
