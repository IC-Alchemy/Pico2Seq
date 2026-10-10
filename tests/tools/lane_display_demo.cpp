// lane_display_demo: renders the SVGs/PNGs in docs/display from the SAME
// portable renderer the firmware runs (src/display/LaneDisplay.h).
//
// Host-only: it implements the tiny Canvas contract over an RGB565 buffer and
// writes binary PPM, which scripts/render_display_demos.py converts to PNG and
// composites into sheets. Nothing here ships to the Pico.
//
// Build (already wired into tests/CMakeLists.txt as an opt-in target):
//   cmake --build build_test_ninja --target pico2seq_display_demo
// Run:
//   ./build_test_ninja/tests/pico2seq_display_demo <output-dir>
#include "display/FocusDisplay.h"
#include "display/LaneDisplay.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
using namespace LaneDisplay;

// --- A 5x7 host font ---------------------------------------------------------
// The firmware draws text with the panel's own bitmap font; preview images need
// glyphs to be readable, so the demo supplies its own. Uppercase and the digits
// cover every label the renderer emits with the demo data below.
constexpr uint8_t kFont[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x40, 0x20, 0x10, 0x08, 0x04}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x41, 0x51, 0x32}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x03, 0x04, 0x78, 0x04, 0x03}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
};

const uint8_t *glyphFor(char c)
{
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c >= '0' && c <= '9') return kFont[12 + (c - '0')];
    if (c >= 'A' && c <= 'Z') return kFont[23 + (c - 'A')];
    switch (c)
    {
    case '!': return kFont[1];
    case '#': return kFont[2];
    case '%': return kFont[3];
    case '(': return kFont[4];
    case ')': return kFont[5];
    case '+': return kFont[6];
    case ',': return kFont[7];
    case '-': return kFont[8];
    case '.': return kFont[9];
    case '/': return kFont[10];
    case ':': return kFont[11];
    default: return kFont[0];
    }
}

// --- Canvas -----------------------------------------------------------------
class PpmCanvas
{
public:
    PpmCanvas() : pixels_(static_cast<size_t>(kWidth) * kHeight, kBackground) {}

    void fillRect(int x, int y, int w, int h, uint16_t colour)
    {
        for (int row = y; row < y + h; ++row)
            for (int column = x; column < x + w; ++column) set(column, row, colour);
    }
    void drawRect(int x, int y, int w, int h, uint16_t colour)
    {
        fillRect(x, y, w, 1, colour);
        fillRect(x, y + h - 1, w, 1, colour);
        fillRect(x, y, 1, h, colour);
        fillRect(x + w - 1, y, 1, h, colour);
    }
    // Bresenham, the same shape the firmware's graphics library draws.
    void drawLine(int x0, int y0, int x1, int y1, uint16_t colour)
    {
        const int dx = x1 - x0 >= 0 ? x1 - x0 : x0 - x1;
        const int dy = y1 - y0 >= 0 ? y0 - y1 : y1 - y0;
        int error = dx + dy;
        for (;;)
        {
            set(x0, y0, colour);
            if (x0 == x1 && y0 == y1) break;
            const int doubled = 2 * error;
            if (doubled >= dy) { error += dy; x0 += x0 < x1 ? 1 : -1; }
            if (doubled <= dx) { error += dx; y0 += y0 < y1 ? 1 : -1; }
        }
    }
    void fillCircle(int cx, int cy, int r, uint16_t colour)
    {
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x)
                if (x * x + y * y <= r * r) set(cx + x, cy + y, colour);
    }
    void drawCircle(int cx, int cy, int r, uint16_t colour)
    {
        int x = r, y = 0, error = 1 - r;
        while (x >= y)
        {
            const int xs[8] = {x, y, -y, -x, -x, -y, y, x};
            const int ys[8] = {y, x, x, y, -y, -x, -x, -y};
            for (int i = 0; i < 8; ++i) set(cx + xs[i], cy + ys[i], colour);
            ++y;
            if (error < 0) error += 2 * y + 1;
            else { --x; error += 2 * (y - x) + 1; }
        }
    }
    void text(int x, int y, const char *value, uint16_t colour, uint8_t size = 1)
    {
        for (const char *c = value; *c != '\0'; ++c, x += 6 * size)
        {
            const uint8_t *columns = glyphFor(*c);
            for (int column = 0; column < 5; ++column)
                for (int row = 0; row < 8; ++row)
                    if ((columns[column] >> row) & 1)
                        fillRect(x + column * size, y + row * size, size, size, colour);
        }
    }

    // Host-side annotation for the layout map: a line plus its own label.
    void annotate(int x, int y, int w, int h, uint16_t colour) { drawRect(x, y, w, h, colour); }

    bool writePpm(const std::string &path) const
    {
        FILE *file = std::fopen(path.c_str(), "wb");
        if (!file) return false;
        std::fprintf(file, "P6\n%d %d\n255\n", kWidth, kHeight);
        for (size_t i = 0; i < pixels_.size(); ++i)
        {
            const uint16_t value = pixels_[i];
            const uint8_t rgb[3] = {static_cast<uint8_t>(((value >> 11) & 0x1F) * 255 / 31),
                                    static_cast<uint8_t>(((value >> 5) & 0x3F) * 255 / 63),
                                    static_cast<uint8_t>((value & 0x1F) * 255 / 31)};
            std::fwrite(rgb, 1, 3, file);
        }
        std::fclose(file);
        return true;
    }

