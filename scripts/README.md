# Development scripts

Use PowerShell 7 (`pwsh`) and the Arduino CLI dependencies listed in the
[main README](../README.md#prerequisites). Host tests use CMake; see
[Contributing](../CONTRIBUTING.md).

## Compile firmware

From the repository root:

```powershell
pwsh -NoProfile -File scripts/build_pico2seq.ps1 `
  -CpuMHz 225 `
  -BuildDirectory build/pico2seq-225
```

The helper stages only the sketch, boot diagnostics header, and firmware source,
including the pinned submodules. Dependency examples and Git metadata are
excluded. It checks submodule pins and conflict markers before invoking Arduino
CLI. A successful command exits with code 0 and writes nonempty
`Pico2Seq.ino.uf2`, `.elf`, `.bin`, and `.map` files to the build directory.
Keep the matching ELF when diagnosing a crash.

Without `-BuildDirectory`, output goes to a timestamped directory under
`build/arduino-cli/`. An explicit relative build directory is resolved from the
caller's current directory. You can invoke the script by its full path from
another directory, including paths containing spaces.

| Option | Purpose |
|---|---|
| `-ArduinoCli <path>` | Select an Arduino CLI executable; defaults to `arduino-cli` on PATH |
| `-CpuMHz 150\|225\|300` | Select the clock; 225 MHz is the default |
| `-AudioInFlash` | Build the audio code in flash instead of SRAM |
| `-ExtraFlags '<flags>'` | Append compiler flags for a deliberate build variant |
| `-KeepStage` | Retain the temporary sketch, even after failure, for inspection |

Temporary staging is removed by default. Compilation does not upload firmware
or establish physical audio timing, sensor behavior, or display correctness.

## Interactive build and optional upload

```powershell
pwsh -NoProfile -File scripts/build.ps1
```

The wrapper asks for a build title and CPU clock. It writes artifacts to
`build/<title>-<clock>/` and a log to `build/<title>-<clock>-build.log` in this
repository, regardless of the caller's directory. It prints the log tail and
returns a failing status if compilation fails.

To compile and then upload to a connected Pico 2:

```powershell
pwsh -NoProfile -File scripts/build.ps1 -Upload
```

Upload runs only after a successful build and detection of exactly one matching
board. Without `-Upload`, no board needs to be connected.

## Measure audio timing

`measure_audio_timing.ps1` captures USB CDC diagnostics from a running board;
`-UploadDir` optionally flashes a build first. See the
[audio performance guide](../docs/audio-performance.md) for the bench procedure.
Use `-Port` to select a serial port explicitly; automatic detection currently
recognizes Windows COM ports.

Previously captured logs can be summarized without hardware:

```powershell
pwsh -NoProfile -File scripts/measure_audio_timing.ps1 `
  -AnalyzeFile build/audio-timing.log -Label replay
```

Build products and measurement logs stay in ignored build directories.
