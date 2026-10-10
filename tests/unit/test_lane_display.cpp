#include "display/LaneDisplay.h"
#include <catch2/catch_test_macros.hpp>
#include <string>

namespace
{
// Records every drawing call and proves nothing lands outside the panel. This is
// the same contract the firmware strip canvas and the demo canvas implement.
struct RecordingCanvas
{
    static constexpr int kWidth = LaneDisplay::kWidth;
    static constexpr int kHeight = LaneDisplay::kHeight;

    int drawn = 0;
    int outside = 0;
    int cursorPixels = 0;
    int minY = kHeight;
    int maxY = -1;
    int cursorXMin = kWidth;
    int cursorXMax = -1;

    bool covers(int x, int y) const { return x >= 0 && x < kWidth && y >= 0 && y < kHeight; }
    void note(int x, int y, int w, int h, uint16_t colour)
    {
        ++drawn;
        if (colour == LaneDisplay::kCursor) {
            ++cursorPixels;
            if (x < cursorXMin) cursorXMin = x;
            if (x > cursorXMax) cursorXMax = x;
        }
        for (int yy = y; yy < y + h; ++yy)
            for (int xx = x; xx < x + w; ++xx)
                if (!covers(xx, yy)) ++outside;
        if (y >= 0 && y < minY) minY = y;
        if (y + h - 1 > maxY) maxY = y + h - 1;
    }
    void fillRect(int x, int y, int w, int h, uint16_t colour) { note(x, y, w, h, colour); }
    void drawRect(int x, int y, int w, int h, uint16_t colour) { note(x, y, w, h, colour); }
    void drawLine(int x0, int y0, int x1, int y1, uint16_t colour)
    {
        note(x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1,
             (x0 < x1 ? x1 - x0 : x0 - x1) + 1, (y0 < y1 ? y1 - y0 : y0 - y1) + 1, colour);
    }
    void fillCircle(int cx, int cy, int r, uint16_t colour)
    {
        note(cx - r, cy - r, 2 * r + 1, 2 * r + 1, colour);
    }
    void drawCircle(int cx, int cy, int r, uint16_t colour)
    {
        note(cx - r, cy - r, 2 * r + 1, 2 * r + 1, colour);
    }
    void text(int x, int y, const char *value, uint16_t colour, uint8_t size = 1)
    {
        int length = 0;
        while (value[length] != '\0' && length < 63) ++length;
        note(x, y, length * 6 * size, 8 * size, colour);
    }
};

void fillLane(LaneDisplay::Lane &lane, uint8_t count, uint8_t start, uint8_t cursor,
              uint8_t seed, const char *label = "", const char *value = "")
{
    lane.count = count;
    lane.start = start;
    lane.cursor = cursor;
    for (uint8_t step = 0; step < count; ++step)
        lane.values[step] = static_cast<uint8_t>((step * 37 + seed * 53) % 256);
    uint8_t index = 0;
    while (label[index] != '\0' && index < sizeof(lane.label) - 1) {
        lane.label[index] = label[index];
        ++index;
    }
    lane.label[index] = '\0';
    index = 0;
    while (value[index] != '\0' && index < sizeof(lane.valueText) - 1) {
        lane.valueText[index] = value[index];
        ++index;
    }
    lane.valueText[index] = '\0';
}

LaneDisplay::Model populatedModel()
{
    LaneDisplay::Model model;
    const char *names[4] = {"ANALOG", "DIGITAL", "BASSLINE", "PERC"};
    for (uint8_t voice = 0; voice < 4; ++voice) {
        uint8_t index = 0;
        while (names[voice][index] != '\0' && index < sizeof(model.voiceNames[0]) - 1) {
            model.voiceNames[voice][index] = names[voice][index];
            ++index;
        }
        for (uint8_t lane = 0; lane < PARAM_ID_COUNT; ++lane)
            fillLane(model.lanes[voice][lane], SequencerConstants::DEFAULT_STEPS_COUNT, 0, 0,
                     static_cast<uint8_t>(voice * 5 + lane), "LANE", "0.50x");
    }
    return model;
}
} // namespace

