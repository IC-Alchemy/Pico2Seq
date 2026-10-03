#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "voice/LoopEngine.h"
#include "voice/LoopTiming.h"
#include "voice/VoiceManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

// The audio side of the looper: the 12-bit store, the single-head record/play/overdub
// machine, regen, tempo sync and the bus integration. Everything here runs the real
// processBlock() the way Core 1 does; the Core 0 policy is in test_loop_controller.cpp.

namespace {
using Catch::Approx;
constexpr float kSr = 48000.0f;
constexpr float kLsb = 1.0f / 1024.0f; // one 12-bit count

// A small buffer (8192 samples) keeps the tests fast; the engine is size-agnostic.
struct Rig
{
    std::vector<uint8_t> storage;
    LoopEngine loop;
    explicit Rig(size_t samples = 8192) : storage(samples / 2 * LoopEngine::kBytesPerPair, 0xA5)
    {
        loop.attach(storage.data(), storage.size(), kSr);
    }
    // Runs `input` through the engine in blocks of `block` frames and returns the output.
    std::vector<float> run(const std::vector<float> &input, uint32_t block = 256)
    {
        std::vector<float> out = input;
        size_t at = 0;
        while (at < out.size())
        {
            const uint32_t n = static_cast<uint32_t>(std::min<size_t>(block, out.size() - at));
            loop.processBlock(out.data() + at, n);
            at += n;
        }
        return out;
    }
    std::vector<float> silence(size_t frames, uint32_t block = 256) { return run(std::vector<float>(frames, 0.0f), block); }
};

std::vector<float> sine(size_t frames, float amplitude, float hz)
{
    std::vector<float> v(frames);
    for (size_t i = 0; i < frames; ++i)
        v[i] = amplitude * std::sin(6.2831853f * hz * static_cast<float>(i) / kSr);
    return v;
}

float maxAbs(const std::vector<float> &v, size_t from = 0, size_t to = SIZE_MAX)
{
    float peak = 0.0f;
    for (size_t i = from; i < std::min(to, v.size()); ++i)
        peak = std::max(peak, std::fabs(v[i]));
    return peak;
}
} // namespace

// --- Storage format -------------------------------------------------------------

TEST_CASE("12-bit pack/unpack round-trips every value without touching neighbours", "[loop][loop_storage]")
{
    std::vector<uint8_t> buffer(3 * 8, 0);
    // Every representable value, at both parities, with the neighbours holding sentinels.
    for (int value = -2048; value <= 2047; ++value)
    {
        for (uint32_t index : {0u, 1u, 6u, 7u})
        {
            for (uint32_t i = 0; i < 16; ++i)
                LoopEngine::pack(buffer.data(), i, (i % 2) ? 1234 : -777);
            LoopEngine::pack(buffer.data(), index, value);
            REQUIRE(LoopEngine::unpack(buffer.data(), index) == value);
            for (uint32_t i = 0; i < 16; ++i)
                if (i != index)
                    REQUIRE(LoopEngine::unpack(buffer.data(), i) == ((i % 2) ? 1234 : -777));
        }
    }
}

TEST_CASE("Three bytes hold exactly two samples", "[loop][loop_storage]")
{
    std::vector<uint8_t> buffer(3, 0);
    LoopEngine::pack(buffer.data(), 0, 0x7FF);
    LoopEngine::pack(buffer.data(), 1, -2048);
    CHECK(buffer[0] == 0xFF);
    CHECK((buffer[1] & 0x0F) == 0x07);
    CHECK((buffer[1] >> 4) == 0x00);
    CHECK(buffer[2] == 0x80);
}

TEST_CASE("Loop buffer sizing respects the heap reserve and whole pairs", "[loop][loop_storage]")
{
    constexpr size_t kReserve = 40 * 1024;
    CHECK(LoopEngine::planBufferBytes(500000, kReserve, 120 * 1024) == 120u * 1024u);
    // Not enough for the wish: take what is left after the reserve, in whole pairs.
    const size_t shrunk = LoopEngine::planBufferBytes(100000, kReserve, 120 * 1024);
    CHECK(shrunk == (100000 - kReserve) / 3 * 3);
    CHECK(shrunk % LoopEngine::kBytesPerPair == 0);
    // At or under the reserve, or too small to be worth it: none.
    CHECK(LoopEngine::planBufferBytes(kReserve, kReserve, 120 * 1024) == 0);
    CHECK(LoopEngine::planBufferBytes(10000, kReserve, 120 * 1024) == 0);
    CHECK(LoopEngine::planBufferBytes(kReserve + 100, kReserve, 120 * 1024) == 0);
    // The default budget is 64 KiB: 43,690 samples at 12 bits. Layers mix into the same loop,
    // so this is all the memory any number of layers will ever use.
    CHECK(LoopEngine::kDefaultBufferBytes == 64u * 1024u);
    CHECK(LoopEngine::kDefaultBufferBytes / LoopEngine::kBytesPerPair * 2 == 43690);
}

