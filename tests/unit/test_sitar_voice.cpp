// Host DSP tests for rpdsp::SitarStringVoice (sitar physical model).
//
// The suite has no FFT/goertzel helpers, so this file carries two small
// host-only measurement utilities (goertzel magnitude + rms). All renders
// are deterministic: prepare()/reset() reseed the excitation noise, so two
// voices with identical settings produce identical samples.

#include <catch2/catch_test_macros.hpp>

#include <rpdsp/sitar.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

constexpr float kSampleRate = 48000.0f;
// Matches Voice.h kWaveguideCapacity (about 24 Hz lowest pitch at 48 kHz).
constexpr size_t kCapacity = 2048;

using Sitar = rpdsp::SitarStringVoice<kCapacity>;

// --- host-only measurement helpers -----------------------------------------

// Goertzel magnitude of one frequency bin over a rendered block (double
// precision keeps repeated renders comparable; host side only).
double goertzelMagnitude(const std::vector<float> &x, double frequencyHz, double sampleRate)
{
    const double omega = 6.283185307179586 * frequencyHz / sampleRate;
    const double coeff = 2.0 * std::cos(omega);
    double s1 = 0.0;
    double s2 = 0.0;
    for (const float value : x) {
        const double s = static_cast<double>(value) + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return std::sqrt(std::max(0.0, power));
}

double rmsOf(const std::vector<float> &x)
{
    if (x.empty()) return 0.0;
    double sum = 0.0;
    for (const float value : x) sum += static_cast<double>(value) * value;
    return std::sqrt(sum / static_cast<double>(x.size()));
}

template <size_t Capacity>
std::vector<float> render(rpdsp::SitarStringVoice<Capacity> &voice, size_t samples)
{
    std::vector<float> out(samples);
    for (float &value : out) value = voice.process();
    return out;
}

// Energy in harmonics 4..10 relative to the fundamental — a jawari buzz must
// raise this ratio, a clean string keeps it low.
double highHarmonicRatio(const std::vector<float> &x, double f0, double sampleRate)
{
    double fundamental = 0.0;
    double high = 0.0;
    for (int harmonic = 1; harmonic <= 10; ++harmonic) {
        const double magnitude = goertzelMagnitude(x, f0 * harmonic, sampleRate);
        if (harmonic == 1) fundamental = magnitude;
        else if (harmonic >= 4) high += magnitude * magnitude;
    }
    return high / (fundamental * fundamental + 1.0e-12);
}

// True when a rendered block's strongest partial near `expectedHz` sits within
// `cents` of it (bracketed by two slightly detuned probe bins).
bool pitchWithin(const std::vector<float> &x, double expectedHz, double sampleRate, double cents)
{
    const double sharp = expectedHz * std::pow(2.0, cents / 1200.0);
    const double flat = expectedHz * std::pow(2.0, -cents / 1200.0);
    const double center = goertzelMagnitude(x, expectedHz, sampleRate);
    return center > goertzelMagnitude(x, sharp, sampleRate) &&
           center > goertzelMagnitude(x, flat, sampleRate);
}

Sitar makeVoice()
{
    Sitar voice;
    voice.prepare(kSampleRate);
    return voice;
}

std::vector<float> window(const std::vector<float> &x, size_t begin, size_t end)
{
    return std::vector<float>(x.begin() + static_cast<std::ptrdiff_t>(begin),
                              x.begin() + static_cast<std::ptrdiff_t>(end));
}

}  // namespace

