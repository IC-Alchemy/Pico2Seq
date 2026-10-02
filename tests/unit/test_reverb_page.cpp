#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ui/ControlSurfaceLogic.h"
#include "ui/ReverbPageControls.h"
#include "ui/UITransitions.h"
#include "voice/ReverbSettings.h"

#include <cmath>
#include <cstring>
#include <limits>

// The live Reverb page: its chord and gestures, the fader curves and the text the
// OLED shows. Hardware glue (tiles, OLED, LEDs) is bench-checked; these are the
// decisions it makes.

using ControlSurface::ReverbControl;
using Catch::Approx;

namespace
{
constexpr uint8_t kShift = 1u << 7;
constexpr uint8_t kSix = 1u << 5;
constexpr uint8_t kTwo = 1u << 1;
constexpr uint8_t kOne = 1u << 0;

constexpr ReverbControl kAll[] = {ReverbControl::Mix, ReverbControl::Decay, ReverbControl::Damping,
                                  ReverbControl::LowCut, ReverbControl::Diffusion,
                                  ReverbControl::ModDepth, ReverbControl::Width};

// What AlchemyControlBridge::update() does with the two chord owners, in its order:
// the reverb page decides first, then the ADSR chord. Returns true when the reverb
// page owned the pass.
bool bridgePass(UIState &ui, uint8_t buttons, uint8_t voices, VoiceEnvelope::Input *adsr = nullptr)
{
    const bool canOpen = !ui.voiceEnvelope.active && !ui.voiceEnvelope.waitRelease && !ui.gateSeqLengthMode;
    const auto reverb = ui.reverbPage.poll(buttons, voices, canOpen);
    if (reverb.consumed)
    {
        if (reverb.open) UITransitions::openReverbPage(ui);
        if (reverb.exit) UITransitions::closeReverbPage(ui);
        return true;
    }
    const auto envelope = ui.voiceEnvelope.poll(buttons, voices);
    if (adsr) *adsr = envelope;
    return false;
}
} // namespace

TEST_CASE("Reverb page opens with Shift, then 6, then a press of 2 — and only that", "[reverb_page][control_surface]")
{
    ReverbPage::Controls page;
    CHECK_FALSE(page.poll(kShift, 0, true).consumed);
    CHECK_FALSE(page.poll(kShift | kSix, 0, true).consumed); // still the ADSR modifier pair
    const auto opened = page.poll(kShift | kSix | kTwo, 0, true);
    CHECK(opened.consumed);
    CHECK(opened.open);
    CHECK(page.active);
    CHECK(page.waitRelease); // the opening fingers must lift before the page acts
    CHECK(page.layer == 0);

    for (const uint8_t held : {uint8_t(kShift), uint8_t(kSix), uint8_t(kTwo), uint8_t(kShift | kTwo),
                               uint8_t(kSix | kTwo), uint8_t(kOne | kSix | kShift)})
    {
        // Missing a key of the chord, or pressing 1 instead of 2, never opens it.
        ReverbPage::Controls other;
        other.poll(static_cast<uint8_t>(held & ~kTwo), 0, true);
        CAPTURE(int(held));
        CHECK_FALSE(other.poll(held, 0, true).open);
    }

    // Button 2 already down before Shift + 6 is not a press of it.
    ReverbPage::Controls early;
    early.poll(kTwo, 0, true);
    early.poll(kTwo | kShift, 0, true);
    CHECK_FALSE(early.poll(kTwo | kShift | kSix, 0, true).open);

    // The ADSR page (or its release tail) owns the chord: no second screen.
    ReverbPage::Controls blocked;
    blocked.poll(kShift | kSix, 0, false);
    CHECK_FALSE(blocked.poll(kShift | kSix | kTwo, 0, false).consumed);
    CHECK_FALSE(blocked.active);
}