// --- Pass-through ---------------------------------------------------------------

TEST_CASE("An idle looper at unity leaves the bus bit-for-bit untouched", "[loop]")
{
    Rig rig;
    const auto in = sine(1000, 0.7f, 220.0f);
    const auto out = rig.run(in);
    CHECK(std::memcmp(in.data(), out.data(), in.size() * sizeof(float)) == 0);
    CHECK(rig.loop.state() == LoopEngine::State::Empty);

    LoopEngine unattached; // no buffer: Disabled, still a clean pass-through
    std::vector<float> copy = in;
    unattached.processBlock(copy.data(), static_cast<uint32_t>(copy.size()));
    CHECK(std::memcmp(in.data(), copy.data(), in.size() * sizeof(float)) == 0);
    CHECK(unattached.state() == LoopEngine::State::Disabled);
    CHECK(unattached.postRecord(100)); // commands queue harmlessly...
    unattached.processBlock(copy.data(), 64); // ...and are ignored without a buffer
    CHECK(unattached.state() == LoopEngine::State::Disabled);
}

TEST_CASE("Sequencer volume scales the live bus and eases without a step", "[loop][loop_mix]")
{
    Rig rig;
    rig.loop.setSequencerVolume(0.5f);
    const auto out = rig.run(std::vector<float>(24000, 1.0f)); // ~33 time constants
    CHECK(out.back() == Approx(0.5f).margin(1e-5f));
    // Ramped over ~15 ms time constant: the first sample is nowhere near the target.
    CHECK(out.front() > 0.9f);
    float biggestStep = 0.0f;
    for (size_t i = 1; i < out.size(); ++i)
        biggestStep = std::max(biggestStep, std::fabs(out[i] - out[i - 1]));
    CHECK(biggestStep < 0.01f);
    // Back to unity settles exactly, restoring the untouched pass-through.
    rig.loop.setSequencerVolume(1.0f);
    rig.run(std::vector<float>(9600, 0.0f));
    const auto in = sine(256, 0.3f, 300.0f);
    CHECK(rig.run(in) == in);
}

// --- Record then play -----------------------------------------------------------

TEST_CASE("A take records exactly one period and then plays it back in place", "[loop][loop_record]")
{
    Rig rig;
    constexpr uint32_t L = 4800; // a tenth of a second at the full 48 kHz store rate
    const auto tone = sine(L, 0.5f, 440.0f);

    REQUIRE(rig.loop.postRecord(L));
    auto out = rig.run(tone);
    // While recording the loop is silent: the bus passes through.
    CHECK(out == tone);
    CHECK(rig.loop.state() == LoopEngine::State::Playing); // switched on frame L, not before
    CHECK(rig.loop.storedSamples() == L);
    CHECK(rig.loop.takesStarted() == 1);

    // The next L frames are the take, 12-bit accurate, with only the seam faded.
    const auto played = rig.silence(L);
    const size_t edge = 48 + 2; // 1 ms seam fade plus a little margin
    for (size_t i = edge; i < L - edge; ++i)
        REQUIRE(std::fabs(played[i] - tone[i]) <= 0.51f * kLsb + 1e-6f);
    CHECK(std::fabs(played[0]) < 0.02f);       // faded in
    CHECK(std::fabs(played[L - 1]) < 0.02f);   // faded out

    // And it keeps going: a second pass is the same audio (regen at 100%).
    const auto again = rig.silence(L);
    for (size_t i = 0; i < L; ++i)
        REQUIRE(again[i] == Approx(played[i]).margin(1e-6f));
    CHECK(rig.loop.state() == LoopEngine::State::Playing);
}

TEST_CASE("Recording ends on exactly the loop's last frame at any block size", "[loop][loop_record]")
{
    for (uint32_t block : {1u, 7u, 32u, 33u, 256u, 513u})
    {
        CAPTURE(block);
        Rig rig;
        constexpr uint32_t L = 3000;
        REQUIRE(rig.loop.postRecord(L));
        std::vector<float> ones(L - 1, 0.25f);
        rig.run(ones, block);
        rig.loop.processBlock(ones.data(), 0); // no-op call
        CHECK(rig.loop.state() == LoopEngine::State::Recording); // one frame short
        std::vector<float> last(1, 0.25f);
        rig.run(last, block);
        CHECK(rig.loop.state() == LoopEngine::State::Playing);
    }
}