TEST_CASE("SitarStringVoice plucks accurate pitch across the usable range", "[rpdsp][sitar]")
{
    // 82.4 Hz needs a 582-sample period (fits 2048); 1318.5 Hz is a top-range
    // sitar melody note. Jawari/taraf/body off to measure the bare string.
    for (const float frequency : {82.41f, 110.0f, 220.0f, 440.0f, 880.0f, 1318.51f}) {
        INFO("frequency " << frequency);
        Sitar voice = makeVoice();
        voice.setJawari(0.0f);
        voice.setTarafAmount(0.0f);
        voice.setBodyAmount(0.0f);
        voice.pluck(frequency, 0.8f);
        const std::vector<float> audio = render(voice, 8192);
        const std::vector<float> steady = window(audio, 2048, 8192);
        REQUIRE(pitchWithin(steady, frequency, kSampleRate, 35.0));
    }

    SECTION("Jawari buzz does not detune the string")
    {
        Sitar voice = makeVoice();
        voice.setJawari(0.7f);
        voice.setTarafAmount(0.0f);
        voice.setBodyAmount(0.0f);
        voice.pluck(220.0f, 0.8f);
        const std::vector<float> audio = render(voice, 8192);
        REQUIRE(pitchWithin(window(audio, 2048, 8192), 220.0, kSampleRate, 35.0));
    }

    SECTION("A different template capacity stays in tune")
    {
        rpdsp::SitarStringVoice<512> voice;
        voice.prepare(kSampleRate);
        voice.setJawari(0.0f);
        voice.setTarafAmount(0.0f);
        voice.setBodyAmount(0.0f);
        voice.pluck(440.0f, 0.8f);
        const std::vector<float> audio = render(voice, 8192);
        REQUIRE(pitchWithin(window(audio, 2048, 8192), 440.0, kSampleRate, 35.0));
    }
}

TEST_CASE("Jawari measurably changes harmonic content", "[rpdsp][sitar]")
{
    const double frequency = 220.0;
    Sitar clean = makeVoice();
    clean.setJawari(0.0f);
    Sitar buzzing = makeVoice();
    buzzing.setJawari(0.85f);
    clean.pluck(frequency, 1.0f);
    buzzing.pluck(frequency, 1.0f);

    const std::vector<float> cleanAudio = render(clean, 48000);
    const std::vector<float> buzzAudio = render(buzzing, 48000);
    for (const float value : buzzAudio) REQUIRE(std::isfinite(value));

    // Early window (string still loud, bridge contact active) and late window
    // (string decayed below the contact threshold).
    const double cleanEarly =
        highHarmonicRatio(window(cleanAudio, 1024, 10240), frequency, kSampleRate);
    const double buzzEarly =
        highHarmonicRatio(window(buzzAudio, 1024, 10240), frequency, kSampleRate);
    const double buzzLate =
        highHarmonicRatio(window(buzzAudio, 8192, 32768), frequency, kSampleRate);

    // The two renders also differ outright (mean absolute difference idiom).
    double difference = 0.0;
    for (size_t i = 1024; i < 10240; ++i)
        difference += std::abs(buzzAudio[i] - cleanAudio[i]);
    REQUIRE(difference / 9216.0 > 0.002);

    INFO("clean early " << cleanEarly << " buzz early " << buzzEarly
                        << " buzz late " << buzzLate);
    // Buzzing raises the upper-harmonic content right after the pluck...
    REQUIRE(buzzEarly > 1.4 * cleanEarly);
    // ...and cleans up as the amplitude falls: evolving buzz, not static
    // distortion (the contact saps energy, so it fades faster than clean).
    REQUIRE(buzzEarly > 1.7 * buzzLate);
}

