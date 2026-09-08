# Pico2Seq MIDI Module

> **2026-09-06:** USB MIDI transmission was removed entirely. `MidiNoteManager` remains as the internal gate/note lifecycle state machine; every send is a stub. USB carries power + the TinyUSB CDC serial console only.

This module is responsible for handling USB MIDI communication for Pico2Seq. See the [main README](../../README.md) for overall project context.

---

## Files

- `MidiCCConfig.h`: Configuration constants, CC number mappings (CC 71–74 for Voice 1, CC 75–78 for Voice 2), rate limiting, and change detection settings.
- `MidiManager.h`: Interface declarations for `MidiNoteManager`, `MidiNoteTracker`, `CCParameterState`, and extern USB MIDI interface (`usb_midi`).
- `MidiManager.cpp`: Implementation of note lifecycle tracking, gate synchronization, and CC transmission.

---

## Responsibilities & Architecture

- **Monophonic Note Management:** Manages note-on/note-off pairing and gate timing for external MIDI devices.
- **Voice Asymmetry:** Manages MIDI note and CC transmission for **Voices 0 and 1** (`voice1Tracker` and `voice2Tracker`). Voices 2 and 3 are internal audio synthesis voices and do not emit MIDI events.
- **Continuous Controller (CC) Output:** Sends real-time parameter changes (Filter, Attack, Decay, Octave) on MIDI Channel 1 with rate limiting and change detection.
- **Realtime MIDI Clock:** Broadcasts `Clock` (24 PPQN), `Start`, and `Stop` messages synchronized with `uClock` on Core 1.

---

## Related Documentation

- [MIDI Subsystem Documentation](../../docs/midi.md): In-depth CC mappings, voice asymmetry, and timing details
- [VoiceSystem Documentation](../../docs/VoiceSystem.md): Centralized voice state management
- [Architecture Documentation](../../docs/architecture.md): Dual-core division and system overview
- [Main Project README](../../README.md): Project overview and setup instructions