TEST_CASE("The store rate follows the loop when it cannot fit at 48 kHz", "[loop][loop_record]")
{
    Rig rig(2048); // 2048 stored samples
    constexpr uint32_t L = 8192; // 4:1 decimation
    // A step: 0.5 for the first half of the loop, 0 for the second.
    std::vector<float> step(L, 0.0f);
    std::fill(step.begin(), step.begin() + L / 2, 0.5f);

    REQUIRE(rig.loop.postRecord(L));
    rig.run(step);
    CHECK(rig.loop.state() == LoopEngine::State::Playing);
    CHECK(rig.loop.storedSamples() == 2048);

    const auto pass = rig.silence(L);
    // The level sits at 0.5, then 0, and the change lands at the middle of the pass
    // to within a few stored samples (4 frames each) plus the playback smoothing.
    CHECK(pass[L / 4] == Approx(0.5f).margin(0.02f));
    CHECK(pass[3 * L / 4] == Approx(0.0f).margin(0.02f));
    size_t crossing = 0;
    for (size_t i = 100; i < L; ++i)
        if (pass[i] < 0.25f) { crossing = i; break; }
    CHECK(std::llabs(static_cast<long long>(crossing) - static_cast<long long>(L / 2)) < 40);
    // The second pass restarts the step exactly one period after the first did.
    const auto second = rig.silence(L);
    size_t crossing2 = 0;
    for (size_t i = 100; i < L; ++i)
        if (second[i] < 0.25f) { crossing2 = i; break; }
    CHECK(crossing2 == crossing);
}

TEST_CASE("Loud takes saturate inside the stored range instead of wrapping", "[loop][loop_record]")
{
    Rig rig;
    constexpr uint32_t L = 3000;
    REQUIRE(rig.loop.postRecord(L));
    rig.run(std::vector<float>(L, 5.0f));
    const auto pass = rig.silence(L);
    CHECK(maxAbs(pass) <= LoopEngine::kFullScale);
    CHECK(pass[L / 2] > 1.9f);
    REQUIRE(rig.loop.postClear());
    rig.silence(64);
    REQUIRE(rig.loop.postRecord(L));
    rig.run(std::vector<float>(L, -5.0f));
    CHECK(rig.silence(L)[L / 2] < -1.9f);
}

TEST_CASE("The loop plays at the loop volume, and the take is unaffected by sequencer volume", "[loop][loop_mix]")
{
    Rig rig;
    constexpr uint32_t L = 3000;
    rig.loop.setSequencerVolume(0.25f);
    rig.silence(48000 / 4); // let the sequencer gain settle before the take
    REQUIRE(rig.loop.postRecord(L));
    // The recording tap sees the bus BEFORE the sequencer volume.
    const auto out = rig.run(std::vector<float>(L, 0.8f));
    CHECK(out[L - 1] == Approx(0.8f * 0.25f).margin(1e-4f)); // audible level follows the fader

    rig.loop.setLoopVolume(1.0f);
    const auto full = rig.silence(L);
    CHECK(full[L / 2] == Approx(0.8f).margin(2 * kLsb));
    rig.loop.setLoopVolume(0.5f);
    rig.silence(24000);
    const auto half = rig.silence(L);
    CHECK(half[L / 2] == Approx(0.4f).margin(2 * kLsb));
    rig.loop.setLoopVolume(0.0f);
    rig.silence(24000);
    CHECK(maxAbs(rig.silence(L)) < 1e-4f);
}

// --- Regen ----------------------------------------------------------------------

TEST_CASE("Regen is the level kept on each repeat; 100% never fades", "[loop][loop_regen]")
{
    constexpr uint32_t L = 3000;
    {
        Rig rig;
        rig.loop.setRegen(0.5f);
        REQUIRE(rig.loop.postRecord(L));
        rig.run(std::vector<float>(L, 0.8f));
        float expected = 1.0f;
        for (int pass = 0; pass < 4; ++pass)
        {
            CAPTURE(pass);
            const auto out = rig.silence(L);
            CHECK(out[L / 2] == Approx(0.8f * expected).margin(2 * kLsb));
            expected *= 0.5f;
        }
    }
    {
        Rig rig;
        rig.loop.setRegen(1.0f);
        REQUIRE(rig.loop.postRecord(L));
        rig.run(std::vector<float>(L, 0.8f));
        for (int pass = 0; pass < 25; ++pass)
            rig.silence(L);
        CHECK(rig.silence(L)[L / 2] == Approx(0.8f).margin(2 * kLsb)); // identical after 25 repeats
        CHECK(rig.loop.passGain() == 1.0f);
    }
}

