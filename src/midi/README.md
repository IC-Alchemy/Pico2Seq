# MIDI Manager

This module is responsible for handling MIDI communications for the PicoMudrasSequencer.

## Files

- `MidiManager.h`: Header file declaring the functions for sending MIDI note on/off messages for both voices, and an `allNotesOff` function.
- `MidiManager.cpp`: Implementation of the MIDI functions. This code was extracted from the main `.ino` file to improve modularity.

## Responsibilities

- Sending MIDI note events.
- Handling monophonic behavior for each voice.
- Providing a central place for all MIDI-related logic.
