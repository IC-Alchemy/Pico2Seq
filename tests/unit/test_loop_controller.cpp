#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "app/LoopController.h"
#include "ui/ControlSurfaceLogic.h"
#include "ui/LoopPageControls.h"
#include "ui/UITransitions.h"
#include "voice/LoopEngine.h"
#include "voice/LoopTiming.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// The Core 0 side of the looper against the REAL engine: a simulated clock delivers
// steps at the tempo, a simulated audio thread renders the frames that pass between
// them, and the controller's decisions are checked by what the loop actually does.

namespace {
using Catch::Approx;
using Phase = LoopController::Phase;
constexpr float kSr = 48000.0f;
constexpr float kBpm = 120.0f;                 // 6000 frames per step
constexpr uint32_t kStepFrames = 6000;

struct Rig
{
    std::vector<uint8_t> storage;
    LoopEngine engine;
    LoopController controller;
    std::vector<float> heard; // everything the bus produced
    float live = 0.0f;        // constant "live bus" level fed in
    explicit Rig(size_t samples = 65536) : storage(samples / 2 * 3, 0)
    {
        engine.attach(storage.data(), storage.size(), kSr);
        controller.bind(&engine, kSr);
        controller.update(kBpm);
    }
    void render(uint32_t frames)
    {
        std::vector<float> block(frames, live);
        uint32_t at = 0;
        while (at < frames)
        {
            const uint32_t n = std::min<uint32_t>(256, frames - at);
            engine.processBlock(block.data() + at, n);
            at += n;
        }
        heard.insert(heard.end(), block.begin(), block.end());
    }
    // One clock step: the controller hears it, then the step's frames are rendered.
    void step(float bpm = kBpm)
    {
        controller.onStep(bpm);
        controller.update(bpm);
        render(kStepFrames);
    }
    void steps(unsigned count) { for (unsigned i = 0; i < count; ++i) step(); }
    void start() { controller.onClockStart(); controller.update(kBpm); }
};
} // namespace

TEST_CASE("A tap waits for the next boundary, records the chosen size and then plays", "[loop][loop_controller]")
{
    Rig rig;
    rig.controller.setSizeIndex(0); // 4 steps
    rig.start();
    rig.live = 0.5f;
    rig.steps(1);                    // step 0 has passed; we are mid-boundary
    rig.controller.tap(kBpm);
    CHECK(rig.controller.phase() == Phase::Armed);
    CHECK(rig.controller.stepsUntilStart() == 4);  // steps 1,2,3 then the boundary at 4

    rig.steps(3);                    // steps 1..3: still waiting
    CHECK(rig.controller.phase() == Phase::Armed);
    CHECK(rig.controller.stepsUntilStart() == 1);
    CHECK(rig.engine.takesStarted() == 0);

    rig.step();                      // step 4: the boundary
    CHECK(rig.engine.takesStarted() == 1);
    CHECK(rig.controller.phase() == Phase::Recording);
    CHECK(rig.controller.loopSteps() == 4);
    CHECK(rig.controller.currentStep() >= 1);

    rig.steps(2);                    // steps 5,6: still inside the 4-step take
    CHECK(rig.engine.state() == LoopEngine::State::Recording);
    rig.live = 0.0f;
    rig.step();                      // step 7 rendered: the take ended on its 24,000th frame
    CHECK(rig.controller.phase() == Phase::Playing);
    CHECK(rig.engine.storedSamples() == 4 * kStepFrames);
}