TEST_CASE("A loop at the lowest regen fades out and the looper empties", "[loop][loop_regen]")
{
    constexpr uint32_t L = 2500;
    Rig rig;
    rig.loop.setRegen(0.0f); // clamps to the 10% floor
    CHECK(rig.loop.regen() == Approx(LoopEngine::kMinRegen));
    REQUIRE(rig.loop.postRecord(L));
    rig.run(std::vector<float>(L, 0.9f));
    for (int pass = 0; pass < 6; ++pass)
        rig.silence(L);
    CHECK(rig.loop.state() == LoopEngine::State::Empty);
    CHECK(maxAbs(rig.silence(L)) == 0.0f);
}

TEST_CASE("Regen is clamped to 10..100% and volumes to 0..1", "[loop][loop_regen]")
{
    LoopEngine loop;
    loop.setRegen(5.0f);
    CHECK(loop.regen() == 1.0f);
    loop.setRegen(-1.0f);
    CHECK(loop.regen() == Approx(0.10f));
    loop.setLoopVolume(2.0f);
    loop.setSequencerVolume(-3.0f);
    CHECK(loop.loopVolume() == 1.0f);
    CHECK(loop.sequencerVolume() == 0.0f);
}

// --- Overdub --------------------------------------------------------------------

TEST_CASE("A second take layers over the loop and bakes the heard level in", "[loop][loop_overdub]")
{
    constexpr uint32_t L = 3000;
    Rig rig;
    rig.loop.setRegen(0.5f);
    REQUIRE(rig.loop.postRecord(L));
    rig.run(std::vector<float>(L, 0.4f));
    rig.silence(L);                       // pass 1 at unity
    // The wrap into pass 2 halves the loop; overdub it from the top of that pass.
    REQUIRE(rig.loop.postRecord(L));
    const auto layered = rig.run(std::vector<float>(L, 0.2f));
    CHECK(rig.loop.state() == LoopEngine::State::Playing);
    // Heard during the overdub: the live bus plus the old loop at its current gain.
    CHECK(layered[L / 2] == Approx(0.2f + 0.4f * 0.5f).margin(0.01f));
    // Baked: old * heard-gain + new, and the next pass starts at unity again.
    CHECK(rig.loop.passGain() == 1.0f);
    CHECK(rig.silence(L)[L / 2] == Approx(0.4f * 0.5f + 0.2f).margin(3 * kLsb));
}

TEST_CASE("Overdub sums are bounded by the soft clip", "[loop][loop_overdub]")
{
    constexpr uint32_t L = 3000;
    Rig rig;
    REQUIRE(rig.loop.postRecord(L));
    rig.run(std::vector<float>(L, 1.5f));
    for (int take = 0; take < 4; ++take)
    {
        REQUIRE(rig.loop.postRecord(L));
        rig.run(std::vector<float>(L, 1.5f));
    }
    const auto out = rig.silence(L);
    CHECK(maxAbs(out) <= LoopEngine::kFullScale);
    CHECK(out[L / 2] > 1.8f); // piled up against the ceiling, not wrapped or hard-clipped
}

TEST_CASE("Cancel drops an unfinished first take and clear forgets the loop", "[loop][loop_record]")
{
    constexpr uint32_t L = 3000;
    Rig rig;
    REQUIRE(rig.loop.postRecord(L));
    rig.run(std::vector<float>(L / 2, 0.5f));
    CHECK(rig.loop.state() == LoopEngine::State::Recording);
    REQUIRE(rig.loop.postCancel());
    const auto in = sine(512, 0.3f, 200.0f);
    CHECK(rig.run(in) == in);
    CHECK(rig.loop.state() == LoopEngine::State::Empty);

    REQUIRE(rig.loop.postRecord(L));
    rig.run(std::vector<float>(L, 0.5f));
    CHECK(rig.loop.state() == LoopEngine::State::Playing);
    REQUIRE(rig.loop.postClear());
    CHECK(maxAbs(rig.silence(L)) < 1e-6f);
    CHECK(rig.loop.state() == LoopEngine::State::Empty);
    // A new take after a clear starts clean.
    REQUIRE(rig.loop.postRecord(L / 2));
    rig.run(std::vector<float>(L / 2, 0.1f));
    CHECK(rig.silence(L / 2)[L / 4] == Approx(0.1f).margin(2 * kLsb));
}

