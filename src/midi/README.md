# Firmware MIDI Module Removed

The dormant `MidiManager.cpp`, `MidiManager.h` and `MidiCCConfig.h` have been
removed. Their stub send paths and two-voice note trackers are no longer part
of the firmware. All four voices use the sequencer's note-duration lifecycle;
`VoiceSystem` holds IDs and control snapshots only.

USB remains TinyUSB CDC-only (power and serial diagnostics), with no MIDI
input, notes, CC or clock output. Optional MIDI callbacks in the portable
sequencer remain available to other applications; they are not a firmware
transport.

See [MIDI status](../../docs/midi.md), [VoiceSystem](../../docs/VoiceSystem.md)
and the [architecture](../../docs/architecture.md).
