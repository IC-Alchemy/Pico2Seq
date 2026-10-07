# Pico2Seq Preset Studio

A Windows program for designing voice presets with sliders and plain-language help, hearing them live on the
Pico, giving them a name, an LED colour and a pad on the preset browser's extra pages, and sending them to the
Pico over USB.

**Start with [docs/preset-studio.md](../../docs/preset-studio.md)** — the player's guide, the wire protocol, the
file formats and how to change the layout.

```
PresetStudio.Core/        engine, no UI: schema, codec, files, USB link, view models (net8.0, builds anywhere)
PresetStudio.App/         the WPF windows (net8.0-windows)
PresetStudio.Core.Tests/  xUnit; includes tests that drive the firmware's own link code (tests/tools/preset_link_sim)
```

```powershell
dotnet run  --project tools/PresetStudio/PresetStudio.App    # Windows
dotnet test tools/PresetStudio/PresetStudio.Core.Tests        # any OS
scripts/build_preset_studio.ps1                               # publish a single exe
```

`PresetStudio.Core/Resources/*.json` (except `field-help.json`) are generated from the firmware by
`tests/unit/test_preset_resources.cpp`; do not edit them by hand (docs §5.7–5.8).
