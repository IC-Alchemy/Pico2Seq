# A larger window onto the pattern

The 3.5-inch panel turns the sequencer into a compact scientific instrument.
Colour identifies a voice, space identifies a lane, and the playhead stays
attached to that lane's actual loop. The visual style can change without
changing the music.

## Select a display

Change **one setting** in `src/display/DisplayConfig.h`:

```cpp
#define PICO2SEQ_DISPLAY 0  // existing 128x64 SH1106
// or
#define PICO2SEQ_DISPLAY 1  // 3.5-inch 320x480 SPI ST7796
```

The default remains SH1106, so an existing instrument does not unexpectedly
change its pins or controls. A compiler flag `-DPICO2SEQ_DISPLAY=1` also works.
The TFT is portrait, rotation 0. Rotation 2 is available for an upside-down
mount. Landscape rotations deliberately fail a compile-time check.

This supports **SPI ST7796/ST7796S modules**, not 8/16-bit parallel shields.
Touch and SD-card hardware on a module are not enabled. This is a screen
upgrade, not an additional input surface.

Install **GFX Library for Arduino** (Arduino_GFX, tested with 1.6.8) alongside
the existing Adafruit GFX and SH110X libraries. SH110X is still used as a
RAM-only contextual renderer in a TFT build; an OLED need not be connected.
An SH1106 build does not include or require Arduino_GFX.

## Wiring

The assignments are centralized in `src/app/HardwarePins.h`.

| ST7796 SPI signal | Pico 2 GPIO |
|---|---|
| SCK / CLK | GP18 |
| MOSI / SDI / DIN | GP19 |
| CS | GP17 |
| D/C / RS | GP20 |
| RESET / RST | GP21 |
| GND | GND |
| MISO / SDO | Not connected; GP16 is reserved by the SPI backend |

Follow **your module's** VCC and backlight specifications. Some boards have
regulators and LED drivers; a bare panel does not. Pico GPIO uses 3.3 V logic.
Do not drive an unknown backlight directly from a GPIO, and do not put 5 V on
a Pico signal input. Connect backlight power through the module's documented
driver or an appropriate external driver. Keep unused touch/SD chip selects
inactive if they share the SPI wiring.

These GPIO choices avoid the live I2C buses, GP7 mode strap, GP8 touch IRQ,
GP1 LEDs, and GP10–12 I2S pins. They do not prove that an external enclosure
or wiring harness leaves those pins free.

SPI starts at a conservative 24 MHz. `kInverted` in the config is available
for modules requiring inverted colour polarity. A successful `begin()` means
the SPI peripheral initialized, **not** that a write-only panel was detected.

## Two new views

### Lane Matrix

Two columns by eight rows: sixteen small multiples, not sixteen unrelated
widgets. The columns are the same voice pair as the physical step pads.
Selecting Voice 1 or 2 shows voices 1–2; selecting Voice 3 or 4 shows voices
3–4. Each column keeps its preset name and a distinct voice colour.

There are eleven real sequencer lanes, so two eight-row banks expose all of
them without squeezing the graphs smaller. The main bank contains Note,
Velocity, Filter, Attack, Release, Octave, Gate Length and Gate. The extended
bank adds Decay, Sustain and Slide, alongside repeated comparison lanes.
There are no invented parameters and no hidden envelope lanes.

Preset-specific labels follow the existing sound-engine bindings. A lane
that controls a recipe's Ratio or a string's Brightness should say so.
Current values use the existing musical formatter: notes, Hz, ms/s, ratios,
gain, octaves and gate states rather than anonymous normalized percentages.

### Loop Observatory

The four voices are shown together as aligned comparisons and independent
loop-phase markers. This view answers “what is changing together?” and “why
are those events drifting apart?” without implying all lanes have the Gate
lane's cursor. See the [visualization guide and demos](display/README.md) for
each encoding, both banks and the style comparison sheet.

### Focus, preserved and framed

The existing editor/status content remains available as Focus, enlarged 2x
and framed with all four gate timelines. Preset browsing, ADSR, reverb,
tuning, arpeggiator mode, step editing and transient confirmations temporarily
take priority over the overviews. The chosen overview resumes afterward.
This deliberately reuses the existing contextual renderer instead of making
a second copy of every musical formatting and modal-priority rule.

## Controls

With the TFT selected, in **Utility mode**, outside modal editors:

| Gesture | Result |
|---|---|
| Tap Button 4 (Swing) | Advance the existing swing template, on release |
| Hold Button 4 for 400 ms | Matrix main → Matrix extended → Observatory → Focus → repeat |
| Hold Shift + Button 4 for 400 ms | Next chart style |
| Voice 1–4 buttons | Existing voice selection; Matrix follows its voice pair |

Shift is captured when Swing is pressed. A display hold is consumed and does
**not** also change swing when released. Entering a modal editor or leaving
Utility mode cancels an unfinished display gesture. Existing Param-mode and
SH1106 controls are unchanged. View/style choices are UI state only, reset at
boot and not added to the song's persistence format.

## What the graphs mean

Graphs show **composed playback values**, including patch-following values,
not the raw `-1` patch-follow sentinel. Height/radius/intensity is quantized
to 0–255 across the parameter's defined lane range. This is a comparative
lane coordinate, not a measured audio waveform, spectrum, envelope trace or
oscilloscope voltage. The textual readout carries the musical units.

Each lane keeps its own absolute step count, loop start and current cursor.
Steps excluded by a nonzero loop start are dim. A 64-step lane remains a
64-step lane, not a silently truncated 16-step graph. Sparse/dense styles
offer different compromises when the number of steps exceeds a small chart's
visual resolution.

## Rendering and verification

Only Core 0 owns the display. Core 1's audio path is untouched.
The TFT uses a fixed **320×8 RGB565 strip (5120 bytes)**, fixed lane snapshots
and a 1 KB contextual bitmap. There is no 320×480 framebuffer and no new
DMA-channel claim. One lane is captured or one strip is rendered per display
pass. Changed strips are sent over SPI; unchanged strips are skipped using
a 64-bit fingerprint. Small strips bound the SPI payload, but do not prove
the resulting control latency or audio timing on a real board.

Snapshots are captured incrementally, so they are an observation window,
not an atomic simultaneous measurement of four voices. Playheads advance
in step-sized increments, not synthesized sub-step interpolation.

Compile both backends using the existing helper, at the project's 225 MHz
baseline:

```powershell
pwsh scripts/build_pico2seq.ps1 -CpuMHz 225 -NoWorkingCopy `
  -BuildDirectory build/st7796 -ExtraFlags '-DPICO2SEQ_DISPLAY=1'
pwsh scripts/build_pico2seq.ps1 -CpuMHz 225 -NoWorkingCopy `
  -BuildDirectory build/sh1106 -ExtraFlags '-DPICO2SEQ_DISPLAY=0'
```

The portable display tests use the `[display]` tag in the host suite.
Demo images are **synthetic host renders**, not device screenshots.

Before accepting the hardware upgrade, verify:

1. Portrait orientation, colour polarity, legibility and backlight power.
2. All eleven lanes on both voice pairs; nonzero loop starts and polymeter.
3. A long Swing hold changes only the view, not the groove; a tap still swings.
4. Presets/tuning/editing/save notices override and restore the chosen view.
5. Control response and audio underrun counters while changing chart styles,
   recording with lidar, and running all four voices. Listen for unchanged
   timing and sound. A compile or a host render cannot establish these.