// --- Tempo and sync -------------------------------------------------------------

TEST_CASE("A tempo change varispeeds the loop so it still fits the step grid", "[loop][loop_sync]")
{
    constexpr uint32_t L = 4000;
    Rig rig;
    REQUIRE(rig.loop.postRecord(L));
    std::vector<float> step(L, 0.0f);
    std::fill(step.begin(), step.begin() + L / 2, 0.5f);
    rig.run(step);
    // Tempo doubles: one pass of the same loop now lasts L/2 frames.
    rig.loop.setPeriodFrames(L / 2);
    const auto pass = rig.silence(L); // two passes' worth of frames
    size_t falls = 0;
    for (size_t i = 1; i < pass.size(); ++i)
        if (pass[i - 1] > 0.25f && pass[i] <= 0.25f)
            ++falls;
    CHECK(falls == 2); // the step fell twice: the loop repeated twice in L frames
    // Back at the recorded tempo it repeats once per L again.
    rig.loop.setPeriodFrames(L);
    rig.silence(L);
    const auto settled = rig.silence(L);
    size_t fallsAgain = 0;
    for (size_t i = 1; i < settled.size(); ++i)
        if (settled[i - 1] > 0.25f && settled[i] <= 0.25f)
            ++fallsAgain;
    CHECK(fallsAgain == 1);
}

TEST_CASE("Sync leaves a head that is within a few ms of the seam alone", "[loop][loop_sync]")
{
    constexpr uint32_t L = 4800;
    Rig a, b;
    for (Rig *rig : {&a, &b})
    {
        REQUIRE(rig->loop.postRecord(L));
        rig->run(sine(L, 0.5f, 330.0f));
    }
    a.silence(L - 20);              // 20 frames before the seam
    b.silence(L - 20);
    REQUIRE(a.loop.postSync());     // a sync arriving 20 frames (0.4 ms) early
    const auto withSync = a.silence(2000);
    const auto without = b.silence(2000);
    for (size_t i = 0; i < withSync.size(); ++i)
        REQUIRE(withSync[i] == Approx(without[i]).margin(1e-6f));
}

TEST_CASE("Sync far from the seam snaps the loop back to the start with a crossfade", "[loop][loop_sync]")
{
    constexpr uint32_t L = 4800;
    Rig rig, reference;
    for (Rig *r : {&rig, &reference})
    {
        REQUIRE(r->loop.postRecord(L));
        r->run(sine(L, 0.5f, 330.0f));
    }
    rig.silence(L / 2);
    reference.silence(L / 2);
    REQUIRE(rig.loop.postSync());
    const auto snapped = rig.silence(L);
    const auto free = reference.silence(L);
    // After the crossfade the snapped loop is a fresh pass of the take, i.e. the take
    // replayed from its top, which is what an untouched engine plays right after recording.
    Rig truth;
    REQUIRE(truth.loop.postRecord(L));
    truth.run(sine(L, 0.5f, 330.0f));
    const auto expected = truth.silence(L);
    for (size_t i = 128; i < L - 128; ++i)
        REQUIRE(snapped[i] == Approx(expected[i]).margin(1e-5f));
    // The free-running reference was mid-pass, so it differs from the snapped one.
    float difference = 0.0f;
    for (size_t i = 128; i < 1000; ++i)
        difference = std::max(difference, std::fabs(snapped[i] - free[i]));
    CHECK(difference > 0.1f);
    // Crossfaded: no jump larger than the signal's own slew across the snap.
    float biggestStep = 0.0f;
    for (size_t i = 1; i < 200; ++i)
        biggestStep = std::max(biggestStep, std::fabs(snapped[i] - snapped[i - 1]));
    CHECK(biggestStep < 0.2f);
}

TEST_CASE("Restart replays the loop from its top", "[loop][loop_sync]")
{
    constexpr uint32_t L = 4800;
    Rig rig;
    REQUIRE(rig.loop.postRecord(L));
    rig.run(sine(L, 0.5f, 330.0f));
    const auto first = rig.silence(L);
    rig.silence(1234);
    REQUIRE(rig.loop.postRestart());
    const auto restarted = rig.silence(L);
    for (size_t i = 128; i < L - 128; ++i)
        REQUIRE(restarted[i] == Approx(first[i]).margin(1e-5f));
}

