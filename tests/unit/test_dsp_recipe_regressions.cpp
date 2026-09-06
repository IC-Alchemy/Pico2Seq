#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <rpdsp/DSPFunctions.h>
#include <rpdsp/analog_adsr.h>
#include <rpdsp/frequency_shifter.h>
#include <rpdsp/tape_delay.h>
#include <array>
#include <cmath>

using namespace Catch::Matchers;

namespace {
constexpr double pi = 3.14159265358979323846;
}

TEST_CASE("Analog ADSR gate-off interrupts attack and allows retrigger", "[rpdsp][recipe_regression]") {
    std::array<float, 3> state{};
    float level = 0.0f;
    for (int i = 0; i < 10; ++i)
        level = rpdsp::adsr_analog(1.0f, 0.001f, 0.01f, 0.6f, 0.25f, state.data());
    REQUIRE(level > 0.0f);
    REQUIRE(level < 0.1f);
    const float released = rpdsp::adsr_analog(0.0f, 0.001f, 0.01f, 0.6f, 0.25f, state.data());
    REQUIRE_THAT(released, WithinAbs(level * 0.75f, 1e-7));
    REQUIRE(state[2] == 0.0f);
    REQUIRE(rpdsp::adsr_analog(1.0f, 0.001f, 0.01f, 0.6f, 0.25f, state.data()) > released);
    REQUIRE(rpdsp::adsr_analog(0.0f, 0.001f, 0.01f, 0.6f, 1.0f, state.data()) == 0.0f);

    rpdsp::AnalogAdsr envelope;
    envelope.prepare(48000.0f);
    envelope.setAttackCoefficient(0.001f);
    envelope.setReleaseCoefficient(1.0f);
    envelope.noteOn();
    REQUIRE(envelope.process() > 0.0f);
    envelope.noteOff();
    REQUIRE(envelope.process() == 0.0f);
}

TEST_CASE("Analog ADSR coefficient endpoints hold or complete safely", "[rpdsp][recipe_regression]") {
    std::array<float, 3> state{};
    REQUIRE(rpdsp::adsr_analog(1, 0, 0, 0.5f, 0, state.data()) == 0.0f);
    REQUIRE(rpdsp::adsr_analog(1, 2, 1, 0.5f, 0, state.data()) == 1.0f);
    REQUIRE(rpdsp::adsr_analog(1, 2, 1, 0.5f, 0, state.data()) == 0.5f);
    REQUIRE(rpdsp::adsr_analog(0, 2, 1, 0.5f, -1, state.data()) == 0.5f);
    REQUIRE(rpdsp::adsr_analog(0, 2, 1, 0.5f, 2, state.data()) == 0.0f);
}

TEST_CASE("Analog ADSR note events retrigger while a held gate sustains", "[rpdsp][recipe_regression]") {
    rpdsp::AnalogAdsr envelope;
    envelope.prepare(48000.0f);
    envelope.setAttackCoefficient(0.1f);
    envelope.setDecayCoefficient(1.0f);
    envelope.setSustain(0.4f);
    envelope.noteOn();
    for (int i = 0; i < 64; ++i) envelope.process(true);
    REQUIRE_THAT(envelope.process(true), WithinAbs(0.4, 1e-7));
    envelope.trigger();
    REQUIRE_THAT(envelope.process(), WithinAbs(0.49, 1e-7));
    for (int i = 0; i < 64; ++i) envelope.process();
    REQUIRE_THAT(envelope.process(), WithinAbs(0.4, 1e-7));
    envelope.noteOn();
    REQUIRE_THAT(envelope.process(), WithinAbs(0.49, 1e-7));
}

TEST_CASE("Analog ADSR seconds survive prepare and sample-rate changes", "[rpdsp][recipe_regression]") {
    rpdsp::AnalogAdsr envelope;
    // Configure all time constants before the first prepare().
    envelope.set(0.02f, 0.03f, 0.5f, 0.04f);
    for (float fs : {24000.0f, 48000.0f, 96000.0f}) {
        CAPTURE(fs);
        envelope.prepare(fs);
        envelope.noteOn();
        float level = 0.0f;
        for (int i = 0; i < std::lround(fs * 0.02f); ++i) level = envelope.process();
        REQUIRE_THAT(level, WithinAbs(1.3 * (1.0 - std::exp(-1.0)), 5e-5));
        // Finish the attack, then measure one decay time constant from its peak.
        int remaining = static_cast<int>(fs);
        while (level < 1.0f && remaining-- > 0) level = envelope.process();
        REQUIRE(level == 1.0f);
        for (int i = 0; i < std::lround(fs * 0.03f); ++i) level = envelope.process();
        REQUIRE_THAT(level, WithinAbs(0.5 + 0.5 * std::exp(-1.0), 5e-5));
        const float beforeRelease = level;
        envelope.noteOff();
        for (int i = 0; i < std::lround(fs * 0.04f); ++i) level = envelope.process();
        REQUIRE_THAT(level, WithinAbs(beforeRelease * std::exp(-1.0), 5e-5));
    }
}

