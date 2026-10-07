namespace PresetStudio.Schema;

/// <summary>What a rule needs to know about a preset to decide whether a value applies to it.</summary>
public interface IShowContext
{
    int Engine { get; }
    int OscillatorCount { get; }
    int Waveform(int oscillator);
    bool FilterEnabled { get; }
    bool LadderFilter { get; }
    bool OverdriveEnabled { get; }
    bool EnvelopeEnabled { get; }
    /// <summary>Name of the recipe the base preset carries ("Feedback FM"), or null for non-recipe bases.</summary>
    string? Recipe { get; }
}

/// <summary>
/// When a value applies. The same rules the box uses to hide controls on its own display
/// (<c>VoiceEdit::available</c>); the C++ tests check the firmware table against them.
/// </summary>
public sealed class ShowRule
{
    private enum Kind { Always, Engine, Oscillators, Pulse, Harmony1, Filter, Ladder, Overdrive, Envelope, Recipe }

    private readonly Kind _kind;
    private readonly int _arg;
    private readonly string? _recipe;

    public static readonly ShowRule Always = new("always", Kind.Always, 0, null);

    /// <summary>The rule as written in the schema file.</summary>
    public string Text { get; }

    private ShowRule(string text, Kind kind, int arg, string? recipe)
    {
        Text = text;
        _kind = kind;
        _arg = arg;
        _recipe = recipe;
    }

    public static ShowRule Parse(string text, EngineIds engines)
    {
        if (text == "always") return Always;
        if (text.StartsWith("engine=", StringComparison.Ordinal))
            return new ShowRule(text, Kind.Engine, engines.Resolve(text["engine=".Length..]), null);
        if (text.StartsWith("osc>=", StringComparison.Ordinal))
            return new ShowRule(text, Kind.Oscillators, int.Parse(text["osc>=".Length..]), null);
        if (text.StartsWith("pulse", StringComparison.Ordinal))
            return new ShowRule(text, Kind.Pulse, int.Parse(text["pulse".Length..]), null);
        if (text.StartsWith("recipe=", StringComparison.Ordinal))
            return new ShowRule(text, Kind.Recipe, engines.Recipe, text["recipe=".Length..]);
        return text switch
        {
            "harmony1" => new ShowRule(text, Kind.Harmony1, 0, null),
            "filter" => new ShowRule(text, Kind.Filter, 0, null),
            "ladder" => new ShowRule(text, Kind.Ladder, 0, null),
            "overdrive" => new ShowRule(text, Kind.Overdrive, 0, null),
            "envelope" => new ShowRule(text, Kind.Envelope, 0, null),
            _ => throw new FormatException($"unknown show rule '{text}' in patch schema"),
        };
    }

    public bool Evaluate(IShowContext c, PatchSchema schema)
    {
        bool Osc(int n) => c.Engine == schema.Engines.Osc && c.OscillatorCount >= n;
        bool Square(int wave) => wave == schema.Waveforms.Square || wave == schema.Waveforms.BandLimitedSquare;
        return _kind switch
        {
            Kind.Always => true,
            Kind.Engine => c.Engine == _arg,
            Kind.Oscillators => Osc(_arg),
            Kind.Pulse => Osc(_arg) && Square(c.Waveform(_arg - 1)),
            Kind.Harmony1 => c.Engine != schema.Engines.Osc || Osc(1),
            Kind.Filter => c.FilterEnabled,
            Kind.Ladder => c.FilterEnabled && c.LadderFilter,
            Kind.Overdrive => c.OverdriveEnabled,
            Kind.Envelope => c.EnvelopeEnabled,
            Kind.Recipe => c.Engine == schema.Engines.Recipe && c.Recipe == _recipe,
            _ => true,
        };
    }

    /// <summary>Plain-language reason a value is greyed out, for tooltips and the help panel.</summary>
    public string WhyUnavailable(PatchSchema schema)
    {
        string EngineName(int id) =>
            id == schema.Engines.Osc ? "Oscillators"
            : id == schema.Engines.Waveguide ? "Waveguide (plucked string)"
            : id == schema.Engines.NoiseFx ? "Noise FX"
            : id == schema.Engines.Hypersaw ? "Hypersaw"
            : "Recipe";
        return _kind switch
        {
            Kind.Always => "",
            Kind.Engine => $"Only used by the {EngineName(_arg)} engine. Change Engine on the Source tab to use it.",
            Kind.Oscillators => $"Only used when the Oscillators engine has at least {_arg} oscillator{(_arg > 1 ? "s" : "")}.",
            Kind.Pulse => $"Only used while oscillator {_arg} is set to a square wave.",
            Kind.Harmony1 => "Only used by the Oscillators engine (oscillator 1) and the other pitched engines.",
            Kind.Filter => "Only used while the main filter is switched on.",
            Kind.Ladder => "Only used by the ladder filter (the state-variable filter has no drive or passband gain).",
            Kind.Overdrive => "Only used while overdrive is switched on.",
            Kind.Envelope => "Only used while the envelope is switched on.",
            Kind.Recipe => $"Only used by the {_recipe} recipe, so the base preset must be built on it.",
            _ => "",
        };
    }

    public override string ToString() => Text;
}
