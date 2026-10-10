#pragma once

#include "LaneDisplay.h"
#include <algorithm>
#include <cstdio>

namespace LaneDisplay {
// Reuses the existing contextual page at 2x, surrounded by an instrument frame
// and all four gate lanes. The mono buffer is SH1106 page-order (128x64).
template <class Canvas>
void renderFocus(Canvas &canvas, const Model &model, const uint8_t *mono) {
    constexpr uint16_t background = kBackground;
    constexpr uint16_t muted = kMuted;
    constexpr uint16_t ink = kInk;
    canvas.fillRect(0, kHeaderHeight, kWidth, kFooterY - kHeaderHeight, background);
    canvas.text(16, 82, "LIVE CONTEXT / EDIT & PERFORMANCE", muted);
    canvas.drawRect(24, 104, 272, 144, 0x2946);
    if (mono) {
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 128; ++x) {
                if (mono[(y / 8) * 128 + x] & (1u << (y % 8)))
                    canvas.fillRect(32 + x * 2, 112 + y * 2, 2, 2, ink);
            }
        }
    }
    canvas.text(16, 267, "FOUR VOICES / GATE TIMELINES", muted);
    for (uint8_t voice = 0; voice < 4; ++voice) {
        const int y = 290 + voice * 38;
        char label[32];
        std::snprintf(label, sizeof(label), "V%u  %.15s", voice + 1, model.voiceNames[voice]);
        canvas.text(16, y, label, voiceAccent(voice));
        const auto &lane = model.lanes[voice][static_cast<uint8_t>(ParamId::Gate)];
        const int count = std::clamp<int>(lane.count, 2, 64);
        const int start = std::clamp<int>(lane.start, 0, count - 2);
        for (int step = 0; step < count; ++step) {
            const int x = 16 + step * 288 / count;
            const int next = 16 + (step + 1) * 288 / count;
            canvas.fillRect(x, y + 13, std::max(1, next - x - 1), 10,
                            step < start ? kExcluded :
                            lane.values[step] > 127 ? voiceAccent(voice) : kPanelEdge);
        }
        const int cursor = std::clamp<int>(lane.cursor, start, count - 1);
        const int x = 16 + cursor * 288 / count;
        canvas.drawLine(x, y + 11, x, y + 25, ink);
    }
}
}
