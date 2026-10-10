#include "PerformanceDisplay.h"
#include "../app/HardwarePins.h"
#include "../app/SequencerView.h"
#include "../voice/VoiceManager.h"

#if PICO2SEQ_DISPLAY == 1
#include "DisplayModel.h"
#include "DisplayPolicy.h"
#include "FocusDisplay.h"
#include "../app/AppState.h"
#include "../app/UserPresetStorage.h"
#include <uClock.h>
#include <cstring>

namespace {
uint64_t fingerprint(const uint16_t *pixels) {
    uint64_t hash = 14695981039346656037ull;
    for (int i = 0; i < StripCanvas::kWidth * StripCanvas::kRows; ++i) {
        hash ^= pixels[i];
        hash *= 1099511628211ull;
    }
    return hash;
}
}
#endif

PerformanceDisplay::PerformanceDisplay()
#if PICO2SEQ_DISPLAY == 1
    // Arduino_RPiPicoSPI 1.6.8 unconditionally configures MISO. Passing -1
    // would call gpio_set_function(255), so reserve real GP16 even write-only.
    : bus_(PIN_TFT_DC, PIN_TFT_CS, PIN_TFT_SCK, PIN_TFT_MOSI, PIN_TFT_MISO, spi0),
      panel_(&bus_, PIN_TFT_RESET, DisplayConfig::kRotation, false)
#endif
{}

bool PerformanceDisplay::begin() {
#if PICO2SEQ_DISPLAY == 1
    static_assert(DisplayConfig::kRotation == 0 || DisplayConfig::kRotation == 2,
                  "The 320x480 layouts require portrait rotation 0 or 2");
    if (!context_.begin(false) || !panel_.begin(DisplayConfig::kSpiHz)) return false;
    panel_.invertDisplay(DisplayConfig::kInverted);
    // Startup only. Live drawing is eight-row strips, never fillScreen().
    panel_.fillScreen(0x0842);
    initialized_ = true;
    Serial.println("ST7796 SPI initialized (write-only; panel presence unverified)");
    return true;
#else
    return context_.begin();
#endif
}

void PerformanceDisplay::update(const UIState &state, const SequencerView &sequencers,
                                VoiceManager *manager) {
#if PICO2SEQ_DISPLAY == 1
    if (!initialized_) return;
    // A context/page/style/voice change preempts an old scan. Capture is bounded
    // to one lane (at most 64 steps) per pass; audio/control work runs between.
    const auto page = DisplayPolicy::effectivePage(state, millis());
    if (model_.page != page || model_.style != state.display.style ||
        model_.laneBank != state.display.laneBank ||
        model_.selectedVoice != std::min<uint8_t>(state.selectedVoiceIndex, 3)) {
        capturing_ = true;
        captureIndex_ = strip_ = 0;
    }
    if (capturing_) {
        if (captureIndex_ == 0) {
            captureMeta(state);
            if (model_.page == LaneDisplay::Page::Focus) {
                context_.update(state, sequencers, manager);
                std::memcpy(contextFrame_, context_.framebuffer(), sizeof(contextFrame_));
            }
        }
        const uint8_t voice = captureIndex_ / PARAM_ID_COUNT;
        const auto id = static_cast<ParamId>(captureIndex_ % PARAM_ID_COUNT);
        const auto *sequence = sequencers.get(voice);
        const auto *config = manager ? manager->getVoiceConfig(voiceSystem.getVoiceId(voice)) : nullptr;
        if (sequence)
            DisplayModel::captureLane(model_.lanes[voice][static_cast<uint8_t>(id)],
                                      *sequence, id, config, model_.tempo);
        if (++captureIndex_ == 4 * PARAM_ID_COUNT) {
            capturing_ = false;
            strip_ = 0;
        }
        return;
    }
    renderStrip();
    if (++strip_ == 60) {
        capturing_ = true;
        captureIndex_ = 0;
    }
#else
    context_.update(state, sequencers, manager);
#endif
}

#if PICO2SEQ_DISPLAY == 1
void PerformanceDisplay::captureMeta(const UIState &state) {
    model_.page = DisplayPolicy::effectivePage(state, millis());
    model_.style = state.display.style;
    model_.laneBank = state.display.laneBank;
    model_.selectedVoice = std::min<uint8_t>(state.selectedVoiceIndex, 3);
    model_.tempo = uClock.getTempo();
    model_.running = isClockRunning;
    for (uint8_t voice = 0; voice < 4; ++voice)
        std::snprintf(model_.voiceNames[voice], sizeof(model_.voiceNames[voice]), "%s",
                      UserPresetStorage::voiceLabel(state, voice));
}

void PerformanceDisplay::renderStrip() {
    using namespace LaneDisplay;
    const int y = strip_ * StripCanvas::kRows;
    canvas_.beginStrip(y, kBackground);
    // Geometry comes from the renderer, never from a second copy of the numbers.
    if (y < kHeaderHeight)
        renderHeader(canvas_, model_);
    if (y + StripCanvas::kRows > kFooterY)
        renderFooter(canvas_, model_);
    const Rect body{0, kHeaderHeight, kWidth, static_cast<int16_t>(kFooterY - kHeaderHeight)};
    const Rect visible{body.x, static_cast<int16_t>(y < body.y ? body.y : y), body.w,
                       static_cast<int16_t>((y + StripCanvas::kRows > body.y + body.h
                                                 ? body.y + body.h
                                                 : y + StripCanvas::kRows) -
                                            (y < body.y ? body.y : y))};
    if (visible.h <= 0) {
        // Header/footer only: nothing else to draw for this strip.
    } else if (model_.page == Page::Focus) {
        renderFocus(canvas_, model_, contextFrame_);
    } else if (model_.page == Page::Matrix) {
        // Two cells per row; only the rows this strip crosses are replayed.
        for (uint8_t cell = 0; cell < kMatrixRows * 2; ++cell) {
            const Rect cellRect = matrixCellRect(cell);
            if (visible.y < cellRect.y + cellRect.h && visible.y + visible.h > cellRect.y)
                renderMatrixCell(canvas_, model_, cell);
        }
    } else if (model_.page == Page::Observatory) {
        for (uint8_t section = 0; section < kObservatorySections; ++section) {
            const Rect sectionRect = observatorySectionRect(section);
            if (visible.y < sectionRect.y + sectionRect.h &&
                visible.y + visible.h > sectionRect.y)
                renderObservatorySection(canvas_, model_, section);
        }
    }
    const auto hash = fingerprint(canvas_.pixels());
    if (!stripValid_[strip_] || stripHash_[strip_] != hash) {
        // pixels() is intentionally non-const: the library's uint16_t* overload
        // batches through writePixels() (64 bytes per SPI write), while the
        // const overload writes one 16-bit word at a time and would multiply the
        // per-strip bus time. 320x8 RGB565 = 5120 bytes = ~1.7 ms at 24 MHz.
        panel_.draw16bitRGBBitmap(0, y, canvas_.pixels(), StripCanvas::kWidth, StripCanvas::kRows);
        stripHash_[strip_] = hash;
        stripValid_[strip_] = true;
    }
}
#endif