TEST_CASE("A bar-long take starts on the bar; a tap during it queues a layer instead of abandoning it", "[loop][loop_controller]")
{
    Rig rig;
    rig.controller.setSizeIndex(2); // 16 steps: the longest loop, one bar
    rig.start();
    rig.steps(5);
    rig.controller.tap(kBpm);
    CHECK(rig.controller.stepsUntilStart() == 12); // next step is 5; boundary at 16
    rig.steps(11);
    CHECK(rig.engine.takesStarted() == 0);
    rig.step();
    CHECK(rig.engine.takesStarted() == 1);
    CHECK(rig.controller.loopSteps() == 16);

    rig.steps(3);
    CHECK(rig.controller.phase() == Phase::Recording);
    CHECK_FALSE(rig.controller.layerQueued());
    // A tap mid-take queues a layer. The take carries on (it is not abandoned), the state
    // stays "Recording" rather than turning into "Armed", and the page can show +DUB.
    rig.controller.tap(kBpm);
    CHECK(rig.controller.phase() == Phase::Recording);
    CHECK(rig.controller.layerQueued());
    // Tapping again withdraws it.
    rig.controller.tap(kBpm);
    CHECK_FALSE(rig.controller.layerQueued());
    CHECK(rig.controller.phase() == Phase::Recording);
    rig.controller.update(kBpm);
    rig.render(256);
    CHECK(rig.controller.phase() == Phase::Recording);
    CHECK(rig.controller.loopSteps() == 16);
}

TEST_CASE("Every tap is another layer: back-to-back passes mixed into one loop", "[loop][loop_controller][loop_layers]")
{
    Rig rig;
    rig.controller.setSizeIndex(0); // 4 steps
    rig.start();
    rig.live = 0.2f;
    const uint32_t memory = rig.engine.capacitySamples();
    rig.controller.tap(kBpm);       // 1st press: begins on step 0 and records the loop length
    rig.steps(1);
    CHECK(rig.engine.takesStarted() == 1);
    rig.controller.tap(kBpm);       // 2nd press, during the take: the next pass is a layer
    CHECK(rig.controller.layerQueued());
    rig.steps(3);                   // steps 1..3: the take ends with step 3
    rig.step();                     // step 4: the boundary; the layer begins
    CHECK(rig.engine.takesStarted() == 2);
    CHECK(rig.controller.phase() == Phase::Overdubbing);
    CHECK_FALSE(rig.controller.layerQueued());
    rig.controller.tap(kBpm);       // 3rd press, during that layer: one more behind it
    CHECK(rig.controller.layerQueued());
    rig.steps(3);
    rig.step();                     // step 8: the boundary again
    CHECK(rig.engine.takesStarted() == 3);
    CHECK(rig.controller.phase() == Phase::Overdubbing);
    // No further press: that layer is the last (steps 8..11), and the loop settles into plain
    // playback. The live bus stays on until the layer is done.
    rig.steps(3);
    rig.live = 0.0f;
    rig.steps(1);                   // step 12: the layer has ended
    CHECK(rig.controller.phase() == Phase::Playing);
    CHECK(rig.controller.loopSteps() == 4);
    // Three passes of 0.2 were mixed into the one loop; the loop did not grow to hold them.
    const size_t before = rig.heard.size();
    rig.steps(2);
    CHECK(rig.heard[before + kStepFrames] == Approx(0.6f).margin(0.01f));
    CHECK(rig.engine.capacitySamples() == memory);
    CHECK(rig.engine.storedSamples() == 4 * kStepFrames);
}

TEST_CASE("A clock edge that reaches the controller before the audio thread still chains the layer", "[loop][loop_controller][loop_layers]")
{
    Rig rig;
    rig.controller.setSizeIndex(0);
    rig.start();
    rig.live = 0.2f;
    rig.controller.tap(kBpm);
    rig.steps(1);                              // step 0 delivered: the take is under way
    rig.controller.tap(kBpm);                  // queue a layer behind it
    rig.steps(2);                              // steps 1 and 2
    // Step 3 is delivered normally; step 4's clock edge arrives 40 frames before the audio
    // thread has finished the take, so the controller's Record reaches an engine that is
    // still Recording.
    rig.controller.onStep(kBpm);
    rig.controller.update(kBpm);
    rig.render(kStepFrames - 40);
    rig.controller.onStep(kBpm);               // step 4, early
    rig.controller.update(kBpm);
    rig.render(40);
    CHECK(rig.engine.audioState() == LoopEngine::State::Overdubbing); // straight into the layer
    CHECK(rig.engine.takesStarted() == 2);
    rig.live = 0.0f;
    rig.render(kStepFrames);
    rig.steps(3);
    rig.controller.update(kBpm);
    CHECK(rig.controller.phase() == Phase::Playing);
}

