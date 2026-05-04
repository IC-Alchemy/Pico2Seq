# AGENTS.md

This file provides guidance to agents when working with this repository.

Build / run
- Open [`Pico2Seq.ino`](Pico2Seq.ino:1) in Arduino IDE and select the target board.
- Required libraries are listed in [`README.md`](README.md:34).
- No PlatformIO/CMake project is present.

Template scope
- This branch is a stripped-down sequencer template.
- Keep hardware-specific input, display, LED, sensor, audio, and pin-map code out of `src/sequencer/`.
- Adapt hardware by overriding `readHardwareInput(TemplateInputEvent &event)` from another source file; the weak default lives in [`src/input/TemplateInput.cpp`](src/input/TemplateInput.cpp:3).

Project-specific rules
- `ParamId` order must match `CORE_PARAMETERS` in [`src/sequencer/SequencerDefs.h`](src/sequencer/SequencerDefs.h:44).
- Note edits are ignored if a step's Gate is LOW; preserve that rule unless explicitly changing sequencer behavior.
- Internal sequencer indexes are zero-based. Serial commands are one-based where noted in [`README.md`](README.md:58).

Testing & CI
- No automated tests or test framework found in repo.
- Practical validation is Arduino compile plus serial-command and USB-MIDI smoke testing on hardware.
