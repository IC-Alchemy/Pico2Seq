# Pico2Seq MIDI Module

> **2026-09-06:** USB MIDI transmission was removed entirely. `MidiNoteManager` remains as the internal gate/note lifecycle state machine; every send is a stub. USB carries power + the TinyUSB CDC serial console only.

This module retains the internal gate/note lifecycle state machine that outlived the USB
MIDI stack. See the [main README](../../README.md) for overall project context.

---

## Files

- `MidiCCConfig.h`: Dormant configuration constants — former CC number mappings (CC 71–74 for Voice 1, CC 75–78 for Voice 2), rate limiting, and change detection settings. Nothing consumes them for transmission anymore.
- `MidiManager.h`: Interface declarations for `MidiNoteManager`, `MidiNoteTracker`, and `CCParameterState` (the CC plumbing is stub-only).
- `MidiManager.cpp`: Implementation of note lifecycle tracking and gate synchronization; all former send paths are stubs.

---

## Responsibilities & Architecture

- **Monophonic Note Management:** Manages note-on/note-off pairing and gate timing for the software gates of voices 0 and 1 — no external MIDI device is involved.
- **Voice Asymmetry:** Tracks note state for **Voices 0 and 1** (`voice1Tracker` and `voice2Tracker`). Voices 2 and 3 are internal audio synthesis voices with no note bookkeeping.
- **CC Output (dormant):** The former real-time parameter stream (Filter, Attack, Decay, Octave on MIDI Channel 1, rate-limited with change detection) has been dead since the 2026-09-06 removal; the constants and methods survive as stubs.
- **No MIDI Clock:** Nothing is broadcast. uClock drives only the internal sequencer (its ISR runs on Core 0 and merely stages events).

---

## Related Documentation

- [MIDI Subsystem Documentation](../../docs/midi.md): In-depth note lifecycle, dormant CC mappings, and timing details
- [VoiceSystem Documentation](../../docs/VoiceSystem.md): Centralized voice state management
- [Architecture Documentation](../../docs/architecture.md): Dual-core division and system overview
- [Main Project README](../../README.md): Project overview and setup instructions