TEST_CASE("Stopping the transport withdraws a queued layer", "[loop][loop_controller][loop_layers]")
{
    Rig rig;
    rig.controller.setSizeIndex(0);
    rig.start();
    rig.controller.tap(kBpm);
    rig.steps(5);                              // the take ended; a loop is playing
    REQUIRE(rig.controller.phase() == Phase::Playing);
    rig.controller.tap(kBpm);
    rig.controller.tap(kBpm);                  // arm, then (while Armed) disarm
    CHECK(rig.controller.phase() == Phase::Playing);
    rig.controller.tap(kBpm);                  // arm a layer for the loop's next boundary
    CHECK(rig.controller.phase() == Phase::Armed);
    rig.controller.onClockStop();
    CHECK_FALSE(rig.controller.layerQueued());
    CHECK(rig.controller.phase() == Phase::Playing);
}

TEST_CASE("Tapping a playing loop arms a layer on the loop's own boundary", "[loop][loop_controller]")
{
    Rig rig;
    rig.controller.setSizeIndex(0);
    rig.start();
    rig.live = 0.4f;
    rig.controller.tap(kBpm);        // before the first step: begins on step 0
    rig.steps(1);
    rig.live = 0.4f;
    rig.steps(3);
    rig.live = 0.0f;
    rig.steps(1);
    REQUIRE(rig.controller.phase() == Phase::Playing);
    const uint32_t first = rig.engine.takesStarted();

    // The loop began on step 0 and is 4 steps long: boundaries at steps 4k.
    rig.steps(1);                    // step 5 has been delivered; now at 6
    rig.controller.tap(kBpm);
    CHECK(rig.controller.phase() == Phase::Armed);
    CHECK(rig.controller.stepsUntilStart() == 3); // steps 6,7 then boundary 8
    // Tap again disarms.
    rig.controller.tap(kBpm);
    CHECK(rig.controller.phase() == Phase::Playing);
    rig.controller.tap(kBpm);
    rig.steps(3);
    CHECK(rig.engine.takesStarted() == first + 1);
    CHECK(rig.controller.loopSteps() == 4); // a layer keeps the loop's size
}

TEST_CASE("The loop is re-synced at every repeat without moving a healthy loop", "[loop][loop_controller]")
{
    Rig rig;
    rig.controller.setSizeIndex(0);
    rig.start();
    rig.live = 0.5f;
    rig.controller.tap(kBpm);
    rig.steps(4);
    rig.live = 0.0f;
    rig.steps(4);                       // take done; first pass playing
    REQUIRE(rig.controller.phase() == Phase::Playing);
    const size_t before = rig.heard.size();
    rig.steps(16);                      // four syncs delivered
    // Every pass of a steady 0.5 take is identical; no sync disturbed it.
    const size_t pass = 4 * kStepFrames;
    const size_t mid = before + 2 * pass + pass / 2;
    CHECK(rig.heard[before + pass / 2] == Approx(0.5f).margin(0.002f));
    CHECK(rig.heard[mid] == Approx(0.5f).margin(0.002f));
    for (size_t i = before + 500; i < rig.heard.size() - 500; ++i)
    {
        const size_t inPass = (i - before) % pass;
        if (inPass > 100 && inPass < pass - 100)
            REQUIRE(rig.heard[i] == Approx(0.5f).margin(0.002f));
    }
}

TEST_CASE("A tempo change retimes the loop on the grid", "[loop][loop_controller]")
{
    Rig rig;
    rig.controller.setSizeIndex(0);
    rig.start();
    rig.live = 0.5f;
    rig.controller.tap(kBpm);
    rig.steps(4);
    rig.live = 0.0f;
    rig.steps(4);
    REQUIRE(rig.controller.phase() == Phase::Playing);
    // 240 BPM: a step is 3000 frames, the 4-step loop 12,000. Count wraps over 6 new steps.
    const size_t before = rig.heard.size();
    for (int i = 0; i < 8; ++i)
    {
        rig.controller.onStep(240.0f);
        rig.controller.update(240.0f);
        rig.render(3000);
    }
    unsigned seams = 0;
    for (size_t i = before + 1; i < rig.heard.size(); ++i)
        if (rig.heard[i - 1] < 0.05f && rig.heard[i] >= 0.05f)
            ++seams;
    CHECK(seams >= 2); // two full passes of 12,000 frames in 24,000
    CHECK(seams <= 3);
}

