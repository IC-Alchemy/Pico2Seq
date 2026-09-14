#include <catch2/catch_test_macros.hpp>
#include "voice/presets/MusicalPresets.h"
#include <cmath>

namespace {
// Frozen equations from rpDSP c8369de. These deliberately do not call the
// prepared APIs: they detect changes to waveform, phase, or Nyquist fading.
float originalPd(float inc, float shape, float *s)
{
    const float p = rpdsp::wrap01(s[0] + rpdsp::clamp(inc, -0.5f, 0.5f));
    s[0] = p;
    const float k = 0.5f - rpdsp::clamp01(shape) * 0.49f;
    const float w = p < k ? p * (0.5f / k) : 0.5f + (p-k) * (0.5f / (1.0f-k));
    return rpdsp::sinNormalizedPhase(rpdsp::wrap01(w));
}
float originalPrism(float inc, float focus, float spread, float *s)
{
    inc = rpdsp::clamp(inc, -0.5f, 0.5f);
    const float center = 1.0f + 5.0f * rpdsp::clamp01(focus);
    const float width = 1.0f + 5.0f * rpdsp::clamp01(spread);
    s[0] = rpdsp::wrap01(s[0] + inc);
    float out = 0, norm = 0;
    for (int h = 1; h <= 6; ++h) {
        const float tent = std::fmax(0.0f, 1.0f - std::fabs(float(h)-center) / width);
        const float weight = tent * tent;
        const float fade = rpdsp::clamp01((0.5f - std::fabs(inc) * h) * 20.0f);
        norm += weight;
        if (weight * fade > 0)
            out += weight * fade * rpdsp::sinNormalizedPhase(rpdsp::wrap01(s[0]*h));
    }
    return out / norm;
}
}

TEST_CASE("Prepared oscillators retain original phase and spectra", "[voice][recipes][optimization]")
{
    for (float control : {-0.2f, 0.0f, 0.25f, 0.8f, 1.0f, 1.2f}) {
        for (float spread : {0.0f, 0.3f, 1.0f}) {
            const auto pd = rpdsp::make_osc_pdmorph_coefficients(control);
            const auto prism = rpdsp::make_osc_prism_coefficients(control, spread);
            float a=0, b=0, c=0, d=0, e=0;
            for (int n = 0; n < 4096; ++n) {
                // Signed frequencies sweep through every partial's Nyquist fade.
                const float inc = -0.6f + 1.2f * n / 4095.0f;
                const float expectedPd = originalPd(inc, control, &a);
                REQUIRE(std::abs(rpdsp::osc_pdmorph(inc, pd, &b) - expectedPd) < 2e-6f);
                REQUIRE(std::abs(rpdsp::osc_pdmorph(inc, control, &e) - expectedPd) < 2e-6f);
                REQUIRE(std::abs(rpdsp::osc_prism(inc, prism, &c) -
                                 originalPrism(inc, control, spread, &d)) < 2e-6f);
                REQUIRE(a == b);
                REQUIRE(c == d);
            }
        }
    }
}

TEST_CASE("Recipe coefficients survive live edits and retriggers", "[voice][recipes][optimization]")
{
    for (const auto initial : {VoicePresets::makeSilkPad(), VoicePresets::makeHollowBell(),
                              VoicePresets::makeSyncLead(), VoicePresets::makeAirChime()}) {
        for (float rate : {32000.0f, 48000.0f, 96000.0f}) {
            auto c = initial;
            RecipeEngine engine;
            engine.prepare(rate);
            engine.select(c.recipe);
            engine.configure(c);
            float s[4]{};
            for (int n = 0; n < 4096; ++n) {
                if (n == 1024) {
                    c.macro1 *= 0.5f; c.macro2 *= 0.75f;
                    engine.configure(c); // Must preserve all oscillator phases.
                }
                if (n == 2048) {
                    engine.trigger(c);
                    if (c.recipeRetrigger) for (float &v : s) v = 0;
                }
                const float hz = 110.0f + n * 0.08f;
                const float inc = hz * (1.0f / rate);
                float expected;
                if (c.recipe == &VoiceRecipes::kSilkPad) {
                    const float ratio = 1.0f + 0.006f * c.macro2;
                    const float a = originalPd(inc * ratio, c.macro1, s);
                    const float b = originalPd(inc / ratio, c.macro1, s + 1);
                    expected = a * (1-c.macro3) + b * c.macro3;
                } else if (c.recipe == &VoiceRecipes::kHollowBell) {
                    const float body = originalPd(inc, c.macro2, s);
                    const float ring = originalPd(inc * c.macro1, 0, s + 1);
                    expected = body * (1-c.macro3) + body * ring * c.macro3;
                } else if (c.recipe == &VoiceRecipes::kSyncLead) {
                    const float sync = rpdsp::osc_revsync(inc, c.macro1, s);
                    const float body = originalPd(inc, c.macro2, s + 3);
                    expected = body * (1-c.macro3) + sync * c.macro3;
                } else {
                    const float partials = originalPrism(inc, c.macro1, c.macro2, s);
                    const float octave = originalPd(inc * 2, 0, s + 1);
                    expected = partials * (1-c.macro3) + octave * c.macro3;
                }
                // Reciprocal multiplication in the detuned pad can round its
                // increment differently; allow accumulated float phase error.
                REQUIRE(std::abs(engine.process(hz, c) - expected) < 1e-4f);
            }
        }
    }
}

TEST_CASE("Feedback operator keeps history when feedback is enabled live", "[voice][recipes][optimization]")
{
    float actual[3]{}, reference[3]{};
    for (int n = 0; n < 8192; ++n) {
        const float feedback = n < 2048 || n >= 6144 ? 0.0f : 0.02f;
        const float mod = n < 4096 ? 0.0f : 0.03f;
        const float p = rpdsp::wrap01(reference[0] + 0.0125f);
        reference[0] = p;
        const float phase = p + mod + feedback * 0.5f * (reference[1] + reference[2]);
        const float expected = rpdsp::sinNormalizedPhase(rpdsp::wrap01(phase));
        reference[2] = reference[1]; reference[1] = expected;
        REQUIRE(rpdsp::osc_fbfm(0.0125f, feedback, mod, actual) == expected);
        REQUIRE(actual[2] == reference[2]);
    }
}