TEST_CASE("Jawari never causes feedback instability", "[rpdsp][sitar]")
{
    for (const float jawari : {0.0f, 0.5f, 1.0f}) {
        for (const float threshold : {0.02f, 0.3f, 0.98f}) {
            for (const float frequency : {82.41f, 220.0f, 880.0f}) {
                for (const float t60 : {0.2f, 5.0f}) {
                    INFO("jawari " << jawari << " threshold " << threshold
                                   << " frequency " << frequency << " t60 " << t60);
                    Sitar voice = makeVoice();
                    voice.setJawari(jawari);
                    voice.setJawariThreshold(threshold);
                    voice.setDecayTimeSeconds(t60);
                    voice.pluck(frequency, 1.0f);
                    const std::vector<float> audio = render(voice, kSampleRate);  // 1 s
                    float peak = 0.0f;
                    for (const float value : audio) {
                        REQUIRE(std::isfinite(value));
                        peak = std::max(peak, std::abs(value));
                    }
                    REQUIRE(peak < 8.0f);
                    if (t60 < 1.0f) {
                        // Short decay must actually die through the buzz.
                        const std::vector<float> early = window(audio, 0, 4800);
                        const std::vector<float> late = window(audio, 43200, 48000);
                        REQUIRE(rmsOf(late) < rmsOf(early));
                    }
                }
            }
        }
    }
}

TEST_CASE("Sympathetic taraf modes ring after the main string dies", "[rpdsp][sitar]")
{
    // String T60 1 s is 100 dB down and quiet-counter deactivated well before
    // the measurement window; taraf decay 3 s keeps the modes ringing into
    // it. Jawari is on: the buzz is what feeds modes tuned between string
    // harmonics (like the fifth).
    const double frequency = 220.0;
    Sitar ringing = makeVoice();
    ringing.setDecayTimeSeconds(1.0f);
    ringing.setJawari(0.7f);
    ringing.setTarafAmount(0.5f);
    ringing.setTarafDecaySeconds(3.0f);
    ringing.setBodyAmount(0.0f);
    Sitar dry = makeVoice();
    dry.setDecayTimeSeconds(1.0f);
    dry.setJawari(0.7f);
    dry.setTarafAmount(0.0f);
    dry.setBodyAmount(0.0f);
    ringing.pluck(frequency, 1.0f);
    dry.pluck(frequency, 1.0f);

    const std::vector<float> ringingAudio = render(ringing, 120000);  // 2.5 s
    const std::vector<float> dryAudio = render(dry, 120000);

    // Window [1.6 s, 2.3 s]: main string gone, sympathetic bank still rings.
    const std::vector<float> ringingTail = window(ringingAudio, 76800, 110400);
    const std::vector<float> dryTail = window(dryAudio, 76800, 110400);

    const double tailRms = rmsOf(ringingTail);
    INFO("tail rms " << tailRms);
    REQUIRE(tailRms > 1.0e-4);
    // With taraf at 0 every component has decayed: the voice must be silent.
    REQUIRE(rmsOf(dryTail) == 0.0);

    // The octave mode (2f0) is driven directly by the string's second
    // harmonic; the fifth (1.5f0, between harmonics) is fed by the buzz. Both
    // must be alive in the tail.
    const double octaveMode = goertzelMagnitude(ringingTail, 2.0 * frequency, kSampleRate);
    const double fifthMode = goertzelMagnitude(ringingTail, 1.5 * frequency, kSampleRate);
    INFO("octave mode " << octaveMode << " fifth mode " << fifthMode);
    REQUIRE(octaveMode > 1.0e-3);
    REQUIRE(fifthMode > 1.0e-3);
}

TEST_CASE("TARAF amount 0 removes the sympathetic contribution", "[rpdsp][sitar]")
{
    Sitar withTaraf = makeVoice();
    withTaraf.setDecayTimeSeconds(1.0f);
    withTaraf.setJawari(0.5f);
    withTaraf.setTarafAmount(0.5f);
    withTaraf.setTarafDecaySeconds(3.0f);
    withTaraf.setBodyAmount(0.0f);
    Sitar withoutTaraf = makeVoice();
    withoutTaraf.setDecayTimeSeconds(1.0f);
    withoutTaraf.setJawari(0.5f);
    withoutTaraf.setTarafAmount(0.0f);
    withoutTaraf.setBodyAmount(0.0f);
    withTaraf.pluck(220.0f, 1.0f);
    withoutTaraf.pluck(220.0f, 1.0f);

    const std::vector<float> withAudio = render(withTaraf, 120000);
    const std::vector<float> withoutAudio = render(withoutTaraf, 120000);

    // Same seeds and settings apart from taraf: early render differs only by
    // the driven resonators, late render is taraf-only.
    double earlyDifference = 0.0;
    for (size_t i = 0; i < 2048; ++i)
        earlyDifference += std::abs(withAudio[i] - withoutAudio[i]);
    REQUIRE(earlyDifference / 2048.0 > 1.0e-5);

    const std::vector<float> withTail = window(withAudio, 76800, 110400);
    REQUIRE(rmsOf(withTail) > 10.0 * rmsOf(window(withoutAudio, 76800, 110400)) + 1.0e-6);
}

