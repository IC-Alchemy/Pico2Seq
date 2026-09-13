#ifndef PICO2SEQ_PATTERN_CODEC_H
#define PICO2SEQ_PATTERN_CODEC_H

#include "ProjectSnapshot.h"

class Sequencer;

namespace persistence
{
void capturePattern(const Sequencer &sequencer, PatternSnapshot &out) noexcept;
void applyPattern(const PatternSnapshot &in, Sequencer &sequencer) noexcept;
} // namespace persistence

#endif