TEST_CASE("Analog ADSR coefficient setters override only their own stage timing", "[rpdsp][recipe_regression]") {
    rpdsp::AnalogAdsr envelope;
    envelope.set(0.02f, 0.03f, 0.4f, 0.04f);
    envelope.setAttackCoefficient(1.0f);
    envelope.setReleaseCoefficient(0.25f);
    for (float fs : {24000.0f, 96000.0f}) {
        CAPTURE(fs);
        envelope.prepare(fs);
        envelope.noteOn();
        REQUIRE(envelope.process() == 1.0f);
        float level = 1.0f;
        for (int i = 0; i < std::lround(fs * 0.03f); ++i) level = envelope.process();
        REQUIRE_THAT(level, WithinAbs(0.4 + 0.6 * std::exp(-1.0), 5e-5));
        envelope.noteOff();
        REQUIRE_THAT(envelope.process(), WithinAbs(level * 0.75f, 1e-7));
    }
    envelope.setDecayCoefficient(0.5f);
    envelope.prepare(48000.0f);
    envelope.noteOn();
    REQUIRE(envelope.process() == 1.0f);
    REQUIRE_THAT(envelope.process(), WithinAbs(0.7, 1e-7));
    // Switching back to seconds must re-enable time-based conversion.
    envelope.setAttackSeconds(0.02f);
    envelope.prepare(96000.0f);
    envelope.noteOn();
    REQUIRE_THAT(envelope.process(), WithinAbs(1.3 * -std::expm1(-1.0 / 1920.0), 1e-8));
}

TEST_CASE("Analog ADSR long time constants retain a nonzero coefficient", "[rpdsp][recipe_regression]") {
    rpdsp::AnalogAdsr envelope;
    envelope.setAttackSeconds(1000.0f);
    envelope.prepare(96000.0f);
    envelope.noteOn();
    const float first = envelope.process();
    REQUIRE(first > 0.0f);
    REQUIRE_THAT(first, WithinAbs(1.3 * -std::expm1(-1.0 / 96000000.0), 1e-14));
    REQUIRE(envelope.process() > first);
}

TEST_CASE("Analog ADSR reset retains instantaneous time settings", "[rpdsp][recipe_regression]") {
    rpdsp::AnalogAdsr envelope;
    envelope.set(-1.0f, 0.0f, 0.3f, -1.0f);
    envelope.prepare(96000.0f);
    for (int repetition = 0; repetition < 2; ++repetition) {
        envelope.reset();
        REQUIRE(envelope.process() == 0.0f);
        envelope.noteOn();
        REQUIRE(envelope.process() == 1.0f);
        REQUIRE_THAT(envelope.process(), WithinAbs(0.3, 1e-7));
        envelope.noteOff();
        REQUIRE(envelope.process() == 0.0f);
    }
}

TEST_CASE("Recipe one-pole coefficients preserve elapsed-time decay", "[rpdsp][recipe_regression]") {
    for (float fs : {22050.0f, 44100.0f, 48000.0f, 96000.0f}) {
        CAPTURE(fs);
        const auto comp = rpdsp::make_comp_feedback_coefficients(fs);
        const auto tape = rpdsp::make_delay_tape_coefficients(fs);
        const float scaled[] = {comp.detector, comp.attack, comp.releaseFast, comp.releaseSlow, tape.oxide};
        const float base[] = {0.0005f, 0.05f, 0.004f, 0.001f, 0.35f};
        for (int i = 0; i < 5; ++i) {
            const double reference = std::pow(1.0 - base[i], 48.0);
            const double actual = std::pow(1.0 - scaled[i], fs * 0.001);
            REQUIRE_THAT(actual, WithinAbs(reference, 2e-6));
        }
        REQUIRE_THAT(tape.wowInc * fs, WithinAbs(0.8, 1e-6));
        REQUIRE_THAT(tape.flutterInc * fs, WithinAbs(6.3, 1e-6));
        REQUIRE(rpdsp::recipe_rate_at_sample_rate(0.0f, fs) == 0.0f);
        REQUIRE(rpdsp::recipe_rate_at_sample_rate(1.0f, fs) == 1.0f);
    }
}

