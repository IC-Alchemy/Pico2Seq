#ifndef PICO2SEQ_PATTERN_CODEC_H
#define PICO2SEQ_PATTERN_CODEC_H

#include "ProjectSnapshot.h"

class Sequencer;

namespace persistence
{
void capturePattern(const Sequencer &sequencer, PatternSnapshot &out) noexcept;
// maxStepCount caps each restored lane's active length. Values stored past the
// cap are kept, as on any shortened lane.
void applyPattern(const PatternSnapshot &in, Sequencer &sequencer,
                  uint8_t maxStepCount = SequencerConstants::MAX_STEPS_COUNT) noexcept;
} // namespace persistence

#endif
