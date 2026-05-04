# MIDI Manager

## Overview

MIDI is the outside path for Pico2Seq. Gates become notes. Parameter movement becomes CC.

This module owns USB MIDI behavior for the sequencer. It sends note-on and note-off messages, keeps each voice monophonic, and provides the central place for MIDI-related logic that was extracted from the main `.ino` file.

## Files

- `MidiManager.h`: Declares the MIDI note manager, note-on/note-off functions, CC update functions, and all-notes-off handling.
- `MidiManager.cpp`: Implements note tracking, monophonic voice behavior, gate-timed note-off handling, CC transmission, and emergency shutdown.
- `MidiCCConfig.h`: Defines CC numbers, value scaling, MIDI channel behavior, and rate limiting.

## Responsibilities

- Send MIDI note events.
- Handle monophonic behavior for each voice.
- Track active note, velocity, channel, gate state, and timing state per voice.
- Send parameter changes as MIDI CC messages.
- Rate-limit CC output so control movement does not flood USB MIDI.
- Provide `allNotesOff()` and emergency-stop paths.

## Voice and Channel Behavior

- Voice count: up to 4 through VoiceSystem.
- MIDI channel: channel 1 for transmitted messages.
- Each voice holds one active note at a time.
- Starting a new note for a voice turns off the previous note for that voice.

## CC Map

- Voice 1: CC71-74 (Octave=71, Decay=72, Attack=73, Filter=74)
- Voice 2: CC75-78 (Octave=75, Decay=76, Attack=77, Filter=78)
- Voice 3: CC79-82 (Octave=79, Decay=80, Attack=81, Filter=82)
- Voice 4: CC83-86 (Octave=83, Decay=84, Attack=85, Filter=86)

## Timing and Safety

- Minimum CC interval: 10ms.
- Parameter values are normalized from `0.0f` to `1.0f`.
- MIDI values are sent from `0` to `127`.
- Atomic update helpers protect shared state across the dual-core firmware.

## Dependencies

- Arduino MIDI library
- Adafruit TinyUSB
- VoiceSystem
- Sequencer timing and note data
- Scale module note conversion
