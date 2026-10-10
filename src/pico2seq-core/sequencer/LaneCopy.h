#ifndef LANE_COPY_H
#define LANE_COPY_H
// LaneCopy: lift one parameter lane out of a voice's sequencer and set it down on
// a lane of another voice (or another lane of the same voice).
// Portable C++ — no Arduino, UI or hardware includes (keep pico2seq-core reusable).
#include "SequencerDefs.h"

class Sequencer;

namespace lanecopy
{

/**
 * @brief One lane's complete stored contents: the loop length and all 64 raw
 * slots, including the tail behind a shortened lane. Plain data (~260 bytes),
 * so a UI can keep one as its "clipboard" without heap.
 *
 * Values are the lane's own stored numbers — the same ones the session file
 * keeps — not what the voice plays: Note holds scale degrees, the patch-following
 * lanes may hold LANE_FOLLOWS_PATCH, the rest are 0..1. The destination voice
 * applies its own patch to them when it plays.
 */
struct LaneSnapshot
{
    ParamId lane = ParamId::Count;
    uint8_t length = 0;
    uint8_t loopStart = 0; // First looped step (Sequencer::setParameterLoop)
    float values[SequencerConstants::MAX_STEPS_COUNT] = {};

    bool valid() const noexcept { return lane < ParamId::Count && length >= 1; }
};

/**
 * @brief Copy `lane` out of `src`. `out` is overwritten whole; an out-of-range
 * lane leaves it invalid. Read-only on the sequencer.
 */
void capture(const Sequencer &src, ParamId lane, LaneSnapshot &out) noexcept;

/**
 * @brief Make `lane` of `dst` match the snapshot: all 64 slots and the loop
 * length, so the pasted lane loops exactly as the source did.
 *
 * Same lane: the stored numbers move verbatim. Different lane: each value goes
 * through convertValue(), so a Filter shape pasted on Velocity keeps its shape.
 * Transport, cursors, gates of other lanes and the voice's patch are untouched.
 *
 * @return false (nothing written) when the snapshot is invalid or `lane` is out of range.
 */
bool paste(const LaneSnapshot &in, Sequencer &dst, ParamId lane) noexcept;

/**
 * @brief One stored value of lane `from`, expressed as a stored value of lane `to`.
 *
 * Both values are placed on their lane's min..max range as 0..1 and mapped onto
 * the other range. A step that follows the patch stays following when the target
 * lane can (the patch-following lanes) and otherwise becomes the target's default;
 * Octave snaps to its five detents. The result is in range but not yet rounded or
 * toggled — Sequencer::setStepParameterValue() does that on store.
 */
float convertValue(ParamId from, ParamId to, float stored) noexcept;

} // namespace lanecopy

#endif // LANE_COPY_H