TEST_CASE("A hold clears; stopping the clock drops an unfinished first take", "[loop][loop_controller]")
{
    Rig rig;
    rig.controller.setSizeIndex(0);
    rig.start();
    rig.live = 0.5f;
    rig.controller.tap(kBpm);
    rig.steps(4);
    rig.live = 0.0f;
    rig.steps(4);
    REQUIRE(rig.controller.phase() == Phase::Playing);
    rig.controller.clear();
    rig.controller.update(kBpm);
    rig.render(512);
    CHECK(rig.controller.phase() == Phase::Empty);
    CHECK(rig.controller.loopSteps() == 0);

    // Stop mid-take.
    rig.controller.tap(kBpm);
    rig.live = 0.5f;
    rig.steps(2);                    // the take began on step 8 and is two steps in
    REQUIRE(rig.controller.phase() == Phase::Recording);
    rig.controller.onClockStop();
    rig.controller.update(kBpm);
    rig.render(512);
    CHECK(rig.controller.phase() == Phase::Empty);
    CHECK(rig.controller.loopSteps() == 0);
}

TEST_CASE("With the transport stopped a tap records immediately at the tempo", "[loop][loop_controller]")
{
    Rig rig;
    rig.controller.setSizeIndex(0);
    rig.live = 0.3f;
    rig.controller.tap(kBpm);       // clock never started
    rig.controller.update(kBpm);
    rig.render(256);
    CHECK(rig.controller.phase() == Phase::Recording);
    rig.render(4 * kStepFrames);
    CHECK(rig.controller.phase() == Phase::Playing);
    CHECK(rig.engine.storedSamples() == 4 * kStepFrames);
    // Armed takes are dropped when the transport stops.
    rig.controller.clear();
    rig.controller.update(kBpm);
    rig.render(256);
    rig.start();
    rig.controller.tap(kBpm);
    rig.controller.onClockStop();
    CHECK(rig.controller.phase() == Phase::Empty);
}

TEST_CASE("A transport restart plays the loop from its top", "[loop][loop_controller]")
{
    // Distinct halves: loud first two steps, quiet last two.
    Rig distinct;
    distinct.controller.setSizeIndex(0);
    distinct.start();
    distinct.controller.tap(kBpm);
    distinct.live = 0.8f;
    distinct.steps(1);
    distinct.steps(1);
    distinct.live = 0.1f;
    distinct.steps(2);
    distinct.live = 0.0f;
    distinct.steps(1);                       // a step into the first pass
    REQUIRE(distinct.controller.phase() == Phase::Playing);
    distinct.controller.onClockStop();
    distinct.controller.onClockStart();      // restart: the next step is step 0 again
    distinct.controller.update(kBpm);
    distinct.render(1024);
    const size_t at = distinct.heard.size();
    distinct.render(2 * kStepFrames);
    // The loop restarted at its loud half rather than continuing in the quiet one.
    CHECK(distinct.heard[at + kStepFrames / 2] > 0.5f);
}

TEST_CASE("Without a loop buffer the controller does nothing", "[loop][loop_controller]")
{
    LoopEngine engine; // never attached: Disabled
    LoopController controller;
    controller.tap(kBpm); // unbound: no crash
    controller.onStep(kBpm);
    controller.bind(&engine, kSr);
    CHECK(controller.phase() == Phase::Unavailable);
    controller.tap(kBpm);
    controller.onClockStart();
    controller.onStep(kBpm);
    controller.update(kBpm);
    controller.clear();
    CHECK(controller.phase() == Phase::Unavailable);
    controller.setSizeIndex(200);
    CHECK(controller.sizeSteps() == 16); // out of range clamps to the longest
}