TEST_CASE("Reverb page consumes its whole gesture and acts on presses only", "[reverb_page][control_surface]")
{
    ReverbPage::Controls page;
    page.poll(kShift | kSix, 0, true);
    REQUIRE(page.poll(kShift | kSix | kTwo, 0, true).open);
    // Release tail: everything is consumed until every button and voice is up.
    CHECK(page.poll(kShift | kTwo, 0, true).consumed);
    CHECK(page.poll(kTwo, 1u << 2, true).consumed);
    CHECK_FALSE(page.poll(kTwo, 1u << 2, true).toggleLayer); // the entry key's release is not a press
    CHECK(page.waitRelease);
    CHECK(page.poll(0, 0, true).consumed);
    CHECK_FALSE(page.waitRelease);
    CHECK(page.layer == 0);

    // Freeze: button 1 toggles on the press edge, never while held.
    auto in = page.poll(kOne, 0, true);
    CHECK(in.consumed);
    CHECK(in.toggleFreeze);
    CHECK_FALSE(page.poll(kOne, 0, true).toggleFreeze);
    CHECK_FALSE(page.poll(0, 0, true).toggleFreeze);
    CHECK(page.poll(kOne, 0, true).toggleFreeze);
    page.poll(0, 0, true);

    // Layer: button 2 flips MAIN <-> TONE and reports it.
    in = page.poll(kTwo, 0, true);
    CHECK(in.toggleLayer);
    CHECK(page.layer == 1);
    page.poll(0, 0, true);
    CHECK(page.poll(kTwo, 0, true).toggleLayer);
    CHECK(page.layer == 0);
    page.poll(0, 0, true);

    // Voice buttons, pads' partners and every other key do nothing here.
    for (uint8_t bit = 2; bit < 7; ++bit)
    {
        if (bit == 1) continue;
        const auto quiet = page.poll(uint8_t(1u << bit), 0x0F, true);
        CAPTURE(int(bit));
        CHECK(quiet.consumed);
        CHECK_FALSE(quiet.toggleFreeze);
        CHECK_FALSE(quiet.exit);
        page.poll(0, 0, true);
    }
    // Shift leaves; its release tail is consumed too, and normal input then resumes.
    const auto leave = page.poll(kShift, 0, true);
    CHECK(leave.exit);
    CHECK_FALSE(page.active);
    CHECK(page.poll(kShift, 8, true).consumed);
    CHECK(page.poll(0, 0, true).consumed);
    CHECK_FALSE(page.poll(0, 0, true).consumed);
    // It can be opened again from a clean start, back on the MAIN layer.
    page.layer = 1;
    page.poll(kShift | kSix, 0, true);
    CHECK(page.poll(kShift | kSix | kTwo, 0, true).open);
    CHECK(page.layer == 0);
}

TEST_CASE("A button held across another screen is not a fresh press afterwards", "[reverb_page][control_surface]")
{
    ReverbPage::Controls page;
    page.observe(kShift | kSix | kTwo, 0); // held while the voice editor owned the pass
    CHECK_FALSE(page.poll(kShift | kSix | kTwo, 0, true).open);
}