TEST_CASE("Decay reaches silence and the voice reports inactive", "[rpdsp][sitar]")
{
    Sitar voice = makeVoice();  // defaults: 5 s string, 4 s taraf, body on
    REQUIRE_FALSE(voice.isActive());

    voice.pluck(220.0f, 1.0f);
    REQUIRE(voice.isActive());

    constexpr size_t kMaxSamples = 30u * 48000u;
    bool becameInactive = false;
    size_t samples = 0;
    while (samples < kMaxSamples) {
        voice.process();
        ++samples;
        if (!voice.isActive()) {
            becameInactive = true;
            break;
        }
    }
    INFO("rendered " << samples << " samples before inactive");
    REQUIRE(becameInactive);

    // Silence stays exactly silent and inactive stays inactive.
    for (int i = 0; i < 1000; ++i) REQUIRE(voice.process() == 0.0f);
    for (int i = 0; i < 10000; ++i) {
        voice.process();
        REQUIRE_FALSE(voice.isActive());
    }

    SECTION("A silent voice revives on the next pluck")
    {
        voice.pluck(330.0f, 1.0f);
        REQUIRE(voice.isActive());
        const std::vector<float> audio = render(voice, 4096);
        float peak = 0.0f;
        for (const float value : audio) peak = std::max(peak, std::abs(value));
        REQUIRE(peak > 0.01f);
    }
}

TEST_CASE("Velocity changes excitation level and jawari intensity", "[rpdsp][sitar]")
{
    SECTION("Excitation amplitude scales with velocity")
    {
        Sitar loud = makeVoice();
        loud.setJawari(0.0f);  // isolate the linear excitation scaling
        Sitar quiet = makeVoice();
        quiet.setJawari(0.0f);
        loud.pluck(220.0f, 1.0f);
        quiet.pluck(220.0f, 0.25f);
        const std::vector<float> loudAudio = render(loud, 2048);
        const std::vector<float> quietAudio = render(quiet, 2048);
        const double ratio = rmsOf(loudAudio) / (rmsOf(quietAudio) + 1.0e-9);
        INFO("loud/quiet rms ratio " << ratio);
        REQUIRE(ratio > 3.0);
        REQUIRE(ratio < 5.0);  // linear excitation would give 4.0
    }

    SECTION("Louder plucks buzz harder")
    {
        Sitar loud = makeVoice();
        loud.setJawari(0.85f);
        Sitar quiet = makeVoice();
        quiet.setJawari(0.85f);
        loud.pluck(220.0f, 1.0f);
        quiet.pluck(220.0f, 0.4f);
        const std::vector<float> loudAudio = render(loud, 6144);
        const std::vector<float> quietAudio = render(quiet, 6144);
        const double loudRatio = highHarmonicRatio(loudAudio, 220.0, kSampleRate);
        const double quietRatio = highHarmonicRatio(quietAudio, 220.0, kSampleRate);
        INFO("loud ratio " << loudRatio << " quiet ratio " << quietRatio);
        REQUIRE(loudRatio > quietRatio);
    }
}