private:
    void set(int x, int y, uint16_t colour)
    {
        if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
        pixels_[static_cast<size_t>(y) * kWidth + x] = colour;
    }

    std::vector<uint16_t> pixels_;
};

// --- Deterministic, musically plausible demo data ----------------------------
void setLabel(LaneDisplay::Lane &lane, const char *text)
{
    std::snprintf(lane.label, sizeof(lane.label), "%s", text);
}

void setValue(LaneDisplay::Lane &lane, const char *text)
{
    std::snprintf(lane.valueText, sizeof(lane.valueText), "%s", text);
}

enum class Shape { Ramp, Arch, Gates, Sparse, Steps, Drift };

void build(LaneDisplay::Lane &lane, uint8_t count, uint8_t start, uint8_t cursor, Shape shape)
{
    lane.count = count;
    lane.start = start;
    lane.cursor = cursor;
    uint32_t seed = 0x1234u + count * 7u + start * 13u;
    for (uint8_t step = 0; step < count; ++step)
    {
        seed = seed * 1103515245u + 12345u;
        const uint8_t noise = static_cast<uint8_t>((seed >> 16) & 0x1F);
        int level = 128;
        switch (shape)
        {
        case Shape::Ramp: level = 20 + (step * 235) / (count - 1); break;
        case Shape::Arch: level = 20 + (step * (count - 1 - step) * 940) / ((count - 1) * (count - 1)); break;
        case Shape::Gates: level = (step % 4 == 0) ? 235 : (step % 2 == 0 ? 150 : 40); break;
        case Shape::Sparse: level = (step % 3 == 1) ? 200 : 60; break;
        case Shape::Steps: level = 40 + ((step / 2) % 4) * 60; break;
        case Shape::Drift: level = 90 + ((step * 37) % 130); break;
        }
        level += noise - 16;
        lane.values[step] = static_cast<uint8_t>(level < 0 ? 0 : (level > 255 ? 255 : level));
    }
}

