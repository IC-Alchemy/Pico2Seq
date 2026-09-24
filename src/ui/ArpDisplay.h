#ifndef ARP_DISPLAY_H
#define ARP_DISPLAY_H

#include "../pico2seq-core/arpeggiator/Arpeggiator.h"
#include "../pico2seq-core/scales/scales.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ArpDisplay
{
inline constexpr size_t kColumns = 21;
using Row = char[kColumns + 1];

inline void fit(const char *text, Row &row)
{
    if (!text)
    {
        row[0] = '\0';
        return;
    }
    const size_t length = std::strlen(text);
    if (length <= kColumns)
    {
        std::snprintf(row, sizeof(row), "%s", text);
        return;
    }
    std::memcpy(row, text, kColumns);
    row[kColumns - 1] = '~';
    row[kColumns] = '\0';
}

inline unsigned swingLong(const Arpeggiator::Settings &settings) noexcept
{
    const uint16_t shortTicks = Arpeggiator::intervalTicks(settings, false);
    const uint16_t longTicks = Arpeggiator::intervalTicks(settings, true);
    const unsigned total = static_cast<unsigned>(shortTicks) + static_cast<unsigned>(longTicks);
    return total ? static_cast<unsigned>((100u * longTicks + total / 2u) / total) : 50u;
}

inline void gate(const Arpeggiator::Settings &settings, float bpm, Row &row)
{
    if (!(bpm > 0.0f))
    {
        fit("--", row);
        return;
    }

    const float msPerTick = 60000.0f / (bpm * 480.0f);
    const unsigned a = static_cast<unsigned>(std::lround(
        Arpeggiator::gateTicks(settings, false) * msPerTick));
    const unsigned b = static_cast<unsigned>(std::lround(
        Arpeggiator::gateTicks(settings, true) * msPerTick));
    const unsigned low = std::min(a, b);
    const unsigned high = std::max(a, b);
    if (low == high)
        std::snprintf(row, sizeof(row), "%ums", low);
    else
        std::snprintf(row, sizeof(row), "%u-%ums", low, high);
}

inline void chord(const Arpeggiator::Engine &arp, const int *scaleRow, Row &row)
{
    if (arp.chordCount() == 0)
    {
        fit("Touch pads for chord", row);
        return;
    }

    constexpr const char *kPitchNames[12] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };

    const bool useOrder = arp.settings().pattern == Arpeggiator::Pattern::Order;
    char text[64] = "KEYS";
    uint8_t shown = 0;

    for (uint8_t i = 0; i < arp.chordCount(); ++i)
    {
        const uint8_t degree = useOrder ? arp.orderDegree(i) : arp.chordDegree(i);
        if (degree == Arpeggiator::kNoDegree)
            continue;

        const int semitone = scaleRow
            ? scaleRow[std::min<size_t>(degree, SCALE_STEPS - 1)]
            : static_cast<int>(degree);
        const int pitchClass = ((semitone % 12) + 12) % 12;
        const int octave = 3 + semitone / 12;

        char token[8];
        std::snprintf(token, sizeof(token), "%s%d", kPitchNames[pitchClass], octave);

        const uint8_t omitted = static_cast<uint8_t>(arp.chordCount() - (i + 1));
        char candidate[64];
        std::snprintf(candidate, sizeof(candidate), "%s %s", text, token);
        if (omitted)
        {
            char withOmitted[64];
            std::snprintf(withOmitted, sizeof(withOmitted), "%s +%u", candidate,
                          static_cast<unsigned>(omitted));
            if (std::strlen(withOmitted) > kColumns)
                break;
        }
        else if (std::strlen(candidate) > kColumns)
        {
            break;
        }

        std::snprintf(text, sizeof(text), "%s", candidate);
        ++shown;
    }

    const uint8_t omitted = static_cast<uint8_t>(arp.chordCount() - shown);
    if (omitted)
    {
        char withOmitted[64];
        std::snprintf(withOmitted, sizeof(withOmitted), "%s +%u", text,
                      static_cast<unsigned>(omitted));
        fit(withOmitted, row);
        return;
    }

    fit(text, row);
}
} // namespace ArpDisplay

#endif // ARP_DISPLAY_H
