# Pico2Seq documentation

For development setup and pull requests, start with [Contributing](../CONTRIBUTING.md).
For playing the instrument, start with the [user manual](manual.md).

## Architecture and development

| Guide | What it covers |
|---|---|
| [Firmware structure](firmware-structure.md) | Entry points, module boundaries, and where to make a change |
| [Architecture](architecture.md) | Core ownership, startup, and data flow |
| [Testing](testing.md) | Host suites, hardware stubs, and known failures |
| [Audio performance](audio-performance.md) | Timing, SRAM, stack, and hardware measurements |
| [Recipe performance](recipe-performance.md) | Recipe-engine optimization and measurements |
| [Persistence](persistence.md) | Project snapshots, codecs, flash, and retained state |

## Synthesis and sequencing

| Guide | What it covers |
|---|---|
| [Voices](voice.md) | Oscillators, filters, envelopes, and the master bus |
| [Voice system](VoiceSystem.md) | Voice identifiers and control snapshots |
| [Voice editing](voice-edit.md) | Patch controls and sequenced modifiers |
| [Sequencer](sequencer.md) | Parameter tracks, polymeter, gates, and clocking |
| [Arpeggiator](arpeggiator.md) | Chord input, patterns, and playback |
| [Scales](scales.md) | Scale rows and note lookup |
| [Tuning](tuning.md) | Tuning library, pitch calculations, and the Tuning page |

## Controls and hardware

| Guide | What it covers |
|---|---|
| [Buttons](ButtonHandlers.md) | Control surface state machines and dispatch |
| [Satellite link](alchemy-satellite-link.md) | Tile packets, freshness, and bus robustness |
| [Tile firmware](../tiles/README.md) | PY32 slider/button firmware and its protocol |
| [Touch matrix](matrix.md) | Capacitive pads and voice banks |
| [LED matrix](LEDMatrix.md) | Themes, playheads, and pad feedback |
| [OLED](oled.md) | Display priorities and screens |
| [Sensors](sensors.md) | Encoder and distance control |
| [USB and MIDI status](midi.md) | CDC console and the removed MIDI transport |

## Historical notes

The [interactive codebase map](codebase-map.html) is a standalone HTML snapshot
of commit `229079b`. Open it locally in a browser. Its details and source line
numbers describe that snapshot; use the firmware and architecture guides above
for current behavior.

[Development observations](next-steps.md) records dated findings and proposed
work. Check each finding against current code before treating it as an open issue.
