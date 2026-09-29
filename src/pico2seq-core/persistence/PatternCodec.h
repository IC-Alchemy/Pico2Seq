#ifndef PICO2SEQ_PATTERN_CODEC_H
#define PICO2SEQ_PATTERN_CODEC_H

// PatternCodec: one voice's lanes <-> snapshot structs (notes to slides).
// Capture/apply every lane verbatim so a saved song replays identically.
// Portable C++ — no Arduino/hardware includes here.
#include "ProjectSnapshot.h"

class Sequencer; // Forward decl only — codec never includes UI/voice types.

namespace persistence
{
// Lane split is format-1 legacy: Note..Slide in `out`, Sustain/Release in
// `envelopes`, so old payloads stay a valid prefix of the new layout.
void capturePattern(const Sequencer &sequencer, PatternSnapshot &out,
                    EnvelopeTracksSnapshot &envelopes) noexcept;
// maxStepCount caps restored loop lengths (e.g. older firmware); over-long
// tails are still kept in storage, as on any shortened lane.
void applyPattern(const PatternSnapshot &in, const EnvelopeTracksSnapshot &envelopes,
                  Sequencer &sequencer,
                  uint8_t maxStepCount = SequencerConstants::MAX_STEPS_COUNT) noexcept;
} // namespace persistence

#endif
