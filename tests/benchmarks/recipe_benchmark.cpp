// Compare Release builds before/after a recipe change. Host timing only;
// use the firmware heartbeat for RP2350 deadlines and underruns.
#include "voice/presets/MusicalPresets.h"
#include <array>
#include <chrono>
#include <cstdio>

int main()
{
    const std::array<const char *, 8> names = {"VelvetKeys", "CopperBass", "ReedPipe",
        "SilkPad", "HollowBell", "SyncLead", "OrbitPluck", "AirChime"};
    const std::array<VoiceConfig, 8> presets = {VoicePresets::makeVelvetKeys(),
        VoicePresets::makeCopperBass(), VoicePresets::makeReedPipe(),
        VoicePresets::makeSilkPad(), VoicePresets::makeHollowBell(),
        VoicePresets::makeSyncLead(), VoicePresets::makeOrbitPluck(),
        VoicePresets::makeAirChime()};
    constexpr int samples = 480000;
    volatile float sink = 0.0f;
    std::puts("preset,setting,best_ns_per_source_sample");
    for (size_t i = 0; i < presets.size(); ++i) {
        for (int extreme = 0; extreme < 2; ++extreme) {
            auto c = presets[i];
            if (extreme) {
                for (const auto &binding : c.parameters->slots)
                    if (binding.target) c.*(binding.target) = binding.maximum;
            }
            double best = 1e30;
            for (int trial = 0; trial < 5; ++trial) {
                RecipeEngine engine;
                engine.prepare(48000.0f);
                engine.select(c.recipe);
                engine.configure(c);
                float sum = 0.0f;
                const auto start = std::chrono::steady_clock::now();
                for (int n = 0; n < samples; ++n) {
                    // Includes pitch changes, glide, and periodic retriggers.
                    if ((n % 12000) == 0) engine.trigger(c);
                    const float hz = 110.0f + float(n % 12000) * 0.02f;
                    sum += engine.process(hz, c);
                }
                const auto stop = std::chrono::steady_clock::now();
                sink = sum;
                const double ns = std::chrono::duration<double, std::nano>(stop - start).count();
                best = std::min(best, ns / samples);
            }
            std::printf("%s,%s,%.2f\n", names[i], extreme ? "maximum" : "default", best);
        }
    }
    (void)sink;
}
