using PresetStudio.Model;
using PresetStudio.Schema;

namespace PresetStudio.Rules;

/// <summary>The range a value may take, and how the editor should label it, for one particular preset.</summary>
public readonly record struct ResolvedField(float Min, float Max, string Label, string Unit, string? Curve = null);

/// <summary>
/// A field's allowed range depends on the voice it ends up in: the base preset's lane layout narrows some
/// ranges (a recipe's macros, an oscillator voice's attack), and changing the engine falls back to that
/// engine's default layout. This mirrors how the box builds the voice (<c>voicecodec::applyPatch</c>).
/// </summary>
public sealed class LimitResolver
{
    private readonly PatchSchema _schema;
    private readonly FactoryCatalog _factory;

    public LimitResolver(PatchSchema schema, FactoryCatalog factory)
    {
        _schema = schema;
        _factory = factory;
    }

    public ResolvedField Resolve(FieldDef field, UserPreset preset)
    {
        var (min, max) = (field.Min, field.Max);
        var engine = (int)preset.Values["source.engine"];
        FactoryPreset? baseFactory = preset.BaseIndex >= 0 && preset.BaseIndex < _factory.Count ? _factory[preset.BaseIndex] : null;

        // The base preset's own layout only survives while the engine is unchanged.
        RangeOverrides? overrides = null;
        if (baseFactory is not null && baseFactory.Engine == engine)
            overrides = baseFactory.Limits;
        else if (_schema.EngineLimits.TryGetValue(engine, out var byEngine))
            overrides = byEngine;
        if (overrides is not null && overrides.TryGet(field.Key, out var r))
            (min, max) = r;

        var label = field.Label;
        var unit = field.Unit;
        string? curve = null;
        if (engine == _schema.Engines.Recipe && baseFactory is not null && field.Key.StartsWith("recipe.macro", StringComparison.Ordinal))
        {
            var macro = baseFactory.Macros.ElementAtOrDefault(field.Key[^1] - '1');
            if (macro is not null)
            {
                label = macro.Label;
                unit = macro.Unit == "Ratio" ? "Number" : macro.Unit;
                (min, max) = (macro.Min, macro.Max);
                curve = macro.Curve;
            }
        }
        return new ResolvedField(min, max, label, unit, curve);
    }

    /// <summary>Clamps a value into its allowed range and rounds integer fields.</summary>
    public float Clamp(FieldDef field, UserPreset preset, float value)
    {
        if (float.IsNaN(value) || float.IsInfinity(value)) return Resolve(field, preset).Min;
        if (field.IsChoice)
        {
            var choices = field.Choices!;
            var nearest = choices.OrderBy(c => Math.Abs(c.Value - value)).First();
            return nearest.Value;
        }
        var r = Resolve(field, preset);
        var v = Math.Clamp(value, r.Min, r.Max);
        return field.Integer || field.IsToggle ? MathF.Round(v, MidpointRounding.AwayFromZero) : v;
    }
}