TEST_CASE("The Reverb chord coexists with the ADSR chord and its deferred Shift+6 action", "[reverb_page][control_surface][voice_envelope]")
{
    UIState ui;
    VoiceEnvelope::Input adsr;

    // Shift + 6 alone, released: the ADSR page's deferred action still fires, once.
    CHECK_FALSE(bridgePass(ui, kShift, 0, &adsr));
    CHECK_FALSE(bridgePass(ui, kShift | kSix, 0, &adsr));
    CHECK(adsr.consumed);
    CHECK_FALSE(bridgePass(ui, kSix, 0, &adsr));
    CHECK_FALSE(bridgePass(ui, 0, 0, &adsr));
    CHECK(adsr.modifierTap);
    CHECK_FALSE(ui.reverbPage.active);

    // Shift + 6 + 2 opens the reverb page and swallows the ADSR chord: releasing the
    // fingers never fires the Shift+6 action, not on release and not on exit.
    ui = UIState{};
    CHECK_FALSE(bridgePass(ui, kShift | kSix, 0, &adsr));
    CHECK(ui.voiceEnvelope.chordPending); // the ADSR chord had started...
    CHECK(bridgePass(ui, kShift | kSix | kTwo, 0));
    CHECK(ui.reverbPage.active);
    CHECK_FALSE(ui.voiceEnvelope.chordPending); // ...and opening the page dropped it
    CHECK(bridgePass(ui, kShift, 0));
    CHECK(bridgePass(ui, 0, 0));
    CHECK_FALSE(ui.reverbPage.waitRelease);
    bool tapped = false;
    tapped |= bridgePass(ui, kShift, 0); // Shift leaves the page
    CHECK_FALSE(ui.reverbPage.active);
    tapped = false;
    for (const uint8_t levels : {uint8_t(kShift), uint8_t(0), uint8_t(0)})
        if (!bridgePass(ui, levels, 0, &adsr)) tapped |= adsr.modifierTap;
    CHECK_FALSE(tapped);

    // While the ADSR page is open the chord cannot open a second screen.
    ui = UIState{};
    CHECK_FALSE(bridgePass(ui, kShift | kSix, 0, &adsr));
    CHECK_FALSE(bridgePass(ui, kShift | kSix, 1u << 1, &adsr));
    REQUIRE(ui.voiceEnvelope.active);
    CHECK_FALSE(bridgePass(ui, kShift | kSix | kTwo, 0, &adsr));
    CHECK_FALSE(ui.reverbPage.active);
}

TEST_CASE("Opening the Reverb page clears competing UI and blocks length holds", "[reverb_page][ui_transitions]")
{
    UIState ui;
    ui.arp.setActive(true);
    ui.settingsMode = ui.slideMode = ui.gateSeqLengthMode = true;
    ui.parameterButtonHeld[0] = true;
    ui.latchedParameter = 2;
    ui.selectedStepForEdit = 5;
    ui.padPressTimestamps[3] = 100;
    ui.voiceEnvelope.chordPending = true;
    UITransitions::openReverbPage(ui);
    CHECK_FALSE(ui.settingsMode);
    CHECK_FALSE(ui.slideMode);
    CHECK_FALSE(ui.gateSeqLengthMode);
    CHECK_FALSE(ui.parameterButtonHeld[0]);
    CHECK(ui.latchedParameter == -1);
    CHECK(ui.selectedStepForEdit == -1);
    CHECK(ui.padPressTimestamps[3] == 0);
    CHECK_FALSE(ui.voiceEnvelope.chordPending);
    CHECK(ui.arp.active()); // the page is live: the arp keeps running
    CHECK(ui.resetStepsLightsFlag);

    // A voice press while the page is open (or draining) cannot arm gate-length entry.
    for (const bool waiting : {false, true})
    {
        UIState state;
        state.reverbPage.active = !waiting;
        state.reverbPage.waitRelease = waiting;
        UITransitions::beginGateLengthHold(state, 0);
        CHECK(state.gateSeqLengthVoice == -1);
        state.reverbPage = {};
        UITransitions::beginGateLengthHold(state, 0);
        REQUIRE(state.gateSeqLengthVoice == 0);
        state.reverbPage.active = !waiting;
        state.reverbPage.waitRelease = waiting;
        CHECK_FALSE(UITransitions::updateGateLengthHold(state, 0, true, 1000, 400));
        CHECK_FALSE(state.gateSeqLengthMode);
    }
    UITransitions::closeReverbPage(ui);
    CHECK(ui.reverbPage.lastControl == 255);
}

