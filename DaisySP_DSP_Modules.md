# DaisySP Core DSP Modules Documentation

This document summarizes purpose, APIs, usage, dependencies, and notable implementation details for several core DSP building blocks used in this repository.
 

## 1) delayline.h — Templated delay line
- Purpose: Fixed-size circular delay buffer with integer/fractional delays, linear or Hermite interpolation, plus a helper all-pass section.
- Key API:
  - Init(), Reset()
  - SetDelay(size_t), SetDelay(float)
  - Write(T sample)
  - Read() const, Read(float delay) const, ReadHermite(float delay) const
  - Allpass(T sample, size_t delay, T coeff)
- Usage:
```cpp
DelayLine<float, 48000> dl; dl.Init();
dl.SetDelay(240.5f);
dl.Write(x);
float y = dl.Read();
```
- Impl. notes: write pointer moves "backwards" modulo max_size; linear and Hermite taps are computed from neighboring samples; all-pass returns -a*write + read.
- Deps: <stdlib.h>, <stdint.h>.

## 2–3) limiter.h / limiter.cpp — Peak limiter
- Purpose: Simple peak limiter with smoothed peak follower and soft limiting; processes buffers in place.
- Key API:
  - void Init()
  - void ProcessBlock(float* in, size_t size, float pre_gain)
- Usage:
```cpp
Limiter lim; lim.Init();
lim.ProcessBlock(buf, n, 1.5f);
```
- Impl. notes: Follows |pre| with fast attack (0.05) and very slow release (0.00002); gain = 1/peak_ when >1; applies SoftLimit(pre*gain*0.7f).
- Deps: "dsp.h" for SoftLimit and math helpers.

## 4) onepole.h — One-pole LP/HP filter
- Purpose: Lightweight TPT one-pole filter (low-pass or high-pass). Frequency is normalized (Hz/sample_rate).
- Key API:
  - void Init(), Reset()
  - void SetFrequency(float normFreq)  // 0..~0.5, clipped to 0.497
  - void SetFilterMode(FilterMode {LOW_PASS, HIGH_PASS})
  - float Process(float in)
  - void ProcessBlock(float* in_out, size_t size)
- Usage:
```cpp
OnePole f; f.Init();
f.SetFilterMode(OnePole::FILTER_MODE_LOW_PASS);
f.SetFrequency(2000.f/48000.f);
float y = f.Process(x);
```
- Impl. notes: g = tan(PI*freq); gi = 1/(1+g); lp = (g*in + state)*gi; state = g*(in - lp) + lp; HP uses in - lp.
- Deps: "Utility/dsp.h" (constants), <cmath>.

## 5–6) compressor.h / compressor.cpp — Dynamics compressor
- Purpose: Monophonic compressor with sidechain, multichannel block processing, and auto makeup gain; parameters precompute internals for efficiency.
- Key parameters/APIs:
  - Init(float sr)  // clamps sr to [1,192000]; defaults ratio=2, atk=0.1s, rel=0.1s, thresh=-12 dB, auto-makeup on
  - SetRatio(float r), GetRatio()
  - SetThreshold(float dB), GetThreshold()
  - SetAttack(float s), GetAttack(); SetRelease(float s), GetRelease()
  - AutoMakeup(bool en); SetMakeup(float dB); GetMakeup()
  - GetGain()  // current gain in dB
  - float Process(float in)
  - float Process(float in, float key)  // sidechain
  - float Apply(float in)  // apply previously computed gain
  - ProcessBlock overloads: (in,out,size), (in,out,key,size), (in**,out**,key,channels,size)
- Usage:
```cpp
Compressor c; c.Init(48000);
c.SetThreshold(-18.f); c.SetRatio(4.f);
c.SetAttack(0.01f); c.SetRelease(0.1f);
c.AutoMakeup(true);
c.ProcessBlock(in, out, n);
```
- Impl. notes: Envelope slope_rec_ follows |in| with atk/rel smoothing; over_dB = max(20*log10(slope_rec_) - thresh, 0); gain_rec_ accumulates via atk_slo2_ and ratio_mul_; linear gain_ = 10^(0.05*(gain_rec_+makeup)). Auto makeup ≈ |thresh - thresh/ratio|*0.5.
- Deps: <cmath>, std headers, "compressor.h"; relies on fastlog10f/pow10f utilities.

## 7–8) tone.h / tone.cpp — First-order low-pass “tone” filter
- Purpose: Minimal IIR low-pass; good for gentle tone shaping/smoothing.
- Key API:
  - void Init(float sr)
  - void SetFreq(float hz); float GetFreq()
  - float Process(float in)
- Usage:
```cpp
Tone t; t.Init(48000);
t.SetFreq(5000.f);
float y = t.Process(x);
```
- Impl. notes: omega = 2*pi*freq/sr; b = 2 - cos(omega); c2 = b - sqrt(b*b - 1); c1 = 1 - c2; y = c1*x + c2*y1.
- Deps: <math.h>, constants from dsp utilities.

## 9–10) biquad.h / biquad.cpp — Two-pole recursive filter
- Purpose: Resonant IIR biquad; defaults to a particular low-pass topology.
- Key API:
  - void Init(float sr)  // sets cutoff=500 Hz, res=0.7, computes coeffs
  - void SetCutoff(float hz); void SetRes(float r)
  - float Process(float in)
- Usage:
```cpp
Biquad bq; bq.Init(48000);
bq.SetCutoff(1200.f); bq.SetRes(0.8f);
float y = bq.Process(x);
```
- Impl. notes: Reset() computes a0..a2 and b0..b2 from cutoff/res with trigs; processing uses y = (b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2)/a0; symmetric b0=b1, b2=0 in this design.
- Deps: <math.h>, dsp constants.

---

## Dependencies & Integration Notes
- DSP utility headers (e.g., Utility/dsp.h or dsp.h) provide constants/functions like PI_F, TWOPI_F, fastlog10f, pow10f, SoftLimit, etc. Ensure they are available in your include paths.
- Sample rate usage: Tone, Biquad, and Compressor require sr in Hz at Init; OnePole uses normalized frequency (Hz/sr); DelayLine is sample-rate agnostic but buffer size determines max delay.
- Real-time use: All modules are allocation-free and safe for audio threads; prefer updating parameters at block boundaries to minimize zippering.

## Minimal processing chain example
```cpp
OnePole hp; hp.Init(); hp.SetFilterMode(OnePole::FILTER_MODE_HIGH_PASS);
hp.SetFrequency(100.f/48000.f);
Tone tone; tone.Init(48000); tone.SetFreq(6000.f);
Biquad bq; bq.Init(48000); bq.SetCutoff(3000.f); bq.SetRes(0.75f);
Compressor comp; comp.Init(48000);
Limiter lim; lim.Init();

for (size_t i = 0; i < n; ++i) {
  float x = in[i];
  x = hp.Process(x);
  x = tone.Process(x);
  x = bq.Process(x);
  x = comp.Process(x);
  temp[i] = x;
}
lim.ProcessBlock(temp, n, 1.5f);
memcpy(out, temp, n*sizeof(float));
```