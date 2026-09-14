# Recipe preset CPU optimization

The eight musical presets keep their oscillator algorithms, parameter mappings,
envelopes, and sample rate. This change removes repeated coefficient work from
their sample loops. It includes changes inside the `src/rpdsp` submodule; those
changes must travel with `RecipeSources.h`.

| Preset | Work removed from the sample loop |
| --- | --- |
| VelvetKeys | Feedback arithmetic and a second phase wrap on an unmodulated, zero-feedback operator. Feedback history still advances for live edits. |
| CopperBass | Phase-distortion arithmetic and a second wrap on its clean sub oscillator; edited sub shapes use prepared coefficients. |
| ReedPipe | Phase-distortion arithmetic and a second wrap on its clean body oscillator. |
| SilkPad | Shape-coefficient divisions for both oscillators and detune-ratio division. |
| HollowBell | Body shape-coefficient division and unused phase distortion on the ring oscillator. |
| SyncLead | Body shape-coefficient division. |
| OrbitPluck | Unused phase distortion on the modulator and clean body oscillators. |
| AirChime | Six harmonic-weight calculations and normalization, plus unused phase distortion on its octave oscillator. |

The existing PhaseMorph, Spectral, and Prism recipes also use the prepared
rpDSP overloads. The old rpDSP signatures remain available for callers that
change shape or spectral controls every sample.

Coefficients use spare slots in the existing 16-float recipe state. Configuration
and trigger callbacks run on the audio core. Configuration preserves oscillator
phases, while triggers preserve each preset's existing reset policy. Pitch and
Prism's harmonic fade near Nyquist still update at audio rate, including slides.

## Validation

- Original-equation comparisons cover signed pitch sweeps, shape/focus/spread
  limits, and harmonic fading. Absolute sample error must be below `2e-6`.
- Recipe comparisons cover live edits and retriggers at 32, 48, and 96 kHz.
  SilkPad's reciprocal multiplication can round the phase increment differently;
  the comparison allows `1e-4` accumulated sample error over 4096 samples.
- Feedback tests check enabling and disabling feedback while retaining history.
- The full host CTest run passes 277 tests, including the focused voice, audio
  buffer ownership, and watchdog suites.
- Cortex-M33 assembly built with the installed Arduino toolchain's `-O3`,
  `-ffast-math`, and softfp settings contains no floating-point division in the
  optimized AirChime or SilkPad source callbacks. The prior callbacks did.
- The final Arduino CLI build at the existing 300 MHz setting exited 0 and
  produced UF2, ELF, BIN, and MAP files. It was not uploaded.

Measured on Windows with Clang 20.1.7, `-O3`, and both benchmark processes pinned
to the same logical CPU (2026-09-14):

| Preset | Default source time before / after (ns) | Default reduction | Maximum-control reduction |
| --- | ---: | ---: | ---: |
| VelvetKeys | 14.95 / 14.00 | 6.4% | 6.3% |
| CopperBass | 20.46 / 18.05 | 11.8% | 12.2% |
| ReedPipe | 17.07 / 15.56 | 8.8% | 17.6% |
| SilkPad | 15.03 / 13.09 | 12.9% | 14.8% |
| HollowBell | 13.80 / 10.87 | 21.2% | 22.2% |
| SyncLead | 13.91 / 12.76 | 8.3% | 10.3% |
| OrbitPluck | 21.07 / 15.95 | 24.3% | 23.5% |
| AirChime | 41.76 / 29.95 | 28.3% | 35.3% |

These are host source timings, not device CPU utilization or whole-voice speedups.
Unpinned runs varied substantially as Windows scheduled the processes on
different cores; use matching CPU affinity when repeating the comparison.

## Repeating the host comparison

Use a Release CMake build, then build the optional target:

```powershell
cmake --build build_test --target pico2seq_recipe_benchmark
.\build_test\tests\pico2seq_recipe_benchmark.exe
```

The benchmark emits CSV for all eight presets at default settings and maximum
timbre controls. Each row is the fastest of five 480,000-sample runs with changing
pitch and periodic triggers. Run the same benchmark source against both revisions
on an otherwise idle machine. It measures source generation only; it excludes
the voice envelope, filters, control queue, mixing, and I2S. Host nanoseconds must
not be converted into RP2350 CPU percentages.

## Pico listening and timing check

Compare the original and optimized firmware at the same CPU clock, sample rate,
sequence, and volume. Exercise four simultaneous voices, sustained gates, fast
retriggers, slides, and the full timbre ranges. In the existing serial heartbeat,
compare `render_us` average/maximum and the changes in `over`, `underruns`, and
`txstalls`. A buffer's deadline is `SAMPLES_PER_BUFFER / 48000` seconds.

The target check is pending. Host tests and assembly inspection do not establish
that the reported scratching or breakups are fixed. High modulation settings
can also produce aliasing in these existing oscillators even when timing meets
the deadline; listen alongside the counters to distinguish those cases.