TEST_CASE("Compressor dynamics agree across sample rates", "[rpdsp][recipe_regression]") {
    float reference = 0.0f;
    for (float fs : {48000.0f, 44100.0f, 96000.0f}) {
        CAPTURE(fs);
        const auto coeff = rpdsp::make_comp_feedback_coefficients(fs);
        std::array<float, 2> state{};
        for (int i = 0; i < static_cast<int>(fs * 0.1f); ++i)
            rpdsp::comp_feedback(0.9f, 0.3f, 0.5f, coeff, state.data());
        for (int i = 0; i < static_cast<int>(fs * 0.01f); ++i)
            rpdsp::comp_feedback(0.1f, 0.3f, 0.5f, coeff, state.data());
        if (fs == 48000.0f) reference = state[0];
        REQUIRE_THAT(state[0], WithinAbs(reference, 0.001));
    }
    REQUIRE(reference > 0.01f);
}

TEST_CASE("Vowel coefficient factory reaches endpoints and scales tuning", "[rpdsp][recipe_regression]") {
    const auto a = rpdsp::make_filt_vowel_coefficients(-1.0f);
    const auto u = rpdsp::make_filt_vowel_coefficients(4.0f);
    REQUIRE_THAT(a.first, WithinAbs(0.105, 1e-7));
    REQUIRE_THAT(u.first, WithinAbs(0.043, 1e-7));
    REQUIRE_THAT(u.second, WithinAbs(0.092, 1e-7));
    for (float fs : {22050.0f, 44100.0f, 96000.0f}) {
        for (float vowel : {0.0f, 1.25f, 4.0f}) {
            CAPTURE(fs, vowel);
            const auto coeff = rpdsp::make_filt_vowel_coefficients(vowel, fs);
            const auto base = rpdsp::make_filt_vowel_coefficients(vowel);
            REQUIRE_THAT(coeff.first * fs, WithinAbs(base.first * 48000.0f, 0.001));
            REQUIRE_THAT(coeff.second * fs, WithinAbs(base.second * 48000.0f, 0.001));
            std::array<float, 4> state{};
            for (int i = 0; i < 16384; ++i) {
                const float y = rpdsp::filt_vowel(i == 0 ? 1.0f : 0.0f, coeff, state.data());
                REQUIRE(std::isfinite(y));
            }
            for (float s : state) REQUIRE(std::fabs(s) < 0.001f);
        }
    }
}

TEST_CASE("Tape clamps both wow excursions at the actual buffer boundaries", "[rpdsp][recipe_regression]") {
    struct Setting { float delay, phase, expected; };
    // Frozen LFOs at +/- peak. Buffer slot i contains i+1, and write index is 0.
    for (const Setting test : {Setting{14, 0.25f, 3}, {14, 0.75f, 3},
                               {1, 0.25f, 16}, {1, 0.75f, 16},
                               {7, 0.25f, 4}, {7, 0.75f, 16}}) {
        CAPTURE(test.delay, test.phase);
        std::array<float, 18> buffer{};
        buffer.front() = buffer.back() = 12345.0f;
        for (int i = 0; i < 16; ++i) buffer[i + 1] = static_cast<float>(i + 1);
        std::array<float, 4> state{{test.phase, test.phase, 0.0f, 0.0f}};
        const rpdsp::TapeDelayCoefficients frozen{0, 0, 1};
        REQUIRE_THAT(rpdsp::delay_tape(0, buffer.data() + 1, 16, test.delay, 100, 0, frozen, state.data()),
                     WithinAbs(test.expected, 1e-6));
        REQUIRE(buffer.front() == 12345.0f);
        REQUIRE(buffer.back() == 12345.0f);
    }
}

TEST_CASE("Buffer recipes reject invalid lengths before touching pointers", "[rpdsp][recipe_regression]") {
    for (int n : {-1, 0, 1, 16777217}) {
        REQUIRE(rpdsp::delay_bbd(1, nullptr, n, 1, 0, nullptr) == 0.0f);
        REQUIRE(rpdsp::delay_tape(1, nullptr, n, 1, 0, 0, nullptr) == 0.0f);
        REQUIRE(rpdsp::fx_diffuse(0.25f, nullptr, n, 1, 1, nullptr) == 0.25f);
        REQUIRE(rpdsp::gran_cloud(1, nullptr, n, 1, nullptr, nullptr) == 0.0f);
    }
    REQUIRE(rpdsp::delay_tape(1, nullptr, 3, 1, 0, 0, nullptr) == 0.0f);
    REQUIRE(rpdsp::fx_diffuse(0.25f, nullptr, 2, 1, 1, nullptr) == 0.25f);
    REQUIRE(rpdsp::gran_cloud(1, nullptr, 3, 1, nullptr, nullptr) == 0.0f);
}

