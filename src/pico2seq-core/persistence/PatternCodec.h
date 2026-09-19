#ifndef PICO2SEQ_PATTERN_CODEC_H
#define PICO2SEQ_PATTERN_CODEC_H

#include "ProjectSnapshot.h"

class Sequencer;

namespace persistence
{
// Lanes Note..Slide go to `out`, Sustain and Release to `envelopes`.
void capturePattern(const Sequencer &sequencer, PatternSnapshot &out,
                    EnvelopeTracksSnapshot &envelopes) noexcept;
// maxStepCount caps each restored lane's active length. Values stored past the
// cap are kept, as on any shortened lane.
void applyPattern(const PatternSnapshot &in, const EnvelopeTracksSnapshot &envelopes,
                  Sequencer &sequencer,
                  uint8_t maxStepCount = SequencerConstants::MAX_STEPS_COUNT) noexcept;
} // namespace persistence

#endif