void buildVoice(LaneDisplay::Model &model, uint8_t voice, const char *name,
                const uint8_t counts[PARAM_ID_COUNT], const uint8_t starts[PARAM_ID_COUNT],
                const Shape shapes[PARAM_ID_COUNT], const char *values[PARAM_ID_COUNT])
{
    std::snprintf(model.voiceNames[voice], sizeof(model.voiceNames[voice]), "%s", name);
    for (uint8_t lane = 0; lane < PARAM_ID_COUNT; ++lane)
    {
        const ParamId id = static_cast<ParamId>(lane);
        LaneDisplay::Lane &target = model.lanes[voice][lane];
        const uint8_t count = counts[lane];
        const uint8_t start = starts[lane] < count ? starts[lane] : 0;
        const uint8_t cursor = static_cast<uint8_t>(start + ((count - start) * 3) / 4 - 1);
        build(target, count, start, cursor, shapes[lane]);
        const auto *definition = parameterDefinition(id);
        setLabel(target, definition ? definition->name : "LANE");
        setValue(target, values[lane]);
    }
}

LaneDisplay::Model demoModel(LaneDisplay::Style style, LaneDisplay::Page page, uint8_t bank,
                             uint8_t selectedVoice)
{
    LaneDisplay::Model model;
    model.style = style;
    model.page = page;
    model.laneBank = bank;
    model.selectedVoice = selectedVoice;
    model.tempo = 112.0f;
    model.running = true;

    // Polymetric on purpose: the different lengths and loop starts are what the
    // new screens exist to show.
    const uint8_t counts1[PARAM_ID_COUNT] = {16, 16, 8, 16, 12, 8, 16, 16, 16, 8, 12};
    const uint8_t starts1[PARAM_ID_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const Shape shapes1[PARAM_ID_COUNT] = {Shape::Steps, Shape::Arch, Shape::Ramp, Shape::Sparse,
                                           Shape::Arch, Shape::Steps, Shape::Ramp, Shape::Gates,
                                           Shape::Sparse, Shape::Ramp, Shape::Arch};
    const char *values1[PARAM_ID_COUNT] = {"C3", "0.72x", "820Hz", "12ms", "1.4s",
                                          "0 oct", "180ms", "On", "Off", "0.55", "420ms"};
    buildVoice(model, 0, "ANALOG", counts1, starts1, shapes1, values1);

    const uint8_t counts2[PARAM_ID_COUNT] = {12, 6, 16, 12, 16, 16, 8, 16, 12, 16, 16};
    const uint8_t starts2[PARAM_ID_COUNT] = {2, 0, 4, 0, 3, 0, 0, 0, 0, 0, 0};
    const Shape shapes2[PARAM_ID_COUNT] = {Shape::Arch, Shape::Sparse, Shape::Drift, Shape::Steps,
                                           Shape::Ramp, Shape::Drift, Shape::Gates, Shape::Gates,
                                           Shape::Sparse, Shape::Arch, Shape::Ramp};
    const char *values2[PARAM_ID_COUNT] = {"G4", "0.48x", "2.1kHz", "3ms", "780ms",
                                          "+1 oct", "95ms", "On", "Off", "0.30", "260ms"};
    buildVoice(model, 1, "DIGITAL", counts2, starts2, shapes2, values2);

    const uint8_t counts3[PARAM_ID_COUNT] = {8, 16, 16, 8, 16, 12, 16, 16, 8, 16, 8};
    const uint8_t starts3[PARAM_ID_COUNT] = {1, 0, 0, 0, 6, 0, 0, 0, 0, 4, 0};
    const Shape shapes3[PARAM_ID_COUNT] = {Shape::Sparse, Shape::Gates, Shape::Arch, Shape::Ramp,
                                           Shape::Drift, Shape::Steps, Shape::Arch, Shape::Gates,
                                           Shape::Gates, Shape::Sparse, Shape::Ramp};
    const char *values3[PARAM_ID_COUNT] = {"C1", "0.88x", "480Hz", "2ms", "320ms",
                                          "-1 oct", "240ms", "On", "On", "0.70", "120ms"};
    buildVoice(model, 2, "BASSLINE", counts3, starts3, shapes3, values3);

    const uint8_t counts4[PARAM_ID_COUNT] = {16, 8, 12, 16, 16, 16, 12, 16, 16, 12, 16};
    const uint8_t starts4[PARAM_ID_COUNT] = {0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 5};
    const Shape shapes4[PARAM_ID_COUNT] = {Shape::Gates, Shape::Ramp, Shape::Sparse, Shape::Arch,
                                           Shape::Steps, Shape::Gates, Shape::Drift, Shape::Sparse,
                                           Shape::Ramp, Shape::Gates, Shape::Arch};
    const char *values4[PARAM_ID_COUNT] = {"A2", "1.00x", "3.4kHz", "1ms", "180ms",
                                          "+2 oct", "60ms", "Rest", "Off", "0.20", "90ms"};
    buildVoice(model, 3, "PERC", counts4, starts4, shapes4, values4);
    return model;
}

// Focus keeps the existing contextual page; the demo draws a schematic of it so
// the new frame and the four gate timelines around it are reviewable.
void drawFocusSchematic(PpmCanvas &canvas)
{
    constexpr uint16_t ink = kInk;
    constexpr uint16_t muted = kMuted;
    canvas.text(40, 126, "V2 DIGITAL   LUSTRE", ink, 2);
    canvas.text(40, 150, "PARAM", muted, 2);
    canvas.text(112, 150, "620HZ", ink, 2);
    canvas.fillRect(40, 176, 232, 10, kPanelEdge);
    canvas.fillRect(40, 176, 148, 10, kVoiceAccent[1]);
    canvas.text(40, 196, "STEP 07/16  FILTER 820HZ", muted, 1);
    canvas.text(40, 210, "SAVE OK  -  PRESET BROWSER LIVE", muted, 1);
    canvas.text(40, 228, "SH1106 CONTEXT PAGE, SHOWN 2X", muted, 1);
}
} // namespace