TEST_CASE("Faders map to reverb controls by layer, with Freeze on a button", "[reverb_page][control_surface]")
{
    using ControlSurface::reverbControlForFader;
    CHECK(reverbControlForFader(0, 0) == ReverbControl::Mix);
    CHECK(reverbControlForFader(0, 1) == ReverbControl::Decay);
    CHECK(reverbControlForFader(0, 2) == ReverbControl::Damping);
    CHECK(reverbControlForFader(0, 3) == ReverbControl::Count); // Freeze is a button, not a fader
    CHECK(reverbControlForFader(1, 0) == ReverbControl::LowCut);
    CHECK(reverbControlForFader(1, 1) == ReverbControl::Diffusion);
    CHECK(reverbControlForFader(1, 2) == ReverbControl::ModDepth);
    CHECK(reverbControlForFader(1, 3) == ReverbControl::Width);
    CHECK(reverbControlForFader(2, 0) == ReverbControl::Count);
    CHECK(reverbControlForFader(0, 4) == ReverbControl::Count);
    // Every setting is reachable exactly once across the two layers.
    for (const ReverbControl control : kAll)
    {
        int reachable = 0;
        for (uint8_t layer = 0; layer < ControlSurface::kReverbLayerCount; ++layer)
            for (uint8_t channel = 0; channel < 4; ++channel)
                reachable += reverbControlForFader(layer, channel) == control ? 1 : 0;
        CAPTURE(int(control));
        CHECK(reachable == 1);
    }
}