TEST_CASE("Display pages and styles cycle through every option and wrap", "[display][lane_display]")
{
    using LaneDisplay::Page;
    using LaneDisplay::Style;
    Page page = Page::Focus;
    for (int i = 0; i < static_cast<int>(Page::Count); ++i)
        page = LaneDisplay::cyclePage(page);
    CHECK(page == Page::Focus);
    CHECK(static_cast<int>(Page::Count) == 3);

    Style style = Style::Trace;
    for (int i = 0; i < static_cast<int>(Style::Count); ++i)
        style = LaneDisplay::cycleStyle(style);
    CHECK(style == Style::Trace);
    CHECK(static_cast<int>(Style::Count) >= 8);
    for (int i = 0; i < static_cast<int>(Style::Count); ++i)
        CHECK(std::string(LaneDisplay::styleName(static_cast<Style>(i))) != "?");
}

TEST_CASE("Both lane banks together expose every real lane", "[display][lane_display]")
{
    bool seen[PARAM_ID_COUNT] = {};
    for (uint8_t bank = 0; bank < LaneDisplay::kLaneBankCount; ++bank)
        for (uint8_t row = 0; row < LaneDisplay::kLanesPerBank; ++row)
            seen[static_cast<uint8_t>(LaneDisplay::bankLane(bank, row))] = true;
    for (uint8_t lane = 0; lane < PARAM_ID_COUNT; ++lane)
        CHECK(seen[lane]);
    // Bank index wraps instead of running off the table.
    CHECK(LaneDisplay::bankLane(2, 0) == LaneDisplay::bankLane(0, 0));
    CHECK(LaneDisplay::bankLane(255, 7) == LaneDisplay::bankLane(1, 7));
}

TEST_CASE("Display geometry tiles the body without overlap", "[display][lane_display]")
{
    CHECK(LaneDisplay::kHeaderHeight + 8 * LaneDisplay::kMatrixCellHeight == LaneDisplay::kFooterY);
    for (uint8_t cell = 0; cell < 16; ++cell) {
        const auto rect = LaneDisplay::matrixCellRect(cell);
        CHECK(rect.x >= 0);
        CHECK(rect.x + rect.w <= LaneDisplay::kWidth);
        CHECK(rect.y >= LaneDisplay::kHeaderHeight);
        CHECK(rect.y + rect.h <= LaneDisplay::kFooterY);
        if (cell % 2 == 0) CHECK(rect.x == 0);
        else CHECK(rect.x == LaneDisplay::kMatrixCellWidth);
    }
    // Out-of-range cells clamp onto the last one instead of drawing off-panel.
    CHECK(LaneDisplay::matrixCellRect(200).y == LaneDisplay::matrixCellRect(15).y);

    int covered = 0;
    for (uint8_t section = 0; section < LaneDisplay::kObservatorySections; ++section) {
        const auto rect = LaneDisplay::observatorySectionRect(section);
        CHECK(rect.x == 0);
        CHECK(rect.y == LaneDisplay::kHeaderHeight + covered);
        covered += rect.h;
    }
    CHECK(covered == LaneDisplay::kFooterY - LaneDisplay::kHeaderHeight);
    CHECK(LaneDisplay::observatorySectionRect(99).y ==
          LaneDisplay::observatorySectionRect(LaneDisplay::kObservatorySections - 1).y);
}