TEST_CASE("The seam fade and position report", "[loop][loop_record]")
{
    constexpr uint32_t L = 4800;
    Rig rig;
    CHECK(rig.loop.position() == 0.0f);
    REQUIRE(rig.loop.postRecord(L));
    rig.run(std::vector<float>(L / 2, 0.5f));
    CHECK(rig.loop.position() == Approx(0.5f).margin(0.01f));
    rig.run(std::vector<float>(L / 2, 0.5f));
    rig.silence(L / 4);
    CHECK(rig.loop.position() == Approx(0.25f).margin(0.01f));
}

TEST_CASE("The command ring reports a full queue instead of dropping", "[loop]")
{
    LoopEngine loop;
    unsigned accepted = 0;
    while (loop.postSync() && accepted < 100)
        ++accepted;
    CHECK(accepted == 8);
    CHECK_FALSE(loop.postClear());
}

// --- Layering: many passes mixed into one small buffer -------------------------

TEST_CASE("Layers mix into the same buffer: any number of passes, no more memory", "[loop][loop_layers]")
{
    constexpr uint32_t L = 3000;
    Rig rig;
    rig.loop.setRegen(1.0f);
    REQUIRE(rig.loop.postRecord(L));
    rig.run(std::vector<float>(L, 0.2f));
    const uint32_t stored = rig.loop.storedSamples();
    const uint32_t capacity = rig.loop.capacitySamples();
    // Three further passes, each started the instant the last one ends: tap, tap, tap.
    for (int layer = 1; layer <= 3; ++layer)
    {
        CAPTURE(layer);
        REQUIRE(rig.loop.postRecord(L));
        const auto heard = rig.run(std::vector<float>(L, 0.2f));
        // While layering you hear the live bus plus everything laid down so far.
        CHECK(heard[L / 2] == Approx(0.2f * static_cast<float>(layer + 1)).margin(4 * kLsb));
    }
    CHECK(rig.loop.audioState() == LoopEngine::State::Playing);
    // Four takes of 0.2 sum to 0.8 in the one loop, and the buffer did not grow.
    CHECK(rig.silence(L)[L / 2] == Approx(0.8f).margin(4 * kLsb));
    CHECK(rig.loop.storedSamples() == stored);
    CHECK(rig.loop.capacitySamples() == capacity);
}

TEST_CASE("With regen below 100% each layer fades what was there, so layers settle instead of piling up", "[loop][loop_layers]")
{
    constexpr uint32_t L = 3000;
    Rig rig;
    rig.loop.setRegen(0.5f);
    REQUIRE(rig.loop.postRecord(L));
    rig.run(std::vector<float>(L, 0.2f));
    // Each layer is a repeat: stored = stored * 0.5 + live. 0.2 -> 0.3 -> 0.35 -> ... -> 0.4.
    float expected = 0.2f;
    for (int layer = 1; layer <= 6; ++layer)
    {
        REQUIRE(rig.loop.postRecord(L));
        rig.run(std::vector<float>(L, 0.2f));
        expected = expected * 0.5f + 0.2f;
    }
    CHECK(rig.silence(L)[L / 2] == Approx(expected).margin(0.01f));
    CHECK(expected == Approx(0.4f).margin(0.01f));
}

TEST_CASE("A tap during a pass queues a layer that follows it with no plain repeat between", "[loop][loop_layers]")
{
    constexpr uint32_t L = 3000;
    Rig rig;
    REQUIRE(rig.loop.postRecord(L));
    rig.run(std::vector<float>(L / 2, 0.2f));
    CHECK(rig.loop.audioState() == LoopEngine::State::Recording);
    // Halfway through the first take two requests arrive: the first is queued, and the second,
    // being for the same hand-over, is the same request.
    REQUIRE(rig.loop.postRecord(L));
    REQUIRE(rig.loop.postRecord(L));
    rig.run(std::vector<float>(L / 2 - 1, 0.2f));
    CHECK(rig.loop.audioState() == LoopEngine::State::Recording);   // one frame short of the seam
    CHECK(rig.loop.takesStarted() == 2);                            // counted once
    rig.run(std::vector<float>(1, 0.2f));
    CHECK(rig.loop.audioState() == LoopEngine::State::Overdubbing); // straight into the layer
    // Mid-way through that layer another is queued behind it.
    rig.run(std::vector<float>(L / 2, 0.2f));
    REQUIRE(rig.loop.postRecord(L));
    rig.run(std::vector<float>(L / 2 - 1, 0.2f));
    CHECK(rig.loop.audioState() == LoopEngine::State::Overdubbing);
    rig.run(std::vector<float>(1, 0.2f));
    CHECK(rig.loop.audioState() == LoopEngine::State::Overdubbing); // a second layer, chained
    CHECK(rig.loop.takesStarted() == 3);
    // Nothing is queued behind the last one: it ends in plain playback.
    rig.run(std::vector<float>(L - 1, 0.2f));
    CHECK(rig.loop.audioState() == LoopEngine::State::Overdubbing);
    rig.run(std::vector<float>(1, 0.2f));
    CHECK(rig.loop.audioState() == LoopEngine::State::Playing);
    // Three passes of 0.2 are in the loop.
    CHECK(rig.silence(L)[L / 2] == Approx(0.6f).margin(4 * kLsb));
}