TEST_CASE("Tape remains bounded while delay and wow change aggressively", "[rpdsp][recipe_regression]") {
    for (int n : {4, 7, 64}) {
        CAPTURE(n);
        std::array<float, 66> buffer{};
        buffer[0] = buffer[n + 1] = 12345.0f;
        std::array<float, 6> state{};
        state.front() = state.back() = 6789.0f;
        for (int i = 0; i < 16384; ++i) {
            const float y = rpdsp::delay_tape(0.1f * std::sin(i * 0.1f), buffer.data() + 1,
                n, 80.0f * std::sin(i * 0.003f), 100.0f, 0.9f, state.data() + 1);
            REQUIRE(std::isfinite(y));
            REQUIRE(std::fabs(y) < 1.2f);
        }
        REQUIRE(buffer[0] == 12345.0f);
        REQUIRE(buffer[n + 1] == 12345.0f);
        REQUIRE(state.front() == 6789.0f);
        REQUIRE(state.back() == 6789.0f);
    }
}

TEST_CASE("Tape wrapper honors sample-rate timing and millisecond delay", "[rpdsp][recipe_regression]") {
    for (float fs : {24000.0f, 48000.0f, 96000.0f}) {
        CAPTURE(fs);
        rpdsp::TapeDelay<2048> delay;
        delay.prepare(fs);
        delay.setDelayMilliseconds(10.0f);
        delay.setFeedback(0.0f);
        std::array<float, 2048> buffer{};
        std::array<float, 4> state{};
        const auto coeff = rpdsp::make_delay_tape_coefficients(fs);
        for (int i = 0; i <= static_cast<int>(fs * 0.01f) + 1; ++i) {
            const float y = delay.process(i == 0 ? 1.0f : 0.0f);
            // This conversion lies just above an integer in float. At arrival,
            // the tiny negative read position must not round to buffer[n].
            const float direct = rpdsp::delay_tape(i == 0 ? 1.0f : 0.0f, buffer.data(), 2048,
                10.0f * 0.001f * fs, 0, 0, coeff, state.data());
            CAPTURE(i);
            const float expected = i == static_cast<int>(fs * 0.01f) ? 1.0f : 0.0f;
            REQUIRE_THAT(y, WithinAbs(expected, 1e-4));
            REQUIRE_THAT(direct, WithinAbs(expected, 1e-4));
        }
    }
}

TEST_CASE("Sine-based recipes match sine in either phase direction", "[rpdsp][recipe_regression]") {
    for (float direction : {-1.0f, 1.0f}) {
        std::array<float, 1> pd{};
        std::array<float, 3> fm{};
        std::array<float, 2> chaos{}, tz{}, dsf{};
        for (int i = 0; i < 2048; ++i) {
            const float inc = direction / 128.0f;
            const double expected = std::sin(2.0 * pi * direction * (i + 1) / 128.0);
            REQUIRE_THAT(rpdsp::osc_pdmorph(inc, 0, pd.data()), WithinAbs(expected, 3e-6));
            REQUIRE_THAT(rpdsp::osc_fbfm(inc, 0, 0, fm.data()), WithinAbs(expected, 3e-6));
            REQUIRE_THAT(rpdsp::osc_chaosdrift(inc, 0, chaos.data()), WithinAbs(expected, 3e-6));
            REQUIRE_THAT(rpdsp::osc_tzfm(inc, 0, tz.data()), WithinAbs(expected, 3e-6));
            REQUIRE_THAT(rpdsp::osc_dsf(inc, -3.7f, 0, dsf.data()), WithinAbs(expected, 3e-6));
        }
    }
}