TEST_CASE("Every page and style stays inside the panel", "[display][lane_display]")
{
    for (uint8_t page = 0; page < static_cast<uint8_t>(LaneDisplay::Page::Count); ++page) {
        for (uint8_t style = 0; style < static_cast<uint8_t>(LaneDisplay::Style::Count); ++style) {
            for (uint8_t bank = 0; bank < LaneDisplay::kLaneBankCount; ++bank) {
                RecordingCanvas canvas;
                auto model = populatedModel();
                model.page = static_cast<LaneDisplay::Page>(page);
                model.style = static_cast<LaneDisplay::Style>(style);
                model.laneBank = bank;
                LaneDisplay::render(canvas, model);
                CHECK(canvas.drawn > 0);
                CHECK(canvas.outside == 0);
                CHECK(canvas.minY == 0);
                CHECK(canvas.maxY == LaneDisplay::kHeight - 1);
            }
        }
    }
}

TEST_CASE("Every cell and section is independently redrawable", "[display][lane_display]")
{
    // The firmware ships 8-row strips, so a single cell/section must be usable on
    // its own without the rest of the page having been drawn first.
    auto model = populatedModel();
    for (uint8_t style = 0; style < static_cast<uint8_t>(LaneDisplay::Style::Count); ++style) {
        model.style = static_cast<LaneDisplay::Style>(style);
        for (uint8_t cell = 0; cell < 16; ++cell) {
            RecordingCanvas canvas;
            LaneDisplay::renderMatrixCell(canvas, model, cell);
            CHECK(canvas.outside == 0);
            const auto rect = LaneDisplay::matrixCellRect(cell);
            CHECK(canvas.minY >= rect.y);
            CHECK(canvas.maxY <= rect.y + rect.h - 1);
            CHECK(canvas.drawn > 20);
        }
        for (uint8_t section = 0; section < LaneDisplay::kObservatorySections; ++section) {
            RecordingCanvas canvas;
            LaneDisplay::renderObservatorySection(canvas, model, section);
            CHECK(canvas.outside == 0);
            const auto rect = LaneDisplay::observatorySectionRect(section);
            CHECK(canvas.minY >= rect.y);
            CHECK(canvas.maxY <= rect.y + rect.h - 1);
        }
    }
}

TEST_CASE("Lane lengths, loop starts and cursors clamp safely", "[display][lane_display]")
{
    LaneDisplay::Lane lane;
    lane.count = 0;
    CHECK(LaneDisplay::laneLength(lane) == SequencerConstants::MIN_STEPS_COUNT);
    lane.count = 200;
    CHECK(LaneDisplay::laneLength(lane) == LaneDisplay::kLaneCount);
    lane.count = 5;
    lane.start = 4;
    CHECK(LaneDisplay::laneStart(lane) == 4);
    lane.start = 5;
    CHECK(LaneDisplay::laneStart(lane) == 0);
    lane.start = 250;
    CHECK(LaneDisplay::laneStart(lane) == 0);
    lane.start = 2;
    lane.cursor = 0;
    CHECK(LaneDisplay::laneCursor(lane) == 2);
    lane.cursor = 200;
    CHECK(LaneDisplay::laneCursor(lane) == 4);
    lane.cursor = 3;
    CHECK(LaneDisplay::laneCursor(lane) == 3);
    CHECK(LaneDisplay::stepColour(0, 3, LaneDisplay::kInk) == LaneDisplay::kExcluded);
    CHECK(LaneDisplay::stepColour(3, 3, LaneDisplay::kInk) == LaneDisplay::kInk);
}

TEST_CASE("Dense lanes keep every step inside the plot", "[display][lane_display]")
{
    // 2, 16 and 64 steps must all stay in the same plot: the point of the strip
    // renderer is that no lane silently loses steps to rounding.
    for (uint8_t count : {uint8_t{2}, uint8_t{16}, uint8_t{64}}) {
        auto model = populatedModel();
        fillLane(model.lanes[0][static_cast<uint8_t>(ParamId::Velocity)], count,
                 static_cast<uint8_t>(count / 2), static_cast<uint8_t>(count - 1), 7, "VELOCITY",
                 "0.75x");
        for (uint8_t style = 0; style < static_cast<uint8_t>(LaneDisplay::Style::Count); ++style) {
            model.style = static_cast<LaneDisplay::Style>(style);
            RecordingCanvas canvas;
            LaneDisplay::renderMatrixCell(canvas, model, 2); // row 1, left column = voice 1
            CHECK(canvas.outside == 0);
            CHECK(canvas.cursorPixels > 0);
        }
    }
}