int main(int argc, char **argv)
{
    const std::string directory = argc > 1 ? argv[1] : "docs/display";
    int written = 0;
    const auto write = [&](const std::string &name, const LaneDisplay::Model &model,
                           bool focus) {
        PpmCanvas canvas;
        if (focus)
        {
            LaneDisplay::renderHeader(canvas, model);
            LaneDisplay::renderFocus(canvas, model, nullptr);
            drawFocusSchematic(canvas);
            LaneDisplay::renderFooter(canvas, model);
        }
        else
        {
            LaneDisplay::render(canvas, model);
        }
        const std::string path = directory + "/" + name + ".ppm";
        if (canvas.writePpm(path)) { ++written; std::printf("wrote %s\n", path.c_str()); }
        else std::printf("FAILED %s\n", path.c_str());
    };

    for (uint8_t style = 0; style < static_cast<uint8_t>(LaneDisplay::Style::Count); ++style)
    {
        const auto s = static_cast<LaneDisplay::Style>(style);
        write(std::string("matrix-bank1-") + LaneDisplay::styleName(s),
              demoModel(s, LaneDisplay::Page::Matrix, 0, 0), false);
    }
    write("matrix-bank2-trace", demoModel(LaneDisplay::Style::Trace, LaneDisplay::Page::Matrix, 1, 2), false);
    write("matrix-bank2-heat", demoModel(LaneDisplay::Style::Heat, LaneDisplay::Page::Matrix, 1, 2), false);

    // One image per Observatory micro form, plus the radar style: these are the
    // three bodies the style selector actually changes on this page.
    for (LaneDisplay::Style s : {LaneDisplay::Style::Trace, LaneDisplay::Style::Bars,
                                 LaneDisplay::Style::Heat, LaneDisplay::Style::Orbit})
        write(std::string("observatory-") + LaneDisplay::styleName(s),
              demoModel(s, LaneDisplay::Page::Observatory, 0, 0), false);
    write("observatory-bank2-trace",
          demoModel(LaneDisplay::Style::Trace, LaneDisplay::Page::Observatory, 1, 2), false);

    write("focus", demoModel(LaneDisplay::Style::Trace, LaneDisplay::Page::Focus, 0, 1), true);
    std::printf("%d screens rendered into %s\n", written, directory.c_str());
    return written > 0 ? 0 : 1;
}