// --- Loop Settings page ---------------------------------------------------------

TEST_CASE("The percent faders reach 100% over the lower 85% of travel", "[loop][loop_page]")
{
    using namespace ControlSurface;
    CHECK(loopVolumeForFader(0.0f) == 0.0f);
    CHECK(loopVolumeForFader(0.425f) == Approx(0.5f));
    CHECK(loopVolumeForFader(0.85f) == 1.0f);
    // The top 15% is flat at 100%.
    for (float x = 0.85f; x <= 1.0f; x += 0.01f)
        CHECK(loopVolumeForFader(x) == 1.0f);
    CHECK(loopVolumeForFader(1.0f) == 1.0f);
    CHECK(loopVolumeForFader(7.0f) == 1.0f);
    CHECK(loopVolumeForFader(-1.0f) == 0.0f);
    CHECK(sequencerVolumeForFader(0.9f) == 1.0f);
    CHECK(sequencerVolumeForFader(0.425f) == Approx(0.5f));
    CHECK(loopVolumeForFader(std::nanf("")) == 0.0f);
    // Monotonic below the plateau.
    float previous = -1.0f;
    for (int i = 0; i <= 85; ++i)
    {
        const float v = loopVolumeForFader(static_cast<float>(i) / 100.0f);
        CHECK(v >= previous);
        previous = v;
    }
}

TEST_CASE("Regen runs 10% to 100% with the top 15% at 100%", "[loop][loop_page]")
{
    using namespace ControlSurface;
    CHECK(loopRegenForFader(0.0f) == Approx(0.10f));
    CHECK(loopRegenForFader(0.425f) == Approx(0.55f));
    CHECK(loopRegenForFader(0.85f) == Approx(1.0f));
    CHECK(loopRegenForFader(0.86f) == 1.0f);
    CHECK(loopRegenForFader(1.0f) == 1.0f);
    CHECK(loopRegenForFader(-3.0f) == Approx(0.10f));
    CHECK(loopRegenForFader(std::nanf("")) == Approx(0.10f));
}

TEST_CASE("The length fader picks 4, 8 or 16 steps in equal zones", "[loop][loop_page]")
{
    using namespace ControlSurface;
    const uint8_t expected[3] = {4, 8, 16};
    for (uint8_t zone = 0; zone < 3; ++zone)
        for (float within : {0.02f, 0.5f, 0.98f})
        {
            const float x = (static_cast<float>(zone) + within) / 3.0f;
            CHECK(LoopTiming::stepsForIndex(loopSizeIndexForFader(x)) == expected[zone]);
        }
    CHECK(loopSizeIndexForFader(0.0f) == 0);
    CHECK(loopSizeIndexForFader(1.0f) == 2);       // the very top is the longest, not out of range
    CHECK(loopSizeIndexForFader(5.0f) == 2);
    CHECK(loopSizeIndexForFader(-1.0f) == 0);
    CHECK(loopSizeIndexForFader(std::nanf("")) == 0);
    // The top 15% of travel (>= 85%) is all inside the 16-step zone.
    for (float x = 0.85f; x <= 1.0f; x += 0.01f)
        CHECK(loopSizeIndexForFader(x) == 2);
}

TEST_CASE("Loop page faders and labels", "[loop][loop_page]")
{
    using namespace ControlSurface;
    CHECK(loopControlForFader(0) == LoopControl::LoopVolume);
    CHECK(loopControlForFader(1) == LoopControl::LoopLength);
    CHECK(loopControlForFader(2) == LoopControl::SequencerVolume);
    CHECK(loopControlForFader(3) == LoopControl::Regen);
    CHECK(loopControlForFader(4) == LoopControl::Count);
    CHECK(std::string(loopControlName(LoopControl::LoopVolume)) == "Loop Vol");
    CHECK(std::string(loopControlName(LoopControl::Count)) == "");
    char text[16];
    formatLoopValue(LoopControl::LoopVolume, 0.8f, text, sizeof(text));
    CHECK(std::string(text) == "80%");
    formatLoopValue(LoopControl::Regen, 1.0f, text, sizeof(text));
    CHECK(std::string(text) == "100%");
    formatLoopValue(LoopControl::LoopLength, 16.0f, text, sizeof(text));
    CHECK(std::string(text) == "16 st");
    formatLoopValue(LoopControl::Count, 0.0f, text, sizeof(text));
    CHECK(std::string(text) == "--");
    formatLoopValue(LoopControl::SequencerVolume, std::nanf(""), text, sizeof(text));
    CHECK(std::string(text) == "--");
    char tiny[3];
    formatLoopValue(LoopControl::Regen, 1.0f, tiny, sizeof(tiny));
    CHECK(tiny[2] == '\0');
}

