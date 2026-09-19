# MIDI Status and Portable Sequencer Hooks

## Firmware: CDC Only

Pico2Seq does not transmit or receive MIDI. USB MIDI was removed on 2026-09-06;
USB carries power and the TinyUSB CDC serial diagnostics console only. TinyUSB
remains a firmware dependency for CDC. There is no note, CC, Clock, Start or
Stop output, and no external MIDI clock input.

The dormant firmware MIDI module has now also been removed: the former
`MidiManager.cpp`, `MidiManager.h` and `MidiCCConfig.h` contained stub send paths,
unused CC mappings and `MidiNoteManager` bookkeeping for only voices 0 and 1.
No active MIDI subsystem replaces them. `src/midi/README.md` is a removal notice.

## One Note-Duration Authority for All Four Voices

Each `Sequencer` owns its note lifecycle. Core 0 drains uClock's pending PPQN
ticks and calls `Sequencer::tickNoteDuration()` for every voice. When a duration
expires, the updated gate-off `VoiceState` is published through `VoiceManager`
to the audio core without waiting for the next step.

`VoiceSystem` holds only voice IDs and control snapshots; it no longer has
separate gate flags or gate timers. There is no two-voice tracking asymmetry,
no MIDI tracker driving internal gates, and no hardware gate output: the former
gate GPIOs are used by I2S.

## Portable Core: Optional MIDI Callbacks

The portable `Sequencer` retains its optional MIDI callbacks and associated
note handling for other consumers of `pico2seq-core`. Those hooks do not depend
on TinyUSB or Arduino and do not constitute a MIDI transport. The Pico2Seq
firmware does not attach an external MIDI output to them.

If a future application needs MIDI, it must provide transport integration
outside the portable core. Do not reintroduce dormant firmware trackers or a
second duration countdown to implement it.

## Related Documentation

- [Architecture](architecture.md) — dual-core ownership and clock drains
- [VoiceSystem](VoiceSystem.md) — voice IDs, snapshots and routing
- [Sequencer](sequencer.md) — portable note lifecycle and polymetric tracks
- [Scales](scales.md) — scale tables and pitch conversion
