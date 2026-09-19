#ifndef OLED_VIEW_H
#define OLED_VIEW_H

#include "UIState.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../voice/VoiceConfig.h"

/**
 * OledView — the pure routing decisions of the OLED state machine.
 *
 * Page selection, parameter-target selection, and value-source selection
 * live here so host tests can drive the real production logic; oled.cpp only
 * renders a Route and keeps the pixel drawing and I2C transfer.
 *
 * route() is read-only against every input: display evaluation must not
 * mutate tracks, advance cursors, publish control updates, or retrigger
 * notes. All values come from the same authoritative sources editing and
 * playback use (stored/composed steps, patch bases) — never a sensor
 * prediction of a write that may have been rejected.
 */
namespace OledView
{

enum class Page : uint8_t
{
    VoiceEditor,   // modal editor pages
    ModeBanner,    // transient PARAM/UTIL strap banner
    Notice,        // transient confirmation notice
    Parameter,     // a parameter page (payload below)
    SettingsMenu,  // settings preset/parameter pages
    GateLength,    // gate sequence-length editing
    StepPlaceholder, // selected step with nothing targeted
    EncoderDefault // resting screen: the encoder target's value
};

struct Route
{
    Page page = Page::EncoderDefault;

    // --- Payload for Page::Parameter ---
    ParamId param = ParamId::Count; // lane shown (Count never on this page)
    bool base = false;              // BASE label: the patch base, not step data
    bool stepSelected = false;      // a step is selected for edit
    uint8_t step = 0;               // step index to label
    bool showDistance = false;      // sensor status readout (focused pages)

    // --- Values (Page::Parameter and Page::EncoderDefault) ---
    // Composed snapshot from the same source editing/playback use; for base
    // pages this is MusicalValues::baseStep of the live config.
    Step values{};
    // True while the encoder-base view window is open (EncoderDefault).
    bool showBase = false;
};

/**
 * Decide what the display renders this frame.
 *
 * @param encoderLane ParamId whose base the performance encoder currently
 *                    targets (VoiceEdit::sequenceLane of the encoder target);
 *                    ParamId::Count for voice-only targets (Slide Time).
 * @param config      Live VoiceConfig of the selected voice (may be null).
 */
Route route(const UIState &ui, const Sequencer &seq, const VoiceConfig *config,
            unsigned long nowMs, ParamId encoderLane);

} // namespace OledView

#endif // OLED_VIEW_H