TEST_CASE("The layer's mix level does not depend on which side of the seam the command fell", "[loop][loop_layers]")
{
    constexpr uint32_t L = 3000;
    // A layer is a repeat, so it mixes the old loop in at regen (0.5 here) however the request
    // lands. `requestFrame` counts frames from the start of the take: L +- 20 is either side of
    // the seam that ends the take (a request before it is queued behind the take, one after it
    // meets a loop that has just been written), and 2L +- 20 is either side of the seam that
    // ends one plain repeat. All four must lay down the same sum.
    for (const uint32_t requestFrame : {L - 20, L + 20, 2 * L - 20, 2 * L + 20})
    {
        CAPTURE(requestFrame);
        Rig rig;
        rig.loop.setRegen(0.5f);
        REQUIRE(rig.loop.postRecord(L));
        std::vector<float> before(requestFrame);
        for (uint32_t f = 0; f < requestFrame; ++f)
            before[f] = f < L ? 0.8f : 0.0f;                 // the live bus: a take of 0.8, then quiet
        rig.run(before);
        REQUIRE(rig.loop.postRecord(L));
        // The rest of the take (if the request beat its end), then the layer pass of 0.2.
        const uint32_t takeLeft = requestFrame < L ? L - requestFrame : 0;
        std::vector<float> rest(takeLeft + L, 0.2f);
        std::fill(rest.begin(), rest.begin() + takeLeft, 0.8f);
        const auto heard = rig.run(rest);
        // The old loop at 0.5 under the live 0.2.
        CHECK(heard[takeLeft + L / 2] == Approx(0.2f + 0.8f * 0.5f).margin(0.02f));
        CHECK(rig.loop.audioState() == LoopEngine::State::Playing);
        CHECK(rig.loop.passGain() == 1.0f);                  // baked: the next repeat is unity again
        CHECK(rig.silence(L)[L / 2] == Approx(0.6f).margin(0.02f));
    }
}

TEST_CASE("Clearing drops a queued layer, so the next take is a plain take", "[loop][loop_layers]")
{
    constexpr uint32_t L = 3000;
    Rig rig;
    REQUIRE(rig.loop.postRecord(L));
    rig.run(std::vector<float>(L / 2, 0.3f));
    REQUIRE(rig.loop.postRecord(L)); // queued behind the take
    REQUIRE(rig.loop.postClear());
    rig.silence(64);
    CHECK(rig.loop.audioState() == LoopEngine::State::Empty);
    REQUIRE(rig.loop.postRecord(L));
    rig.run(std::vector<float>(L, 0.3f));
    CHECK(rig.loop.audioState() == LoopEngine::State::Playing); // not Overdubbing: the queue was dropped
}

// --- Timing helpers -------------------------------------------------------------

TEST_CASE("Loop sizes and step timing", "[loop][loop_timing]")
{
    // A bar is the longest loop.
    CHECK(LoopTiming::kSizeCount == 3);
    CHECK(LoopTiming::kMaxSteps == 16);
    const uint8_t expected[3] = {4, 8, 16};
    for (uint8_t i = 0; i < 3; ++i)
    {
        CHECK(LoopTiming::stepsForIndex(i) == expected[i]);
        CHECK(LoopTiming::indexForSteps(expected[i]) == i);
    }
    CHECK(LoopTiming::stepsForIndex(9) == 16);       // out of range clamps to the longest
    CHECK(LoopTiming::indexForSteps(5) == LoopTiming::kDefaultSizeIndex);
    CHECK(LoopTiming::indexForSteps(32) == LoopTiming::kDefaultSizeIndex); // no longer a size
    CHECK(LoopTiming::indexForSteps(64) == LoopTiming::kDefaultSizeIndex);
    // A step is a sixteenth: 120 BPM = 0.125 s = 6000 frames; a bar is 96,000 frames.
    CHECK(LoopTiming::loopFrames(1, 120.0f, kSr) == 6000);
    CHECK(LoopTiming::loopFrames(16, 120.0f, kSr) == 96000);
    CHECK(LoopTiming::loopFrames(16, 90.0f, kSr) == 128000);
    // Hostile tempos clamp instead of dividing by zero or overflowing.
    CHECK(LoopTiming::loopFrames(16, 0.0f, kSr) == LoopTiming::loopFrames(16, LoopTiming::kMinBpm, kSr));
    CHECK(LoopTiming::loopFrames(16, -50.0f, kSr) > 0);
    CHECK(LoopTiming::loopFrames(16, 1.0e9f, kSr) == LoopTiming::loopFrames(16, LoopTiming::kMaxBpm, kSr));
    CHECK(LoopTiming::loopFrames(16, 20.0f, kSr) == 576000);
    CHECK(LoopTiming::loopFrames(16, 20.0f, kSr) <= LoopTiming::kMaxFrames);
    // A take starts on a loop boundary, so the longest wait is one loop (at most a bar).
    CHECK(LoopTiming::quantizeSteps(4) == 4);
    CHECK(LoopTiming::quantizeSteps(8) == 8);
    CHECK(LoopTiming::quantizeSteps(16) == 16);
    CHECK(LoopTiming::quantizeSteps(64) == 16); // never longer than a bar
    CHECK(LoopTiming::quantizeSteps(0) == 1);
}