TEST_CASE("The loop button debounces and sorts presses into tap, hold and chord", "[loop][loop_page]")
{
    using LoopPage::Button;
    using Event = Button::Event;
    auto press = [](Button &b, uint32_t &now, bool shift) {
        Event seen = Event::None;
        for (int i = 0; i < 40; ++i, ++now) { const Event e = b.update(true, now, shift); if (e != Event::None) seen = e; }
        return seen;
    };
    {   // A clean tap: nothing at the press, Tap on release.
        Button b; uint32_t now = 1000; b.begin(false, now);
        CHECK(press(b, now, false) == Event::None);
        Event e = Event::None;
        for (int i = 0; i < 40; ++i, ++now) { const Event r = b.update(false, now, false); if (r != Event::None) e = r; }
        CHECK(e == Event::Tap);
    }
    {   // Contact bounce shorter than the debounce window is no press at all.
        Button b; uint32_t now = 1000; b.begin(false, now);
        Event any = Event::None;
        for (int i = 0; i < 12; ++i, ++now) { const Event r = b.update(i % 2 == 0, now, false); if (r != Event::None) any = r; }
        for (int i = 0; i < 100; ++i, ++now) { const Event r = b.update(false, now, false); if (r != Event::None) any = r; }
        CHECK(any == Event::None);
    }
    {   // Held long enough: one Hold mid-press, and the release is not also a tap.
        Button b; uint32_t now = 1000; b.begin(false, now);
        unsigned holds = 0, taps = 0;
        for (int i = 0; i < 1200; ++i, ++now) { const Event r = b.update(true, now, false); holds += r == Event::Hold; taps += r == Event::Tap; }
        for (int i = 0; i < 100; ++i, ++now) { const Event r = b.update(false, now, false); holds += r == Event::Hold; taps += r == Event::Tap; }
        CHECK(holds == 1);
        CHECK(taps == 0);
    }
    {   // Shift at the press: a chord, with no tap or hold afterwards.
        Button b; uint32_t now = 1000; b.begin(false, now);
        unsigned chords = 0, others = 0;
        for (int i = 0; i < 1200; ++i, ++now) { const Event r = b.update(true, now, true); chords += r == Event::ChordPress; others += (r == Event::Hold || r == Event::Tap); }
        for (int i = 0; i < 100; ++i, ++now) { const Event r = b.update(false, now, true); others += (r == Event::Hold || r == Event::Tap); }
        CHECK(chords == 1);
        CHECK(others == 0);
    }
    {   // Shift pressed AFTER the button is not a chord.
        Button b; uint32_t now = 1000; b.begin(false, now);
        for (int i = 0; i < 40; ++i, ++now) b.update(true, now, false);
        unsigned chords = 0;
        for (int i = 0; i < 40; ++i, ++now) chords += b.update(true, now, true) == Event::ChordPress;
        CHECK(chords == 0);
    }
    {   // Held through reset: no phantom press or tap.
        Button b; uint32_t now = 1000; b.begin(true, now);
        Event any = Event::None;
        for (int i = 0; i < 1500; ++i, ++now) { const Event r = b.update(true, now, false); if (r != Event::None) any = r; }
        for (int i = 0; i < 100; ++i, ++now) { const Event r = b.update(false, now, false); if (r != Event::None) any = r; }
        CHECK(any == Event::None);
    }
}

