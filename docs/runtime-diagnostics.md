# Runtime diagnostics

The normal two-second `[DIAG C1]` heartbeat includes the audio renderer's
whole-buffer mean/maximum and a permanent low-overhead stage split:

```text
voices_us=1850 delay_us=310 comp_us=95 misc_us=120
```

All stage values are per-buffer means for the reporting window. `misc_us` is
the remainder of total render time, including PCM16 conversion, dispatch and
profiler overhead. Timing is collected on Core 1; only the queued snapshot is
printed by Core 0.

## Sequencer trace

Send `D` or `d` to the board's 115200-baud USB serial port to toggle detailed
sequencer tracing. It is off after every boot. While enabled, each processed
step prints one `[SEQTRACE]` line per voice with:

- the transport and independent Filter/Attack/Decay lane cursors;
- each lane's raw stored value, composed playback value and published
  `VoiceState` value;
- gate and note state;
- engine, parameter layout, patch-base, filter/envelope enable flags;
- cutoff/attack/decay bases and filter-envelope range.

The three values should normally agree as `raw -> cmp -> pub`. A changing raw
value with a fixed composed value isolates lane composition. A changing
composed value with a fixed published value isolates step publication. If the
published cutoff changes but neither sound nor OLED follows, the fault is
downstream of the sequencer.

Tracing deliberately runs on Core 0 and never prints from the audio core. It
is verbose enough to affect UI/control-loop latency, so use it only for short
captures and send `D` again to turn it off.