TEST_CASE("Each lane draws its own cursor and loop extent", "[display][lane_display]")
{
    // Polymeter is the whole musical point: two lanes in one cell pair must not
    // share a play head or a loop window.
    auto model = populatedModel();
    fillLane(model.lanes[0][static_cast<uint8_t>(ParamId::Note)], 16, 0, 5, 1, "NOTE", "C3");
    fillLane(model.lanes[0][static_cast<uint8_t>(ParamId::Velocity)], 5, 2, 4, 2, "VELOCITY", "0.5x");
    for (uint8_t style = 0; style < static_cast<uint8_t>(LaneDisplay::Style::Count); ++style) {
        model.style = static_cast<LaneDisplay::Style>(style);
        RecordingCanvas noteCanvas;
        RecordingCanvas velocityCanvas;
        const auto rect = LaneDisplay::matrixCellRect(0);
        const LaneDisplay::Rect plot{static_cast<int16_t>(rect.x + 6), static_cast<int16_t>(rect.y + 16),
                                     static_cast<int16_t>(rect.w - 12), static_cast<int16_t>(rect.h - 28)};
        LaneDisplay::Lane probe = model.lanes[0][static_cast<uint8_t>(ParamId::Note)];
        probe.count = 16;
        LaneDisplay::drawLanePlot(noteCanvas, probe, plot, LaneDisplay::kInk,
                                  static_cast<LaneDisplay::Style>(style));
        probe = model.lanes[0][static_cast<uint8_t>(ParamId::Velocity)];
        probe.count = 5;
        LaneDisplay::drawLanePlot(velocityCanvas, probe, plot, LaneDisplay::kInk,
                                  static_cast<LaneDisplay::Style>(style));
        CHECK(noteCanvas.outside == 0);
        CHECK(velocityCanvas.outside == 0);
        CHECK(noteCanvas.cursorPixels > 0);
        CHECK(velocityCanvas.cursorPixels > 0);
        const int noteCursor = LaneDisplay::stepX(plot, 5, 16);
        const int velocityCursor = LaneDisplay::stepX(plot, 4, 5);
        CHECK(noteCursor != velocityCursor);
        if (style == static_cast<uint8_t>(LaneDisplay::Style::Orbit))
        {
            // Radar reads angle, so its play head is a radial line rather than a
            // vertical column: a distinct mark, asserted by the >0 checks above.
        }
        else
        {
            CHECK(noteCanvas.cursorXMin == noteCursor);
            CHECK(velocityCanvas.cursorXMin == velocityCursor);
        }
    }
}

TEST_CASE("Out-of-range selections cannot draw off-panel", "[display][lane_display]")
{
    auto model = populatedModel();
    model.selectedVoice = 250;
    model.laneBank = 250;
    model.style = static_cast<LaneDisplay::Style>(250);
    model.page = static_cast<LaneDisplay::Page>(250);
    RecordingCanvas canvas;
    LaneDisplay::render(canvas, model);
    CHECK(canvas.outside == 0);
    for (uint8_t cell = 0; cell < 16; ++cell) {
        RecordingCanvas cellCanvas;
        LaneDisplay::renderMatrixCell(cellCanvas, model, cell);
        CHECK(cellCanvas.outside == 0);
    }
}

TEST_CASE("Default model renders safely before any capture", "[display][lane_display]")
{
    // Boot state: no sequencer data captured yet. It must still be a valid screen.
    const LaneDisplay::Model model;
    RecordingCanvas canvas;
    LaneDisplay::render(canvas, model);
    CHECK(canvas.outside == 0);
    CHECK(canvas.drawn > 0);
}