TEST_CASE("Modulated recipe phases stay normalized including negative rates", "[rpdsp][recipe_regression]") {
    std::array<float, 1> pd{}, tri{};
    std::array<float, 2> chaos{}, tz{}, dsf{};
    std::array<float, 3> fm{}, sync{};
    std::array<float, 4> formant{};
    for (int i = 0; i < 16384; ++i) {
        const float inc = std::sin(i * 0.013f); // Includes over-range increments.
        const float out[] = {
            rpdsp::osc_pdmorph(inc, 2.0f, pd.data()),
            rpdsp::osc_fbfm(inc, 1, -2, fm.data()),
            rpdsp::osc_chaosdrift(inc, 2, chaos.data()),
            rpdsp::osc_morphtsq(inc, -2, -1, tri.data()),
            rpdsp::osc_tzfm(inc, -3, tz.data()),
            rpdsp::osc_dsf(inc, -7.5f, i % 2 == 0 ? -2.0f : 2.0f, dsf.data()),
            rpdsp::osc_formant(inc, -0.125f, 0.999f, formant.data()),
            rpdsp::osc_revsync(inc, -4.5f, sync.data())};
        for (float y : out) {
            REQUIRE(std::isfinite(y));
            REQUIRE(std::fabs(y) < 1.01f);
        }
        for (float p : {pd[0], fm[0], chaos[0], tri[0], tz[0], dsf[0], dsf[1], formant[0], formant[1], sync[0], sync[1]}) {
            REQUIRE(p >= 0.0f);
            REQUIRE(p < 1.0f);
        }
    }
    // A stopped fundamental must not allow its grain carrier to grow forever.
    for (int i = 0; i < 32768; ++i) {
        rpdsp::osc_formant(0, 0.25f, 0.999f, formant.data());
        REQUIRE(formant[1] >= 0.0f);
        REQUIRE(formant[1] < 1.0f);
    }
}

TEST_CASE("Frequency-shifter carrier has the requested signed frequency", "[rpdsp][recipe_regression]") {
    for (float fs : {44100.0f, 48000.0f, 96000.0f}) {
        for (float hz : {-20000.0f, -5000.0f, 0.0f, 5000.0f, 20000.0f}) {
            for (bool cached : {false, true}) {
                CAPTURE(fs, hz, cached);
                std::array<float, 35> state{};
                const auto coeff = rpdsp::make_fx_freqshift_coefficients(hz / fs);
                double angle = 0.0, c = 1.0, s = 0.0;
                for (int i = 0; i < 4096; ++i) {
                    if (cached) rpdsp::fx_freqshift(0, coeff, state.data());
                    else rpdsp::fx_freqshift(0, hz / fs, state.data());
                    angle += std::atan2(c * state[34] - s * state[33], c * state[33] + s * state[34]);
                    c = state[33]; s = state[34];
                    REQUIRE_THAT(c * c + s * s, WithinAbs(1.0, 5e-6));
                }
                REQUIRE_THAT(angle * fs / (2.0 * pi * 4096), WithinAbs(hz, 0.03));
            }
        }
    }
}

TEST_CASE("Frequency-shifter wrapper preserves its audio sideband convention", "[rpdsp][recipe_regression]") {
    rpdsp::FrequencyShifter shifter;
    for (float fs : {48000.0f, 96000.0f}) {
        for (float hz : {-5000.0f, 5000.0f}) {
            CAPTURE(fs, hz);
            // Also exercise set-before-prepare and sample-rate changes.
            shifter.setShiftHz(hz);
            shifter.prepare(fs);
            double wantedC = 0, wantedS = 0, imageC = 0, imageS = 0;
            const int warmup = static_cast<int>(fs / 4);
            const int count = static_cast<int>(fs / 10);
            for (int i = 0; i < warmup + count; ++i) {
                const double t = i / static_cast<double>(fs);
                const float y = shifter.process(static_cast<float>(std::sin(2 * pi * 6000 * t)));
                REQUIRE(std::isfinite(y));
                if (i >= warmup) {
                    // Original Hilbert polarity: positive shift moves downward.
                    wantedC += y * std::cos(2 * pi * (6000 - hz) * t);
                    wantedS += y * std::sin(2 * pi * (6000 - hz) * t);
                    imageC += y * std::cos(2 * pi * (6000 + hz) * t);
                    imageS += y * std::sin(2 * pi * (6000 + hz) * t);
                }
            }
            const double wanted = 2 * std::hypot(wantedC, wantedS) / count;
            const double image = 2 * std::hypot(imageC, imageS) / count;
            REQUIRE(wanted > 0.9);
            REQUIRE(image < 0.05);
        }
    }
}

TEST_CASE("Legacy LCG recipes progress from a zero seed", "[rpdsp][recipe_regression]") {
    uint32_t cubicSeed = 0, wanderSeed = 0, cloudSeed = 0;
    std::array<float, 5> cubic{};
    std::array<float, 1> wander{};
    std::array<float, 4> cloud{}, buffer{};
    rpdsp::lfo_randcubic(1, &cubicSeed, cubic.data());
    REQUIRE(cubicSeed != 0u);
    REQUIRE(rpdsp::cv_wander(0.001f, 0.1f, &wanderSeed, wander.data()) != 0.0f);
    REQUIRE(wanderSeed != 0u);
    REQUIRE(std::isfinite(rpdsp::gran_cloud(1, buffer.data(), 4, 0, &cloudSeed, cloud.data())));
    REQUIRE(cloudSeed != 0u);
}