TEST_CASE("Retrigger excites cleanly without runaway", "[rpdsp][sitar]")
{
    Sitar voice = makeVoice();
    voice.setDecayTimeSeconds(3.0f);
    voice.setJawari(0.6f);
    voice.setTarafAmount(0.5f);
    voice.setBodyAmount(0.3f);
    voice.pluck(220.0f, 1.0f);
    const std::vector<float> first = render(voice, 24000);
    voice.pluck(261.63f, 1.0f);
    const std::vector<float> second = render(voice, 24000);

    auto peakOf = [](const std::vector<float> &x) {
        float peak = 0.0f;
        for (const float value : x) peak = std::max(peak, std::abs(value));
        return peak;
    };
    INFO("first peak " << peakOf(first) << " second peak " << peakOf(second));
    REQUIRE(peakOf(second) > 0.05f);
    REQUIRE(peakOf(second) > 0.5f * peakOf(first));
    REQUIRE(peakOf(second) < 2.0f * peakOf(first) + 0.1f);

    SECTION("Rapid retriggers stay bounded and finite")
    {
        Sitar storm = makeVoice();
        storm.setJawari(0.9f);
        storm.setTarafAmount(1.0f);
        storm.setBodyAmount(1.0f);
        float peak = 0.0f;
        for (int note = 0; note < 96; ++note) {
            storm.pluck(note % 2 == 0 ? 220.0f : 330.0f, 1.0f);
            for (int sample = 0; sample < 1024; ++sample) {
                const float value = storm.process();
                REQUIRE(std::isfinite(value));
                peak = std::max(peak, std::abs(value));
            }
        }
        REQUIRE(peak < 8.0f);
    }
}

TEST_CASE("Pitch slide (meend) is continuous and lands on target", "[rpdsp][sitar]")
{
    Sitar voice = makeVoice();
    voice.setJawari(0.3f);
    voice.setTarafAmount(0.4f);
    voice.setBodyAmount(0.2f);
    voice.pluck(220.0f, 0.9f);

    const std::vector<float> before = render(voice, 9600);  // 0.2 s settles
    voice.slideTo(330.0f);
    // Default slide time is 120 ms; render 3x that plus measurement margin.
    std::vector<float> after = render(voice, 33600);

    // No discontinuity while the ringing string bends (a reset or retrigger
    // would show a sample-to-sample jump far larger than a 330 Hz waveform).
    float maxDelta = 0.0f;
    float previous = before.back();
    for (const float value : after) {
        maxDelta = std::max(maxDelta, std::abs(value - previous));
        previous = value;
    }
    INFO("max sample-to-sample delta during slide " << maxDelta);
    REQUIRE(maxDelta < 0.5f);

    // Sympathetic bank survives the bend: the voice never goes inactive.
    for (const float value : after) {
        (void)value;
        REQUIRE(voice.isActive());
    }

    // After >= 3x the slide time the pitch sits at the target.
    const std::vector<float> settled = window(after, 24000, 33600);
    REQUIRE(pitchWithin(settled, 330.0, kSampleRate, 35.0));
    REQUIRE(goertzelMagnitude(settled, 330.0, kSampleRate) >
            goertzelMagnitude(settled, 220.0, kSampleRate));

    SECTION("Explicit slide time changes the bend length")
    {
        Sitar slow = makeVoice();
        slow.setJawari(0.3f);
        slow.setTarafAmount(0.0f);
        slow.setBodyAmount(0.0f);
        slow.setSlideTimeSeconds(0.4f);
        slow.pluck(220.0f, 0.9f);
        const std::vector<float> lead = render(slow, 4800);
        slow.slideTo(330.0f);
        const std::vector<float> bent = render(slow, 4800);  // only 0.1 s in
        float maxDelta = 0.0f;
        float previous = lead.back();
        for (const float value : bent) {
            maxDelta = std::max(maxDelta, std::abs(value - previous));
            previous = value;
        }
        REQUIRE(maxDelta < 0.5f);
        // Mid-bend the pitch is between start and target, still stable.
        const double midMagnitude = goertzelMagnitude(bent, 262.0, kSampleRate);
        REQUIRE(midMagnitude > 1.0e-3);
    }
}

