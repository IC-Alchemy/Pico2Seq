#ifndef LOOP_RANGE_GESTURE_H
#define LOOP_RANGE_GESTURE_H

#include <cstdint>

/**
 * @brief Two-pad loop gesture: hold one step pad, press another on the same
 * voice row, and the selected parameter lane loops between those two steps.
 *
 * Pure pad bookkeeping, no sequencer or Arduino types: the UI funnel reports
 * every pad edge (notePress/noteRelease wrap the whole handler, so modal
 * pages that swallow edges cannot leave a pad stuck), asks pairFor() when a
 * press is eligible, and checks consumed() on release so neither pad of a
 * gesture also toggles a gate or opens step edit.
 *
 * The anchor is the pad that went down while no other pad was held. Each
 * further pad in the anchor's bank pairs with it, so holding step 4 and
 * tapping 8 then 12 loops 4..8 and then 4..12.
 *
 * Tested by tests/unit/test_loop_range_gesture.cpp.
 */
namespace LoopRange
{
constexpr uint8_t kPadsPerBank = 16;

struct Pair
{
    bool valid = false;
    uint8_t anchorPad = 0; // raw pad index (0..31)
    uint8_t otherPad = 0;
};

class Gesture
{
public:
    // Every pad press edge, before any routing.
    void notePress(uint8_t pad) noexcept
    {
        if (pad >= 32)
            return;
        if (held_ == 0)
            anchor_ = static_cast<int8_t>(pad);
        held_ |= bit(pad);
    }

    // Every pad release edge, after routing (so consumed() still answers).
    void noteRelease(uint8_t pad) noexcept
    {
        if (pad >= 32)
            return;
        held_ &= ~bit(pad);
        consumed_ &= ~bit(pad);
        if (anchor_ == static_cast<int8_t>(pad))
            anchor_ = -1;
    }

    /**
     * @brief Pair this press with the held anchor. On success both pads are
     * marked consumed (their releases do nothing else).
     */
    Pair pairFor(uint8_t pad) noexcept
    {
        Pair pair;
        if (pad >= 32 || anchor_ < 0 || static_cast<uint8_t>(anchor_) == pad)
            return pair;
        const uint8_t anchor = static_cast<uint8_t>(anchor_);
        if (!(held_ & bit(anchor)) || anchor / kPadsPerBank != pad / kPadsPerBank)
            return pair;
        consumed_ |= bit(anchor) | bit(pad);
        pair.valid = true;
        pair.anchorPad = anchor;
        pair.otherPad = pad;
        return pair;
    }

    bool consumed(uint8_t pad) const noexcept
    {
        return pad < 32 && (consumed_ & bit(pad)) != 0;
    }

private:
    static constexpr uint32_t bit(uint8_t pad) { return 1u << pad; }

    uint32_t held_ = 0;
    uint32_t consumed_ = 0;
    int8_t anchor_ = -1;
};
} // namespace LoopRange

#endif // LOOP_RANGE_GESTURE_H
