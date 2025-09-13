# Audio Hot-Path Optimization Guide

This guide documents optimizations applied to [VoiceManager::processAllVoices()](src/voice/VoiceManager.cpp:356) and provides a reusable framework to optimize other real-time paths (Core 0) across the project.

Targets and constraints:
- Runs on Core 0: no heap allocations, no blocking I/O.
- Preserve voice/gate semantics and routing. Use vendor DSP in [src/dsp/dsp.h](src/dsp/dsp.h:1).
- Keep minimal branching and memory indirection inside per-sample loops.

---

## Summary of bottlenecks and changes

Original bottlenecks (typical in embedded audio):
- Indirection and repeated checks inside tight loops (smart pointers, optional null checks, scattered branching).
- Index recomputation and pointer arithmetic in interleaved stereo writes.
- Extra temporaries and non-const locals causing register spills.
- Work done every sample that could be hoisted or throttled (e.g., compressor internal state update).

Changes made:
- Caching pointers and using early-continue reduces inner-branch cost in [VoiceManager::processAllVoices()](src/voice/VoiceManager.cpp:356).
- Sequential pointer writes in [fill_audio_buffer()](Pico2Seq.ino:611) avoid repeated 2×i indexing math.
- Kept conversion as a small, branchless, saturating path in [FloatToPcm16()](Pico2Seq.ino:586).
- Preserved optional compressor fast-path with N-sample update window (documented but left off by default to keep CPU headroom).

---

## Step-by-step techniques (and why they help)

1) Minimize branching inside hot loops
- Use early-continue rather than nested ifs. Fewer taken branches reduces pipeline disruptions on Cortex-M.
- Example: inside voice mixing loop, check enabled/voice!=nullptr once, otherwise continue.

2) Cache frequently used pointers/references
- Convert std::unique_ptr<T> indirection to a raw pointer in a local for the duration of the loop.
- Avoids repeated .get() and potential aliasing penalties.

3) Tighten arithmetic in inner loops
- Keep minimal temporaries; make locals const to encourage register allocation.
- Favor simple MAC (sum += v * mix) that the compiler can keep in FPU registers.

4) Linear pointer writes for interleaved audio
- Use a running pointer (int16_t* p = out) and increment p to avoid recomputing out[2*i + k].
- Improves codegen and reduces ALU overhead in [fill_audio_buffer()](Pico2Seq.ino:611).

5) Hoist or throttle infrequent work
- If enabling the compressor, update internal state every N samples and call Apply() in-between.
- Avoids full state update cost per sample while keeping dynamics consistent.

6) Prefer inline/constexpr for hot-path helpers
- Keep small helpers inline (e.g., FloatToPcm16) to enable constant propagation and better scheduling.

---

## Before/After examples

A) processAllVoices()

Before (simplified):
```cpp
float VoiceManager::processAllVoices() noexcept
{
    float mixedOutput = 0.0f;
    for (auto& managedVoice : voices)
    {
        if (managedVoice->enabled && managedVoice->voice)
        {
            float voiceOutput = managedVoice->voice->process();
            mixedOutput += voiceOutput * managedVoice->mixLevel;
        }
    }
    return mixedOutput * globalVolume;
}
```

After (current):
```cpp
float VoiceManager::processAllVoices() noexcept
{
    float sum = 0.0f;

    for (const auto& mvPtr : voices)
    {
        const ManagedVoice* mv = mvPtr.get();
        if (!mv->enabled || mv->voice == nullptr)
            continue;

        const float v = mv->voice->process();
        sum += v * mv->mixLevel;
    }

    return sum * globalVolume;
}
```

What changed:
- Reduced nested-if by using early-continue.
- Cached ManagedVoice pointer in a local to avoid repeated smart-pointer deref.
- Kept arithmetic minimal inside loop to improve FPU scheduling.

B) fill_audio_buffer()

Before:
```cpp
for (int i = 0; i < N; ++i)
{
    float y = voiceManager->processAllVoices();
    int16_t s = FloatToPcm16(y);
    out[2 * i + 0] = s;
    out[2 * i + 1] = s;
}
```

After (current):
```cpp
int16_t* p = out;
for (int i = 0; i < N; ++i)
{
    const float y = voiceManager->processAllVoices();
    const int16_t s = FloatToPcm16(y);
    *p++ = s; // L
    *p++ = s; // R
}
```

What changed:
- Linear pointer writes remove repeated index arithmetic.
- const locals encourage register allocation, reduce spills.

---

## Reusable optimization checklist/template

Use this list when optimizing any hot-path function:

- Identify hot loops
  - Confirm function is called per sample or per sample-pair.
  - Check for nested loops or expensive calls inside per-sample region.