TEST_CASE("The Loop Settings page opens on the Shift chord and closes on Shift", "[loop][loop_page]")
{
    using LoopPage::Controls;
    constexpr uint8_t kShift = Controls::kShift;
    Controls page;
    // Ordinary traffic is not ours.
    CHECK_FALSE(page.poll(0, 0, false, false, true).consumed);
    CHECK_FALSE(page.poll(kShift, 0, false, false, true).consumed);
    // The chord only opens when allowed.
    CHECK_FALSE(page.poll(kShift, 0, true, true, false).consumed);
    CHECK_FALSE(page.active);
    auto in = page.poll(kShift, 0, true, true, true);
    CHECK(in.open);
    CHECK(in.consumed);
    CHECK(page.active);
    CHECK(page.waitRelease);
    // The opening fingers stay down: still consumed, page waits.
    CHECK(page.poll(kShift, 0, true, false, true).consumed);
    CHECK(page.waitRelease);
    CHECK(page.poll(0, 0, true, false, true).consumed);
    CHECK(page.waitRelease);               // the loop button is still down
    CHECK(page.poll(0, 0, false, false, true).consumed);
    CHECK_FALSE(page.waitRelease);
    // Voice and tile buttons are swallowed while the page is open.
    in = page.poll(0b00000101, 0b0011, false, false, true);
    CHECK(in.consumed);
    CHECK_FALSE(in.exit);
    page.poll(0, 0, false, false, true);
    // Shift leaves; its release tail is consumed.
    in = page.poll(kShift, 0, false, false, true);
    CHECK(in.exit);
    CHECK_FALSE(page.active);
    CHECK(page.waitRelease);
    CHECK(page.poll(kShift, 0, false, false, true).consumed);
    CHECK(page.poll(0, 0, false, false, true).consumed);
    CHECK_FALSE(page.waitRelease);
    CHECK_FALSE(page.poll(0, 0, false, false, true).consumed);
}

TEST_CASE("Opening the Loop page clears competing UI and blocks length holds", "[loop][loop_page][ui_transitions]")
{
    UIState ui;
    ui.arp.setActive(true);
    ui.settingsMode = ui.slideMode = ui.gateSeqLengthMode = true;
    ui.parameterButtonHeld[0] = true;
    ui.latchedParameter = 2;
    ui.selectedStepForEdit = 5;
    ui.padPressTimestamps[3] = 100;
    ui.voiceEnvelope.chordPending = true;
    ui.loopPage.lastControl = 2;
    UITransitions::openLoopPage(ui);
    CHECK_FALSE(ui.settingsMode);
    CHECK_FALSE(ui.slideMode);
    CHECK_FALSE(ui.gateSeqLengthMode);
    CHECK_FALSE(ui.parameterButtonHeld[0]);
    CHECK(ui.latchedParameter == -1);
    CHECK(ui.selectedStepForEdit == -1);
    CHECK(ui.padPressTimestamps[3] == 0);
    CHECK_FALSE(ui.voiceEnvelope.chordPending);
    CHECK(ui.loopPage.lastControl == 255);
    CHECK(ui.arp.active()); // the page is live: the arp (and the loop) keep running
    CHECK(ui.resetStepsLightsFlag);

    // A voice press while the page is open (or draining) cannot arm gate-length entry.
    for (const bool waiting : {false, true})
    {
        UIState state;
        state.loopPage.active = !waiting;
        state.loopPage.waitRelease = waiting;
        UITransitions::beginGateLengthHold(state, 0);
        CHECK(state.gateSeqLengthVoice == -1);
        state.loopPage = {};
        UITransitions::beginGateLengthHold(state, 0);
        REQUIRE(state.gateSeqLengthVoice == 0);
        state.loopPage.active = !waiting;
        state.loopPage.waitRelease = waiting;
        CHECK_FALSE(UITransitions::updateGateLengthHold(state, 0, true, 1000, 400));
        CHECK_FALSE(state.gateSeqLengthMode);
    }
    ui.loopPage.lastControl = 1;
    UITransitions::closeLoopPage(ui);
    CHECK(ui.loopPage.lastControl == 255);
}
