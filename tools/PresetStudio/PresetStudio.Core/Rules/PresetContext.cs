using PresetStudio.Model;
using PresetStudio.Schema;

namespace PresetStudio.Rules;

/// <summary>Adapts a preset (its values plus the factory preset it is built on) to <see cref="IShowContext"/>.</summary>
public sealed class PresetContext : IShowContext
{
    private readonly PatchValues _values;
    private readonly FactoryPreset? _base;

    public PresetContext(PatchValues values, FactoryPreset? baseFactory)
    {
        _values = values;
        _base = baseFactory;
    }

    public int Engine => (int)_values["source.engine"];
    public int OscillatorCount => (int)_values["source.oscCount"];
    public int Waveform(int oscillator) => (int)_values[$"osc{Math.Clamp(oscillator, 0, 2) + 1}.wave"];
    public bool FilterEnabled => _values["filter.enabled"] >= 0.5f;
    public bool LadderFilter => (int)_values["filter.type"] == 0;
    public bool OverdriveEnabled => _values["drive.enabled"] >= 0.5f;
    public bool EnvelopeEnabled => _values["env.enabled"] >= 0.5f;
    public string? Recipe => _base?.Recipe;
}
