# Pico Mudras Sequencer
Fully featured sequencer project for the Raspberry Pi Pico2 microcontroller.
Currently under active development; audio output and MIDI output already work.

### Goal:  
Make a step sequencer that:
feels like performing live instead of turning knobs

This project expands on that concept of my Mudras Moduleby using the same LIDAR technology to
sequence multiple parameters on 4 separate synth voices.


# Hardware

This sequencer needs a couple of custom pieces of hardware (touch matrix and
magnetic encoder).  
I will explain how to make them here at some point but if you want to use a Pico
for DSP now, check out
https://github.com/IC-Alchemy/Pico-DSP-Garden

### Microcontroller: Raspberry Pi Pico2 (RP2350)
### Audio Codec: Uses standard I2S audio output (compatible with common, easy-to-find audio codecs)
### User Interface: 4x8 Touch matrix w/ MPR121, OLED display, distance sensor, home made magnetic encoder

I need to upload guides to making the touch matrix and magnetic encoder.

### What Makes it Unique

This is a 4-voice polyphonic sequencer with:

Most parameters have per-step modulation; each parameter
has its own track and adjustable length for **easy polymetric sequencing**

Real-time parameter control via **LIDAR sensor**
Per-step parameter control with a **magnetic encoder**
Step sequencing with **per-step parameter automation**
MIDI I/O support

**Recent Architecture Improvements:**
- Centralized VoiceSystem for efficient voice management
- Array-based voice access patterns for better maintainability
- Scalable design supporting easy voice count modifications
- Reduced code duplication across the codebase

Key Files

src/voice/Voice.cpp - The heart of the DSP engine
src/voice/VoiceSystem.h - Centralized voice management architecture
Pico2Seq.ino - Main Arduino sketch
src/audio/ - I2S audio interface for the Pico2
src/sequencer/ - Step sequencer logic
src/dsp/ - DSP building blocks (filters, oscillators, effects)
src/ui/UIState.h - UI state management with array-based voice handling

Getting Started

This is an Arduino project designed for the Raspberry Pi Pico2.
You'll need the Arduino IDE with RP2040/RP2350 board support and the
required libraries for your peripherals (MPR121, OLED, and your LIDAR sensor).

You DO NOT need to install the DaisyDuino library from the Arduino IDE
(it is fine if you did).  This project uses modified versions of some of the
DaisySP files and they are all included in src/dsp.

The audio output uses I2S, so you'll need an I2S-compatible DAC/codec.
Most common audio breakout boards should work fine.

Basic workflow:
Open PicoMudrasSequencer.ino in Arduino IDE, select the Raspberry Pi Pico2 board,
choose the correct serial port, then compile and upload.

License

MIT License - see LICENSE file for details.
