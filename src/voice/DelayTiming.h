#pragma once

// Musical delay divisions. Kept independent of the UI and Arduino so the
// control core and audio core use exactly the same note/time mapping.
#include <cstdint>

namespace DelayTiming
{
struct NoteValue
{
    uint8_t beatNumerator;
    uint8_t beatDenominator;
    const char *label;
};

// Longest to shortest in actual duration. Dotted values and triplets run
// through 64ths; the fader moves from short (left) to long (right).
inline constexpr NoteValue kNotes[] = {
    {4, 1, "WHOLE"},       {3, 1, "1/2 DOTTED"},
    {2, 1, "1/2"},         {3, 2, "1/4 DOTTED"},
    {4, 3, "1/2 TRIPLET"}, {1, 1, "1/4"},
    {3, 4, "1/8 DOTTED"},  {2, 3, "1/4 TRIPLET"},
    {1, 2, "1/8"},         {3, 8, "1/16 DOTTED"},
    {1, 3, "1/8 TRIPLET"}, {1, 4, "1/16"},
    {3, 16, "1/32 DOTTED"},{1, 6, "1/16 TRIPLET"},
    {1, 8, "1/32"},        {3, 32, "1/64 DOTTED"},
    {1, 12, "1/32 TRIPLET"},{1, 16, "1/64"},
    {1, 24, "1/64 TRIPLET"},
};
inline constexpr uint8_t kNoteCount = sizeof(kNotes) / sizeof(kNotes[0]);
inline constexpr uint8_t kDefaultNoteIndex = 5; // quarter note

inline constexpr uint8_t clampIndex(uint8_t index) noexcept
{
    return index < kNoteCount ? index : kNoteCount - 1;
}

inline uint8_t indexForFader(float normalized) noexcept
{
    if (!(normalized > 0.0f)) return kNoteCount - 1;
    if (normalized >= 1.0f) return 0;
    return static_cast<uint8_t>(kNoteCount - 1 -
                                static_cast<uint8_t>(normalized * kNoteCount));
}

inline float secondsForIndex(uint8_t index, float bpm) noexcept
{
    // uClock's supported range is 45..200 BPM. A bad target cannot request
    // a delay longer than the line or create an invalid division.
    if (!(bpm >= 45.0f)) bpm = 45.0f;
    if (bpm > 200.0f) bpm = 200.0f;
    const auto &note = kNotes[clampIndex(index)];
    return (60.0f / bpm) *
           (static_cast<float>(note.beatNumerator) / note.beatDenominator);
}

inline const char *labelForIndex(uint8_t index) noexcept
{
    return kNotes[clampIndex(index)].label;
}
} // namespace DelayTiming
