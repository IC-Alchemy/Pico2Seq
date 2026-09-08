# Pico2Seq MIDI Module

> **2026-09-06:** USB MIDI transmission was removed entirely. `MidiNoteManager` remains as the internal gate/note lifecycle state machine; every send is a stub. USB carries power + the TinyUSB CDC serial console only.

This module maintains the *internal* MIDI-style note lifecycle for Pico2Seq — it transmits nothing. See the [main README](../../README.md) for overall project context.

---

## Files

- `MidiCCConfig.h`: Configuration constants and legacy CC number mappings (CC 71–74 for Voice 1, CC 75–78 for Voice 2) kept for the internal CC bookkeeping (rate limiting, change detection).
- `MidiManager.h`: Interface declarations for `MidiNoteManager`, `MidiNoteTracker`, and `CCParameterState`. (The former `usb_midi` extern is gone.)
- `MidiManager.cpp`: Implementation of note lifecycle tracking and gate synchronization; every former send is an empty stub.

---

## Responsibilities & Architecture

- **Monophonic Note Management:** Tracks note-on/note-off pairing and gate timing for voices 0 and 1, exactly as if they were going out over MIDI. Call sites: `StepPlayback::processSequencerStep()` (note events) and `ClockService::processPendingGateTicks()` → `updateTiming(gateTick)` (note-off timing); `onClockStop` triggers `onSequencerStop()` cleanup.
- **Voice Asymmetry:** Only **Voices 0 and 1** (`voice1Tracker` and `voice2Tracker`) participate. Voices 2 and 3 are audio-only.
- **CC Bookkeeping (vestigial):** The CC 71–78 mappings and rate limiting still run (`updateParameterCC` from `StepPlayback`), but `shouldTransmitCC` gates a stub — nothing leaves the device.
- **No MIDI I/O:** There is no USB MIDI input or output path (removed 2026-09-06) and no realtime clock broadcast. USB carries power + the CDC serial console only.

---

## Related Documentation

- [MIDI Subsystem Documentation](../../docs/midi.md): In-depth CC mappings, voice asymmetry, and timing details
- [VoiceSystem Documentation](../../docs/VoiceSystem.md): Centralized voice state management
- [Architecture Documentation](../../docs/architecture.md): Dual-core division and system overview
- [Main Project README](../../README.md): Project overview and setup instructions