TEST_CASE("Reverb fader curves hit the exact limits and are monotonic and invertible", "[reverb_page][control_surface]")
{
    using namespace ControlSurface;
    struct Limits
    {
        ReverbControl control;
        float low, high;
        bool logarithmic;
    };
    const Limits limits[] = {
        {ReverbControl::Mix, ReverbParams::kMixMin, ReverbParams::kMixMax, false},
        {ReverbControl::Decay, ReverbParams::kDecayMin, ReverbParams::kDecayMax, true},
        {ReverbControl::Damping, ReverbParams::kDampingMin, ReverbParams::kDampingMax, true},
        {ReverbControl::LowCut, ReverbParams::kLowCutMin, ReverbParams::kLowCutMax, true},
        {ReverbControl::Diffusion, ReverbParams::kDiffusionMin, ReverbParams::kDiffusionMax, false},
        {ReverbControl::ModDepth, ReverbParams::kModDepthMin, ReverbParams::kModDepthMax, false},
        {ReverbControl::Width, ReverbParams::kWidthMin, ReverbParams::kWidthMax, false},
    };
    for (const Limits &l : limits)
    {
        CAPTURE(int(l.control), reverbControlName(l.control));
        CHECK(reverbValueForFader(l.control, 0.0f) == l.low);
        CHECK(reverbValueForFader(l.control, 1.0f) == l.high);
        float previous = l.low;
        for (int i = 0; i <= 1000; ++i)
        {
            const float n = static_cast<float>(i) / 1000.0f;
            const float value = reverbValueForFader(l.control, n);
            REQUIRE(value >= l.low);
            REQUIRE(value <= l.high);
            REQUIRE(value >= previous);
            previous = value;
            // value -> fader -> value returns where it started.
            REQUIRE(reverbFaderForValue(l.control, value) == Approx(n).margin(2.0e-4));
        }
        const float middle = reverbValueForFader(l.control, 0.5f);
        if (l.logarithmic)
            CHECK(middle == Approx(std::sqrt(l.low * l.high)).epsilon(1.0e-4)); // equal travel per octave
        else
            CHECK(middle == Approx(0.5f * (l.low + l.high)).epsilon(1.0e-5));
        // Out-of-range and hostile positions clamp; nothing non-finite comes out.
        CHECK(reverbValueForFader(l.control, -3.0f) == l.low);
        CHECK(reverbValueForFader(l.control, 7.0f) == l.high);
        CHECK(reverbValueForFader(l.control, std::numeric_limits<float>::quiet_NaN()) == l.low);
        CHECK(reverbValueForFader(l.control, std::numeric_limits<float>::infinity()) == l.low);
        CHECK(reverbFaderForValue(l.control, l.low - 1.0f) == 0.0f);
        CHECK(reverbFaderForValue(l.control, l.high * 2.0f) == 1.0f);
        CHECK(reverbFaderForValue(l.control, std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    }
    // Anchors a performer can check against the display: decade/octave landmarks.
    CHECK(reverbValueForFader(ReverbControl::Decay, 0.5f) == Approx(10.0f).epsilon(1.0e-4));   // 10 s mid-throw
    CHECK(reverbValueForFader(ReverbControl::LowCut, 0.5f) == Approx(100.0f).epsilon(1.0e-4));
    CHECK(reverbValueForFader(ReverbControl::Damping, 0.5f) == Approx(1039.2f).epsilon(1.0e-3));
    CHECK(reverbValueForFader(ReverbControl::Count, 0.5f) == 0.0f);
    CHECK(std::strcmp(reverbControlName(ReverbControl::Count), "") == 0);
    // The documented defaults sit on the travel where a fresh reverb rests.
    ReverbSettings defaults;
    CHECK(reverbValueOf(defaults, ReverbControl::Decay) == 20.0f);
    CHECK(reverbValueOf(defaults, ReverbControl::Count) == 0.0f);
}

TEST_CASE("Reverb values format compactly and predictably for the OLED", "[reverb_page][control_surface]")
{
    using namespace ControlSurface;
    const auto text = [](ReverbControl control, float value) {
        char buffer[16];
        formatReverbValue(control, value, buffer, sizeof buffer);
        return std::string(buffer);
    };
    CHECK(text(ReverbControl::Mix, 0.0f) == "0%");
    CHECK(text(ReverbControl::Mix, 0.35f) == "35%");
    CHECK(text(ReverbControl::Mix, 1.0f) == "100%");
    CHECK(text(ReverbControl::Decay, 0.1f) == "0.10s");
    CHECK(text(ReverbControl::Decay, 0.35f) == "0.35s");
    CHECK(text(ReverbControl::Decay, 3.2f) == "3.2s");
    CHECK(text(ReverbControl::Decay, 20.0f) == "20s");
    CHECK(text(ReverbControl::Decay, 450.0f) == "450s");
    CHECK(text(ReverbControl::Decay, 1000.0f) == "1000s");
    CHECK(text(ReverbControl::Damping, 100.0f) == "100Hz");
    CHECK(text(ReverbControl::Damping, 800.0f) == "800Hz");
    CHECK(text(ReverbControl::Damping, 3000.0f) == "3.0kHz");
    CHECK(text(ReverbControl::Damping, 10800.0f) == "10.8kHz");
    CHECK(text(ReverbControl::LowCut, 40.0f) == "40Hz");
    CHECK(text(ReverbControl::LowCut, 1000.0f) == "1000Hz");
    CHECK(text(ReverbControl::Diffusion, 0.8f) == "80%");
    CHECK(text(ReverbControl::ModDepth, 0.5f) == "50%");
    CHECK(text(ReverbControl::Width, 1.0f) == "100%");
    CHECK(text(ReverbControl::Width, 2.0f) == "200%");
    // Out-of-range values show the nearest limit; non-finite and unassigned show "--".
    CHECK(text(ReverbControl::Mix, 5.0f) == "100%");
    CHECK(text(ReverbControl::Decay, -1.0f) == "0.10s");
    CHECK(text(ReverbControl::Decay, std::numeric_limits<float>::quiet_NaN()) == "--");
    CHECK(text(ReverbControl::Count, 1.0f) == "--");

    // Every position of every fader fits the OLED column (7 characters).
    for (const ReverbControl control : kAll)
        for (int i = 0; i <= 1000; ++i)
        {
            char buffer[16];
            formatReverbValue(control, reverbValueForFader(control, static_cast<float>(i) / 1000.0f), buffer, sizeof buffer);
            CAPTURE(int(control), i, buffer);
            REQUIRE(std::strlen(buffer) <= 7);
        }
    // Small or missing buffers are safe and always terminated.
    char tiny[3] = {'x', 'x', 'x'};
    formatReverbValue(ReverbControl::Damping, 3000.0f, tiny, 3);
    CHECK(tiny[2] == '\0');
    char one[1] = {'x'};
    formatReverbValue(ReverbControl::Mix, 0.5f, one, 1);
    CHECK(one[0] == '\0');
    formatReverbValue(ReverbControl::Mix, 0.5f, nullptr, 8);
    formatReverbValue(ReverbControl::Mix, 0.5f, tiny, 0);
}