// --- On the master bus ----------------------------------------------------------

namespace {
VoiceConfig sinePatch()
{
    VoiceConfig config;
    config.oscillatorCount = 1;
    config.oscWaveforms[0] = WAVE_SIN;
    config.hasEnvelope = false;
    config.hasFilter = false;
    return config;
}

VoiceState heldNote()
{
    VoiceState state;
    state.noteIndex = 24.0f;
    state.velocityLevel = 1.0f;
    state.isGateHigh = true;
    state.shouldRetrigger = true;
    return state;
}

float busPeak(VoiceManager &manager, uint32_t frames)
{
    std::array<float, 256> block{};
    float peak = 0.0f;
    while (frames)
    {
        const uint32_t n = std::min<uint32_t>(frames, 256);
        manager.processBlock(block.data(), n);
        for (uint32_t i = 0; i < n; ++i)
            peak = std::max(peak, std::fabs(block[i]));
        frames -= n;
    }
    return peak;
}
} // namespace

TEST_CASE("The master bus records a voice and keeps playing it after the voice stops", "[loop][loop_bus]")
{
    auto manager = std::make_unique<VoiceManager>(1);
    const uint8_t id = manager->addVoice(sinePatch());
    manager->init(kSr);
    manager->setGlobalVolume(1.0f);
    REQUIRE(manager->allocateLoopBuffer(1000000, 3 * 2048, 0));
    CHECK(manager->loopBufferBytes() == 3 * 2048);
    CHECK(manager->loop().state() == LoopEngine::State::Empty);

    manager->updateVoiceState(id, heldNote());
    busPeak(*manager, 4800); // let the voice and the master gain come up
    constexpr uint32_t L = 2048;
    REQUIRE(manager->loop().postRecord(L));
    CHECK(busPeak(*manager, L) > 0.01f);
    CHECK(manager->loop().state() == LoopEngine::State::Playing);

    // Silence the voice: only the loop remains.
    manager->disableVoice(id);
    busPeak(*manager, 256); // drain the queued disable
    busPeak(*manager, 4800);
    CHECK(busPeak(*manager, 3 * L) > 0.01f);

    // Sequencer volume at zero mutes the live voice but not the loop.
    manager->enableVoice(id);
    manager->loop().setSequencerVolume(0.0f);
    busPeak(*manager, 9600);
    manager->loop().setLoopVolume(0.0f);
    busPeak(*manager, 9600);
    CHECK(busPeak(*manager, 2048) < 1e-4f);
}

TEST_CASE("Allocating the loop buffer is refused cleanly when the heap is short", "[loop][loop_bus]")
{
    auto manager = std::make_unique<VoiceManager>(1);
    manager->init(kSr);
    CHECK_FALSE(manager->allocateLoopBuffer(10000, 120 * 1024, 40 * 1024));
    CHECK(manager->loopBufferBytes() == 0);
    CHECK(manager->loop().state() == LoopEngine::State::Disabled);
    // The bus is unaffected.
    std::array<float, 64> block{};
    manager->processBlock(block.data(), 64);
    // A later, sufficient request attaches; a repeated one keeps the first buffer.
    CHECK(manager->allocateLoopBuffer(300000, 3 * 4096, 0));
    CHECK(manager->allocateLoopBuffer(300000, 3 * 8192, 0));
    CHECK(manager->loopBufferBytes() == 3 * 4096);
}