- Minimize memory indirection
  - Cache smart pointers (unique_ptr/shared_ptr) to raw pointers for loop scope.
  - Cache frequently accessed fields in locals.

- Reduce branching in inner loops
  - Prefer early-continue on guard conditions.
  - Merge conditions when possible; avoid nested branches.

- Eliminate redundant calculations
  - Hoist invariant expressions out of the loop.
  - Replace repeated index math with linear pointer increments.

- Use constexpr/inline for micro-helpers
  - Keep tiny helpers inline to enable better codegen.
  - Avoid virtual dispatch in hot loops where possible.

- Throttle slow-updating state
  - For dynamics/LFO/filters that don’t need per-sample recompute, update every N samples.
  - Provide a cheap Apply() fast-path between updates.

- Mind FPU pipeline
  - Keep operations simple and ordered to let compiler schedule well (MAC patterns).
  - Avoid unnecessary conversions or function calls.

- Verify no heap or blocking I/O on Core 0
  - No new/delete/malloc/free.
  - No logging, printing, or driver calls on the audio thread.

Template:

```cpp
// Hot-path function template
inline float process_hot_path(/* args */) noexcept
{
    // 1) Precompute/hoist
    // const float invBuf = 1.0f / N; // example

    // 2) Cache pointers/refs
    // MyType* ptr = container[i].get();

    float acc = 0.0f;
    for (/* each active item */)
    {
        // 3) Early-continue guards
        // if (!enabled || obj == nullptr) continue;

        // 4) Fast path arithmetic; minimal temporaries
        // const float v = obj->Process();
        // acc += v * gain;
    }

    // 5) Optional throttled state update
    // if ((counter++ & (kInterval - 1)) == 0) { UpdateExpensiveState(); }

    return acc;
}
```

---

## Measuring impact and verifying parity

Measuring performance:
- Profiling hooks (coarse grained):
  - Add GPIO toggle around [fill_audio_buffer()](Pico2Seq.ino:611) loop and check duty cycle with logic analyzer.
  - Wrap sections with light-weight timestamp delta (cycle counter if available on RP2350).
- Timing macros:
  - Create a minimal timing macro that records cycles in a static buffer when a debug flag is enabled. Ensure it’s compiled out in release.
- Per-block budget:
  - Set explicit per-sample time budget for 48kHz (20.833 μs). Ensure fill path stays within budget with margin.

Functional parity:
- Regression tests (manual until test harness exists):
  - A/B record 10–20 seconds pre/post changes; compare RMS and peak levels at known settings.
  - Verify gate timing with scope by mapping an envelope stage to a debug GPIO for transient integrity.
- Audio QA checklist:
  - No zipper noise increases on parameter moves (filter cutoff, envelope depth).
  - No added latency or phasing on voice mix at unity.
  - No increased denorm behavior (ensure FPU flush-to-zero if needed).

---

## Integration into workflow

Code review checklist (performance-sensitive PRs):
- Confirms: No heap/IO in audio thread. No logging on Core 0.
- Confirms: Branch minimization and pointer caching in hot loops.
- Confirms: Expensive work hoisted or throttled with an update interval.
- Confirms: Vendor DSP APIs unchanged and compatible ([src/dsp/dsp.h](src/dsp/dsp.h:1)).
- Confirms: Gate/voice semantics intact (see [Pico2Seq.ino](Pico2Seq.ino:486)).

CI suggestions:
- Build-time perf toggles:
  - Add a PERF_DIAGNOSTICS flag to enable cycle capture (compiled out by default).
- Static checks:
  - Lint rule denies new/malloc/free in files that compile into Core 0 image path.
- Binary size/section checks:
  - Ensure hot-path functions remain inline; check .text growth for unexpected bloat.

---

## Appendix: Optional compressor fast-path pattern

If enabling the compressor in [VoiceManager::processAllVoices()](src/voice/VoiceManager.cpp:356):
```cpp
float sum = /* ... */;

float compressed = sum;
if (compressorUpdateInterval == 0)
{
    compressed = compressor.Process(sum);
    }
    else
    {
        if (compressorUpdateCounter == 0)
                compressed = compressor.Process(sum);
                    else
                            compressed = compressor.Apply(sum);
                            
                                compressorUpdateCounter = (compressorUpdateCounter + 1) % compressorUpdateInterval;
                                }
                                
                                return compressed * globalVolume;
                                ```
                                ```
This updates gain state every N samples but uses a single multiply in-between, reducing CPU load while maintaining dynamics.

---

By applying the checklist and patterns above, developers can systematically improve performance across the codebase while preserving audio behavior and project constraints.