TEST_CASE("Parameter extremes remain stable at multiple sample rates", "[rpdsp][sitar]")
{
    for (const float sampleRate : {44100.0f, 48000.0f, 96000.0f}) {
        INFO("sample rate " << sampleRate);
        for (const bool atMaximum : {false, true}) {
            rpdsp::SitarStringVoice<kCapacity> voice;
            voice.prepare(sampleRate);
            voice.setDecayTimeSeconds(atMaximum ? 10.0f : 0.05f);
            voice.setBrightness(atMaximum ? 1.0f : 0.0f);
            voice.setPickPosition(atMaximum ? 0.5f : 0.02f);
            voice.setPickHardness(atMaximum ? 1.0f : 0.0f);
            voice.setStiffness(atMaximum ? 1.0f : 0.0f);
            voice.setDetuneCents(atMaximum ? 30.0f : 0.0f);
            voice.setJawari(atMaximum ? 1.0f : 0.0f);
            voice.setJawariThreshold(atMaximum ? 1.0f : 0.0f);
            voice.setTarafAmount(atMaximum ? 1.0f : 0.0f);
            voice.setTarafDecaySeconds(atMaximum ? 12.0f : 0.05f);
            voice.setBodyAmount(atMaximum ? 1.0f : 0.0f);
            voice.setBodyFrequency(atMaximum ? 500.0f : 50.0f);
            voice.setSlideTimeSeconds(atMaximum ? 2.0f : 0.005f);
            voice.pluck(atMaximum ? 880.0f : 82.41f, 1.0f);
            if (atMaximum) voice.slideTo(660.0f);
            float peak = 0.0f;
            for (size_t i = 0; i < 24000; ++i) {
                const float value = voice.process();
                REQUIRE(std::isfinite(value));
                peak = std::max(peak, std::abs(value));
            }
            INFO("atMaximum " << atMaximum << " peak " << peak);
            REQUIRE(peak < 8.0f);
        }
    }
}

TEST_CASE("No denormal accumulation; silence is exact", "[rpdsp][sitar]")
{
    // Tiny pluck below every threshold: the voice must decay to exact zeros,
    // never hover in denormal fog or grow back.
    Sitar voice = makeVoice();
    voice.setJawari(1.0f);
    voice.setJawariThreshold(0.98f);
    voice.setDecayTimeSeconds(0.3f);
    voice.setTarafDecaySeconds(0.4f);
    voice.pluck(220.0f, 1.0e-3f);

    constexpr size_t kMaxSamples = 10u * 48000u;
    bool becameInactive = false;
    for (size_t i = 0; i < kMaxSamples && !becameInactive; ++i) {
        const float value = voice.process();
        REQUIRE(std::isfinite(value));
        becameInactive = !voice.isActive();
    }
    REQUIRE(becameInactive);
    for (int i = 0; i < 48000; ++i) REQUIRE(voice.process() == 0.0f);
}

TEST_CASE("Identical settings render bit-identical audio", "[rpdsp][sitar]")
{
    Sitar first = makeVoice();
    Sitar second = makeVoice();
    for (Sitar *voice : {&first, &second}) {
        voice->setJawari(0.5f);
        voice->setTarafAmount(0.4f);
        voice->setBodyAmount(0.3f);
        voice->setSlideTimeSeconds(0.12f);
        voice->pluck(220.0f, 0.9f);
    }
    const std::vector<float> firstAudio = render(first, 24000);
    const std::vector<float> secondAudio = render(second, 24000);
    for (size_t i = 0; i < firstAudio.size(); ++i) {
        if (firstAudio[i] != secondAudio[i]) {
            FAIL("sample " << i << " diverged: " << firstAudio[i] << " vs "
                           << secondAudio[i]);
        }
    }
    REQUIRE(first.isActive() == second.isActive());
}
