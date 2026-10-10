#pragma once

#include "DisplayConfig.h"
#include "../OLED/oled.h"

#if PICO2SEQ_DISPLAY == 1
#include "StripCanvas.h"
#include "LaneDisplay.h"
// Library discovery through the public umbrella; the panel uses hardware SPI,
// not the library's full-frame Arduino_Canvas or a DMA channel.
#include <Arduino_GFX_Library.h>
#endif

// Single control-core display owner. The legacy context renderer is shared,
// not reimplemented: OLED builds are unchanged; TFT builds keep every editor.
class PerformanceDisplay {
public:
    PerformanceDisplay();
    bool begin();
    void setVoiceManager(VoiceManager *manager) { context_.setVoiceManager(manager); }
    void onVoiceParameterChanged(uint8_t id, const VoiceState &state) {
        context_.onVoiceParameterChanged(id, state);
    }
    void onVoiceSwitched(const UIState &state, VoiceManager *manager) {
        context_.onVoiceSwitched(state, manager);
    }
    void update(const UIState &state, const SequencerView &sequencers, VoiceManager *manager);

private:
    OLEDDisplay context_;
#if PICO2SEQ_DISPLAY == 1
    Arduino_RPiPicoSPI bus_;
    Arduino_ST7796 panel_;
    StripCanvas canvas_;
    LaneDisplay::Model model_;
    uint8_t contextFrame_[1024] = {};
    // Dirty strip fingerprints save SPI traffic without another 300KB buffer.
    uint64_t stripHash_[60] = {};
    bool stripValid_[60] = {};
    bool initialized_ = false;
    uint8_t captureIndex_ = 0;
    uint8_t strip_ = 0;
    bool capturing_ = true;
    void captureMeta(const UIState &state);
    void renderStrip();
#endif
};
