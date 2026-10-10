#pragma once

#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include <cstdint>
#include <cstdio>

// LaneDisplay: the 320x480 portrait visualizations for the optional ST7796.
//
// Musical role: show every sequencer lane's shape, length, loop start and live
// cursor so polymetric drift is visible instead of inferred. Technical role: a
// pure, allocation-free renderer over a tiny Canvas contract, so the firmware
// (320x8 RGB565 strips), the host tests and the demo image generator all replay
// the SAME drawing code.
//
// Everything here is read-only. No transport, no DSP, no persistence.
// The Canvas contract is deliberately small:
//   fillRect(x, y, w, h, rgb565)   drawRect(x, y, w, h, rgb565)
//   drawLine(x0, y0, x1, y1, c)    fillCircle(cx, cy, r, c)   drawCircle(...)
//   text(x, y, const char *, rgb565, size = 1)

namespace LaneDisplay {

// --- Geometry ----------------------------------------------------------------
inline constexpr int kWidth = 320;
inline constexpr int kHeight = 480;
inline constexpr int kHeaderHeight = 68;
inline constexpr int kFooterY = 452;
inline constexpr int kFooterHeight = kHeight - kFooterY;
inline constexpr int kMatrixRows = 8;
inline constexpr int kMatrixCellHeight = (kFooterY - kHeaderHeight) / kMatrixRows; // 48
inline constexpr int kMatrixCellWidth = kWidth / 2;                               // 160
inline constexpr int kObservatorySections = 8;
inline constexpr int kObservatoryCompareCount = 4;
inline constexpr int kObservatoryCompareHeight = 64;
inline constexpr int kObservatoryPhaseHeight = 32;

struct Rect
{
    int16_t x = 0, y = 0, w = 0, h = 0;
};

// Row-major contents (cell / 2) x (cell % 2); row 0 is the top lane pair.
constexpr Rect matrixCellRect(uint8_t cell) noexcept
{
    const int index = cell < kMatrixRows * 2 ? cell : kMatrixRows * 2 - 1;
    return Rect{static_cast<int16_t>((index % 2) * kMatrixCellWidth),
                static_cast<int16_t>(kHeaderHeight + (index / 2) * kMatrixCellHeight),
                kMatrixCellWidth, kMatrixCellHeight};
}

// Sections 0..3 compare one voice across lanes (64px each); sections 4..7 show
// one voice's loop phasing (32px each). Together they fill header..footer.
constexpr Rect observatorySectionRect(uint8_t section) noexcept
{
    const int index = section < kObservatorySections ? section : kObservatorySections - 1;
    if (index < kObservatoryCompareCount)
        return Rect{0, static_cast<int16_t>(kHeaderHeight + index * kObservatoryCompareHeight),
                    kWidth, kObservatoryCompareHeight};
    return Rect{0, static_cast<int16_t>(kHeaderHeight + kObservatoryCompareCount * kObservatoryCompareHeight +
                                       (index - kObservatoryCompareCount) * kObservatoryPhaseHeight),
                kWidth, kObservatoryPhaseHeight};
}

static_assert(kHeaderHeight + kObservatoryCompareCount * kObservatoryCompareHeight +
                      (kObservatorySections - kObservatoryCompareCount) * kObservatoryPhaseHeight ==
                  kFooterY,
              "Observatory sections must fill the body exactly");

constexpr Rect inset(const Rect &r, int dx, int dy) noexcept
{
    return Rect{static_cast<int16_t>(r.x + dx), static_cast<int16_t>(r.y + dy),
                static_cast<int16_t>(r.w - 2 * dx), static_cast<int16_t>(r.h - 2 * dy)};
}

// --- Palette (RGB565; the near-black instrument look) ------------------------
inline constexpr uint16_t kBackground = 0x0842;
inline constexpr uint16_t kPanel = 0x10A4;
inline constexpr uint16_t kPanelEdge = 0x2946;
inline constexpr uint16_t kInk = 0xEF7D;
inline constexpr uint16_t kMuted = 0x8C71;
inline constexpr uint16_t kExcluded = 0x2126; // steps before a nonzero loop start
inline constexpr uint16_t kCursor = 0xFFFF;   // the play head, and nothing else
inline constexpr uint16_t kLoopExtent = 0xCE59; // the lane's own loop span
inline constexpr uint16_t kVoiceAccent[4] = {0x4EFD, 0xFDA6, 0xB37F, 0x6F35};

constexpr uint16_t voiceAccent(uint8_t voice) noexcept
{
    return kVoiceAccent[voice < 4 ? voice : 3];
}

// The Matrix columns are the physical step-pad banks: voices 1-2, or 3-4.
// Wrapped, never indexed, so a stale selection cannot read past the arrays.
constexpr uint8_t voicePairFirst(uint8_t selectedVoice) noexcept
{
    return static_cast<uint8_t>(((selectedVoice % 4) / 2) * 2);
}

// --- Pages, banks and styles -------------------------------------------------
enum class Page : uint8_t { Focus = 0, Matrix, Observatory, Count };

enum class Style : uint8_t
{
    Trace = 0,  // polyline through the stored values
    Stair,      // sample-and-hold staircase
    Bars,       // one bar per step
    Dots,       // bubble scatter, radius encodes level
    Heat,       // intensity cells, no vertical axis
    Ribbon,     // mirrored thickness around a centre line
    Orbit,      // radial radar, angle is time
    Ticks,      // quantized ruler plus the current composed value
    Count
};

constexpr const char *pageName(Page page) noexcept
{
    switch (page)
    {
    case Page::Focus: return "FOCUS";
    case Page::Matrix: return "LANE MATRIX";
    case Page::Observatory: return "LOOP OBSERVATORY";
    default: return "?";
    }
}

constexpr const char *styleName(Style style) noexcept
{
    switch (style)
    {
    case Style::Trace: return "TRACE";
    case Style::Stair: return "STAIR";
    case Style::Bars: return "BARS";
    case Style::Dots: return "DOTS";
    case Style::Heat: return "HEAT";
    case Style::Ribbon: return "RIBBON";
    case Style::Orbit: return "ORBIT";
    case Style::Ticks: return "TICKS";
    default: return "?";
    }
}

// Two eight-row banks cover all eleven real lanes; the extended bank adds the
// envelope/detent lanes and repeats the main ones for side-by-side comparison.
inline constexpr uint8_t kLaneBankCount = 2;
inline constexpr uint8_t kLanesPerBank = 8;
inline constexpr ParamId kLaneBank[2][kLanesPerBank] = {
    {ParamId::Note, ParamId::Velocity, ParamId::Filter, ParamId::Attack,
     ParamId::Release, ParamId::Octave, ParamId::GateLength, ParamId::Gate},
    {ParamId::Decay, ParamId::Sustain, ParamId::Slide, ParamId::Velocity,
     ParamId::Filter, ParamId::Attack, ParamId::Release, ParamId::Note}};

constexpr ParamId bankLane(uint8_t bank, uint8_t row) noexcept
{
    return kLaneBank[bank % kLaneBankCount][row % kLanesPerBank];
}

constexpr Page cyclePage(Page page) noexcept
{
    return static_cast<Page>((static_cast<uint8_t>(page) + 1) % static_cast<uint8_t>(Page::Count));
}

constexpr Style cycleStyle(Style style) noexcept
{
    return static_cast<Style>((static_cast<uint8_t>(style) + 1) % static_cast<uint8_t>(Style::Count));
}

// --- Model -------------------------------------------------------------------
inline constexpr uint8_t kLaneCount = SequencerConstants::MAX_STEPS_COUNT; // 64

struct Lane
{
    // Composed playback levels, 0..255 across this lane's defined range. Never a
    // patch-follow sentinel and never a raw volatile read.
    uint8_t values[kLaneCount] = {};
    uint8_t count = 2;      // lane length, 2..64
    uint8_t start = 0;      // first step of this lane's own loop
    uint8_t cursor = 0;     // step sounding now, inside [start, count-1]
    char label[12] = {};    // patch-specific name, else the standard one
    char valueText[24] = {};// current composed value in musical units
};

struct Model
{
    Lane lanes[4][PARAM_ID_COUNT] = {};
    char voiceNames[4][16] = {};
    uint8_t selectedVoice = 0; // 0-based; the Matrix shows this voice's pair
    float tempo = 90.0f;
    bool running = false;
    Style style = Style::Trace;
    Page page = Page::Matrix;
    uint8_t laneBank = 0;
};

// Display-only navigation state. Lives in UIState so a page flip is one field,
// and is intentionally absent from the song's persistence format.
struct Controls
{
    Page page = Page::Matrix;
    Style style = Style::Trace;
    uint8_t laneBank = 0;
};

// --- Lane geometry helpers ---------------------------------------------------
constexpr uint8_t laneLength(const Lane &lane) noexcept
{
    return lane.count < SequencerConstants::MIN_STEPS_COUNT ? SequencerConstants::MIN_STEPS_COUNT
           : lane.count > kLaneCount                          ? kLaneCount
                                                             : lane.count;
}

constexpr uint8_t laneStart(const Lane &lane) noexcept
{
    return lane.start >= laneLength(lane) ? 0 : lane.start;
}

constexpr uint8_t laneCursor(const Lane &lane) noexcept
{
    const uint8_t start = laneStart(lane);
    const uint8_t last = laneLength(lane) - 1;
    return lane.cursor < start ? start : (lane.cursor > last ? last : lane.cursor);
}

constexpr int levelY(const Rect &r, uint8_t level) noexcept
{
    const int span = r.h - 1;
    return r.y + span - (level * span) / 255;
}

constexpr int stepX(const Rect &r, uint8_t step, uint8_t count) noexcept
{
    if (count < 2)
        return r.x;
    return r.x + (step * (r.w - 1)) / (count - 1);
}

// Excluded steps (before a nonzero loop start) stay visible but recede, so the
// lane's real length is never disguised by cropping.
constexpr uint16_t stepColour(uint8_t step, uint8_t start, uint16_t accent) noexcept
{
    return step < start ? kExcluded : accent;
}

// A common result shape for the stress tests: anything drawn outside this rect
// means a bug in geometry, not a layout choice.
constexpr bool within(const Rect &outer, const Rect &inner) noexcept
{
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}

// --- Colour and integer trigonometry ----------------------------------------
constexpr uint16_t scale565(uint16_t colour, uint16_t num, uint16_t den) noexcept
{
    if (den == 0) return colour;
    const uint16_t r = static_cast<uint16_t>((((colour >> 11) & 0x1Fu) * num) / den);
    const uint16_t g = static_cast<uint16_t>((((colour >> 5) & 0x3Fu) * num) / den);
    const uint16_t b = static_cast<uint16_t>(((colour & 0x1Fu) * num) / den);
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// Intensity floor keeps a zero step visible as a cell rather than a hole.
constexpr uint16_t heat565(uint16_t accent, uint8_t level) noexcept
{
    return scale565(accent, static_cast<uint16_t>(70 + (level * 185) / 255), 255);
}

// sin(0..90 degrees in 6-degree steps) x 1024: the radar needs no libm, no
// runtime table and no allocation, and stays identical on host and firmware.
inline constexpr int kSinQuarter[16] = {0, 107, 213, 316, 416, 512, 602, 685,
                                        761, 828, 887, 936, 974, 1002, 1018, 1024};

constexpr int sinDeg(int degrees) noexcept
{
    int d = degrees % 360;
    if (d < 0) d += 360;
    const int index = d <= 90 ? d / 6 : (d <= 180 ? (180 - d) / 6 : (d <= 270 ? (d - 180) / 6 : (360 - d) / 6));
    const int clamped = index > 15 ? 15 : index;
    const bool negative = d > 180;
    return negative ? -kSinQuarter[clamped] : kSinQuarter[clamped];
}

constexpr int cosDeg(int degrees) noexcept { return sinDeg(degrees + 90); }

// --- The eight encodings -----------------------------------------------------
// Every style keeps the same three facts readable: time order along the lane,
// the lane's own loop extent, and where its play head is right now.
template <class Canvas>
void drawLanePlot(Canvas &canvas, const Lane &lane, const Rect &plot, uint16_t accent,
                  Style style)
{
    canvas.fillRect(plot.x, plot.y, plot.w, plot.h, kPanel);
    const uint8_t count = laneLength(lane);
    const uint8_t start = laneStart(lane);
    const uint8_t cursor = laneCursor(lane);
    const int bottom = plot.y + plot.h - 1;

    switch (style)
    {
    case Style::Trace:
    {
        canvas.fillRect(plot.x, bottom, plot.w, 1, kPanelEdge);
        for (uint8_t step = 0; step + 1 < count; ++step)
        {
            canvas.drawLine(stepX(plot, step, count), levelY(plot, lane.values[step]),
                            stepX(plot, step + 1, count), levelY(plot, lane.values[step + 1]),
                            stepColour(step, start, accent));
        }
        for (uint8_t step = 0; step < count; ++step)
        {
            if (step >= start)
                canvas.fillCircle(stepX(plot, step, count), levelY(plot, lane.values[step]), 1,
                                  kInk);
        }
        break;
    }
    case Style::Stair:
    {
        canvas.fillRect(plot.x, bottom, plot.w, 1, kPanelEdge);
        for (uint8_t step = 0; step < count; ++step)
        {
            const int x = stepX(plot, step, count);
            const int next = step + 1 < count ? stepX(plot, step + 1, count) : plot.x + plot.w - 1;
            const int y = levelY(plot, lane.values[step]);
            canvas.fillRect(x, y, next - x < 1 ? 1 : next - x, 2, stepColour(step, start, accent));
            if (step + 1 < count)
                canvas.fillRect(next, y, 1, levelY(plot, lane.values[step + 1]) - y + 1, kPanelEdge);
        }
        break;
    }
    case Style::Bars:
    {
        canvas.fillRect(plot.x, bottom, plot.w, 1, kPanelEdge);
        const int slot = plot.w / count < 1 ? 1 : plot.w / count;
        for (uint8_t step = 0; step < count; ++step)
        {
            const int x = plot.x + (step * (plot.w - 1)) / count;
            const int top = levelY(plot, lane.values[step]);
            canvas.fillRect(x, top, slot > 1 ? slot - 1 : 1, bottom - top + 1,
                            stepColour(step, start, accent));
        }
        break;
    }
    case Style::Dots:
    {
        canvas.fillRect(plot.x, bottom, plot.w, 1, kPanelEdge);
        // Radius carries level; a dense style trades precision for shape.
        const int maxRadius = plot.h / 5 < 1 ? 1 : plot.h / 5;
        for (uint8_t step = 0; step < count; ++step)
        {
            const int radius = 1 + (lane.values[step] * maxRadius) / 255;
            canvas.fillCircle(stepX(plot, step, count), levelY(plot, lane.values[step]), radius,
                              stepColour(step, start, accent));
        }
        break;
    }
    case Style::Heat:
    {
        // No vertical axis at all: cell brightness alone carries the level, so a
        // 64-step lane still fits a narrow cell without losing any step.
        const int slot = plot.w / count < 1 ? 1 : plot.w / count;
        for (uint8_t step = 0; step < count; ++step)
        {
            const int x = plot.x + (step * plot.w) / count;
            const int width = slot - 1 < 1 ? 1 : slot - 1;
            canvas.fillRect(x, plot.y + 1, width, plot.h - 2,
                            heat565(accent, lane.values[step]));
        }
        break;
    }
    case Style::Ribbon:
    {
        // Mirrored thickness around a centre line: loud steps bloom, quiet steps
        // pinch, and the vertical midpoint is a readable zero axis.
        const int centre = plot.y + plot.h / 2;
        canvas.fillRect(plot.x, centre, plot.w, 1, kPanelEdge);
        const int slot = plot.w / count < 1 ? 1 : plot.w / count;
        const int maxHalf = plot.h / 2 - 1 < 1 ? 1 : plot.h / 2 - 1;
        for (uint8_t step = 0; step < count; ++step)
        {
            const int x = plot.x + (step * plot.w) / count;
            const int half = 1 + (lane.values[step] * maxHalf) / 255;
            const int width = slot - 1 < 1 ? 1 : slot - 1;
            canvas.fillRect(x, centre - half, width, half * 2 + 1,
                            stepColour(step, start, accent));
        }
        break;
    }
    case Style::Orbit:
    {
        // Angle is time, radius is level: one closed lap of the lane is one
        // revolution, so a loop's length is visible as its own period.
        const int cx = plot.x + plot.w / 2;
        const int cy = plot.y + plot.h / 2;
        const int radius = (plot.h / 2) - 2 < 2 ? 2 : (plot.h / 2) - 2;
        canvas.drawCircle(cx, cy, radius, kPanelEdge);
        int previousX = 0;
        int previousY = 0;
        for (uint8_t step = 0; step < count; ++step)
        {
            const int angle = -90 + (step * 360) / count;
            const int reach = (radius * (64 + (lane.values[step] * 3) / 5)) / 256;
            const int x = cx + (reach * cosDeg(angle)) / 1024;
            const int y = cy + (reach * sinDeg(angle)) / 1024;
            if (step > 0)
                canvas.drawLine(previousX, previousY, x, y, stepColour(step, start, accent));
            previousX = x;
            previousY = y;
        }
        if (count > 1)
        {
            const int angle = -90 + (cursor * 360) / count;
            const int reach = (radius * (64 + (lane.values[cursor] * 3) / 5)) / 256;
            canvas.drawLine(cx, cy, cx + (reach * cosDeg(angle)) / 1024,
                            cy + (reach * sinDeg(angle)) / 1024, kCursor);
        }
        break;
    }
    case Style::Ticks:
    default:
    {
        // A ruler, not a picture: quantized ticks show the shape and the composed
        // value itself stays legible even in a 26px-tall cell.
        const int centre = plot.y + plot.h / 2;
        canvas.fillRect(plot.x, centre, plot.w, 1, kPanelEdge);
        const int slot = plot.w / count < 1 ? 1 : plot.w / count;
        for (uint8_t step = 0; step < count; ++step)
        {
            const int x = plot.x + (step * plot.w) / count;
            const int up = centre - plot.y - 1;
            const int height = lane.values[step] >= 128
                                   ? 1 + ((lane.values[step] - 128) * up) / 127
                                   : 1 + ((128 - lane.values[step]) * up) / 128;
            const int width = slot - 2 < 1 ? 1 : slot - 2;
            if (lane.values[step] >= 128)
                canvas.fillRect(x, centre - height, width, height, stepColour(step, start, accent));
            else
                canvas.fillRect(x, centre + 1, width, height, stepColour(step, start, accent));
        }
        break;
    }
    }

    // Cursor last, so it is never painted over: a 2px column with a heavier head.
    // Drawn on one column, so the play head position stays exactly computable.
    const int cursorX = stepX(plot, cursor, count);
    canvas.fillRect(cursorX, plot.y, 2, plot.h, scale565(kCursor, 110, 255));
    canvas.fillRect(cursorX, plot.y, 2, 3, kCursor);

    // Loop extent: an underline from the loop start to the lane end. Its own
    // colour, so the play head stays the only white mark in the cell.
    const int loopX = stepX(plot, start, count);
    const int loopEnd = plot.x + plot.w - 1;
    if (start > 0)
        canvas.fillRect(loopX, bottom - 1, loopEnd - loopX + 1, 1, kLoopExtent);
}

// Right-aligned text, measured with the same 6px advance the canvas fonts use.
template <class Canvas>
void textRight(Canvas &canvas, int rightX, int y, const char *value, uint16_t colour,
               uint8_t size = 1)
{
    int length = 0;
    while (value[length] != '\0' && length < 63) ++length;
    const int width = length * 6 * size;
    canvas.text(rightX - width, y, value, colour, size);
}

// Copy at most as many characters as fit, so a long composed value can never
// overrun the lane name beside it. Out is always terminated.
inline void fitText(const char *text, char *out, size_t size, int maxPixels) noexcept
{
    if (size == 0) return;
    const int limit = maxPixels / 6;
    size_t index = 0;
    while (text[index] != '\0' && index < size - 1 && static_cast<int>(index) < limit)
    {
        out[index] = text[index];
        ++index;
    }
    out[index] = '\0';
}

// The Observatory draws each lane as a micro chart; at five pixels tall the eight
// styles collapse into three honest encodings (and the page says which one it is
// using), rather than pretending a radar fits in five pixels.
enum class MicroForm : uint8_t { Line = 0, Column, Brightness };

constexpr MicroForm microForm(Style style) noexcept
{
    switch (style)
    {
    case Style::Trace:
    case Style::Stair:
    case Style::Ribbon:
    case Style::Orbit: return MicroForm::Line;
    case Style::Bars:
    case Style::Ticks:
    case Style::Dots: return MicroForm::Column;
    case Style::Heat:
    default: return MicroForm::Brightness;
    }
}

constexpr const char *microFormName(MicroForm form) noexcept
{
    switch (form)
    {
    case MicroForm::Line: return "LINES";
    case MicroForm::Column: return "COLUMNS";
    case MicroForm::Brightness: return "BRIGHTNESS";
    default: return "?";
    }
}

// One lane as a 5px-tall aligned chart. xStep maps an absolute step index to the
// shared axis, so every row in the section is read against the same ruler.
template <class Canvas, class AxisStep>
void drawMicroLane(Canvas &canvas, const Lane &lane, int rowY, uint16_t accent,
                   MicroForm form, const AxisStep &xStep)
{
    const uint8_t count = laneLength(lane);
    const uint8_t start = laneStart(lane);
    const uint8_t cursor = laneCursor(lane);
    int previousX = 0;
    int previousY = 0;
    for (uint8_t step = 0; step < count; ++step)
    {
        const int x = xStep(step);
        const int level = lane.values[step];
        const uint16_t colour = stepColour(step, start, accent);
        if (form == MicroForm::Brightness)
        {
            const int width = xStep(step + 1 < count ? step + 1 : step) - x + (step + 1 < count ? 0 : 2);
            canvas.fillRect(x, rowY, width < 1 ? 1 : width, 4, heat565(colour == kExcluded ? kPanelEdge : accent, static_cast<uint8_t>(level)));
            previousX = x;
            previousY = rowY;
            continue;
        }
        const int height = 1 + (level * 3) / 255;
        const int y = rowY + 4 - height;
        if (form == MicroForm::Column)
            canvas.fillRect(x, y, 2, height, colour);
        else
            canvas.fillRect(x, y, 2, 1, colour);
        if (form == MicroForm::Line && step > 0)
            canvas.drawLine(previousX, previousY, x, y, colour);
        previousX = x;
        previousY = y;
    }
    canvas.fillRect(xStep(cursor), rowY, 2, 5, kCursor);
}

// --- Page chrome -------------------------------------------------------------
// A lane's patch-specific name, falling back to the shared lane table.
inline const char *laneLabel(const Model &model, uint8_t voice, ParamId id) noexcept
{
    const char *label = model.lanes[voice][static_cast<uint8_t>(id)].label;
    const auto *definition = parameterDefinition(id);
    return label[0] != '\0' ? label : (definition ? definition->name : "--");
}

template <class Canvas>
void renderHeader(Canvas &canvas, const Model &model)
{
    canvas.fillRect(0, 0, kWidth, kHeaderHeight, kBackground);
    canvas.fillRect(0, 0, kWidth, 2, kPanelEdge);
    canvas.text(8, 6, "PICO2SEQ", kMuted);
    textRight(canvas, kWidth - 8, 6, pageName(model.page), kInk);

    canvas.fillRect(8, 18, 2, 26, voiceAccent(model.selectedVoice));
    if (model.page == Page::Matrix)
    {
        const uint8_t first = voicePairFirst(model.selectedVoice);
        char line[32];
        for (uint8_t column = 0; column < 2; ++column)
        {
            const uint8_t voice = static_cast<uint8_t>(first + column);
            std::snprintf(line, sizeof(line), "V%u %.11s", static_cast<unsigned>(voice) + 1,
                          model.voiceNames[voice][0] ? model.voiceNames[voice] : "-");
            canvas.text(static_cast<int>(column) * 160 + 14, 20, line, voiceAccent(voice), 2);
        }
    }
    else
    {
        char line[32];
        std::snprintf(line, sizeof(line), "V%u %.11s", static_cast<unsigned>(model.selectedVoice) + 1,
                      model.voiceNames[model.selectedVoice][0] ? model.voiceNames[model.selectedVoice]
                                                              : "-");
        canvas.text(14, 20, line, voiceAccent(model.selectedVoice), 2);
    }
    {
        char line[48];
        std::snprintf(line, sizeof(line), "%u BPM  %s", static_cast<unsigned>(model.tempo + 0.5f),
                      model.running ? "RUN" : "STOP");
        textRight(canvas, kWidth - 8, 22, line, model.running ? kInk : kMuted, 1);
    }

    char status[56];
    std::snprintf(status, sizeof(status), "BANK %u/%u %s   STYLE %.12s",
                  static_cast<unsigned>(model.laneBank % kLaneBankCount) + 1,
                  static_cast<unsigned>(kLaneBankCount),
                  model.laneBank % kLaneBankCount == 0 ? "MAIN " : "EXTRA",
                  styleName(model.style));
    canvas.text(8, 52, status, kMuted);
}

template <class Canvas>
void renderFooter(Canvas &canvas, const Model &model)
{
    canvas.fillRect(0, kFooterY, kWidth, kFooterHeight, kBackground);
    canvas.fillRect(0, kFooterY, kWidth, 1, kPanelEdge);
    canvas.text(8, kFooterY + 5, "HOLD SWING: VIEW   SHIFT+HOLD: STYLE", kMuted);
    // Legend: the three marks every style keeps.
    canvas.fillRect(8, kFooterY + 19, 8, 3, voiceAccent(model.selectedVoice));
    canvas.text(20, kFooterY + 17, "STEP", kMuted, 1);
    canvas.fillRect(54, kFooterY + 19, 8, 3, kExcluded);
    canvas.text(66, kFooterY + 17, "EXCLUDED", kMuted, 1);
    canvas.fillRect(128, kFooterY + 19, 8, 3, kLoopExtent);
    canvas.text(140, kFooterY + 17, "LOOP", kMuted, 1);
    canvas.fillRect(176, kFooterY + 19, 8, 3, kCursor);
    canvas.text(188, kFooterY + 17, "CURSOR", kMuted, 1);
    char line[32];
    std::snprintf(line, sizeof(line), "%u STYLES", static_cast<unsigned>(Style::Count));
    textRight(canvas, kWidth - 8, kFooterY + 17, line, kMuted, 1);
}

// --- Lane Matrix -------------------------------------------------------------
// cells 0..15: contents (cell / 2) x (cell % 2); left column is the pair's first
// voice, right column the second, matching the physical step-pad banks.
template <class Canvas>
void renderMatrixCell(Canvas &canvas, const Model &model, uint8_t cell)
{
    const Rect rect = matrixCellRect(cell);
    // Clearing the whole cell (not just the plot) makes every cell separately
    // redrawable, which is what lets the firmware ship 8-row strips.
    canvas.fillRect(rect.x, rect.y, rect.w, rect.h, kBackground);
    canvas.fillRect(rect.x, rect.y, rect.w, 1, kPanelEdge);

    const uint8_t row = static_cast<uint8_t>(cell / 2);
    const uint8_t first = voicePairFirst(model.selectedVoice);
    const uint8_t voice = static_cast<uint8_t>(first + (cell % 2));
    const ParamId laneId = bankLane(model.laneBank, row);
    const Lane &lane = model.lanes[voice][static_cast<uint8_t>(laneId)];
    const uint16_t accent = voiceAccent(voice);

    canvas.fillRect(rect.x, rect.y, 2, rect.h, accent);
    const char *name = laneLabel(model, voice, laneId);
    canvas.text(rect.x + 6, rect.y + 4, name, accent);
    // The value is right-aligned into whatever the name leaves free.
    int nameWidth = 0;
    while (name[nameWidth] != '\0' && nameWidth < 20) ++nameWidth;
    char fitted[24];
    fitText(lane.valueText, fitted, sizeof(fitted), rect.w - 16 - nameWidth * 6);
    if (fitted[0] != '\0')
        textRight(canvas, rect.x + rect.w - 4, rect.y + 4, fitted,
                  model.style == Style::Ticks ? kInk : kMuted);
    // Loop length, so a polymetric lane's own period is readable at a glance.
    if (laneLength(lane) != SequencerConstants::DEFAULT_STEPS_COUNT || laneStart(lane) != 0)
    {
        char badge[16];
        std::snprintf(badge, sizeof(badge), "%u/%u", static_cast<unsigned>(laneLength(lane)),
                      static_cast<unsigned>(laneStart(lane)));
        canvas.text(rect.x + 6, rect.y + kMatrixCellHeight - 12, badge, kMuted, 1);
    }
    drawLanePlot(canvas, lane, Rect{static_cast<int16_t>(rect.x + 6),
                                    static_cast<int16_t>(rect.y + 16),
                                    static_cast<int16_t>(rect.w - 12),
                                    static_cast<int16_t>(rect.h - 28)},
                 accent, model.style);
}

// --- Loop Observatory --------------------------------------------------------
// Sections 0..3: one voice per section, every lane aligned on the SAME absolute
// step axis, so "what changes together" is a straight vertical read.
// Sections 4..7: one voice per section, loop windows only, so polymetric phase
// (and the drift between lanes) is a horizontal read.
template <class Canvas>
void renderObservatorySection(Canvas &canvas, const Model &model, uint8_t section)
{
    const uint8_t index = section < kObservatorySections ? section : kObservatorySections - 1;
    const Rect rect = observatorySectionRect(index);
    canvas.fillRect(rect.x, rect.y, rect.w, rect.h, kBackground);
    canvas.fillRect(rect.x, rect.y, rect.w, 1, kPanelEdge);

    const uint8_t voice = index < kObservatoryCompareCount
                              ? index
                              : static_cast<uint8_t>(index - kObservatoryCompareCount);
    const uint16_t accent = voiceAccent(voice);

    // The axis spans the longest lane shown, so lengths stay comparable.
    int widest = SequencerConstants::MIN_STEPS_COUNT;
    for (uint8_t row = 0; row < kLanesPerBank; ++row)
    {
        const int length = laneLength(model.lanes[voice][static_cast<uint8_t>(bankLane(model.laneBank, row))]);
        if (length > widest) widest = length;
    }
    // Wide enough for the longest lane name (GATELENGTH) plus its gutter.
    const int axisX = 68;
    const int axisW = kWidth - axisX - 8;
    const auto axisStep = [&](uint8_t step) {
        return axisX + (step * (axisW - 1)) / (widest - 1 < 1 ? 1 : widest - 1);
    };

    if (index < kObservatoryCompareCount)
    {
        canvas.fillRect(rect.x, rect.y, 3, rect.h, accent);
        char title[40];
        std::snprintf(title, sizeof(title), "V%u %.11s", static_cast<unsigned>(voice) + 1,
                      model.voiceNames[voice][0] ? model.voiceNames[voice] : "-");
        canvas.text(8, rect.y + 4, title, accent);
        canvas.text(96, rect.y + 4, "ALIGNED ON STEP", kMuted);
        char span[32];
        std::snprintf(span, sizeof(span), "%s  %d ST", microFormName(microForm(model.style)), widest);
        textRight(canvas, kWidth - 8, rect.y + 4, span, kMuted);

        const MicroForm form = microForm(model.style);
        for (uint8_t row = 0; row < kLanesPerBank; ++row)
        {
            const ParamId laneId = bankLane(model.laneBank, row);
            const Lane &lane = model.lanes[voice][static_cast<uint8_t>(laneId)];
            const uint8_t start = laneStart(lane);
            const int rowY = rect.y + 16 + row * 6;
            char brief[12];
            fitText(laneLabel(model, voice, laneId), brief, sizeof(brief), axisX - 10);
            canvas.text(6, rowY - 2, brief, kMuted);
            canvas.fillRect(axisX, rowY + 3, axisW, 1, kPanelEdge);
            drawMicroLane(canvas, lane, rowY, accent, form, axisStep);
            // Loop extent, per lane, never shared.
            if (start > 0)
                canvas.fillRect(axisStep(start), rowY, axisStep(static_cast<uint8_t>(widest - 1)) -
                                                        axisStep(start) + 1, 1, kLoopExtent);
        }
    }
    else
    {
        canvas.fillRect(rect.x, rect.y, 3, rect.h, accent);
        char title[40];
        std::snprintf(title, sizeof(title), "V%u PHASE", static_cast<unsigned>(voice) + 1);
        canvas.text(8, rect.y + 3, title, accent);
        canvas.text(80, rect.y + 3, "LOOP WINDOWS / ROWS FOLLOW THE BANK", kMuted);
        // Eight 2px rows with no gaps: every lane's window stays visible in 32px.
        for (uint8_t row = 0; row < kLanesPerBank; ++row)
        {
            const Lane &lane = model.lanes[voice][static_cast<uint8_t>(bankLane(model.laneBank, row))];
            const int rowY = rect.y + 14 + row * 2;
            canvas.fillRect(axisX, rowY, axisW, 2, kPanelEdge);
            const int from = axisStep(laneStart(lane));
            const int to = axisStep(static_cast<uint8_t>(laneLength(lane) - 1));
            canvas.fillRect(from, rowY, to - from + 1, 2, scale565(accent, 150, 255));
            canvas.fillRect(axisStep(laneCursor(lane)), rowY, 2, 2, kCursor);
        }
    }
}

// --- Whole pages -------------------------------------------------------------
template <class Canvas>
void render(Canvas &canvas, const Model &model)
{
    canvas.fillRect(0, 0, kWidth, kHeight, kBackground);
    renderHeader(canvas, model);
    if (model.page == Page::Matrix)
    {
        for (uint8_t cell = 0; cell < kMatrixRows * 2; ++cell)
            renderMatrixCell(canvas, model, cell);
    }
    else if (model.page == Page::Observatory)
    {
        for (uint8_t section = 0; section < kObservatorySections; ++section)
            renderObservatorySection(canvas, model, section);
    }
    renderFooter(canvas, model);
}
} // namespace LaneDisplay
