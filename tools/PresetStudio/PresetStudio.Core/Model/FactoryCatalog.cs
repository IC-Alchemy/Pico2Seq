using System.Text.Json;
using PresetStudio.Schema;

namespace PresetStudio.Model;

/// <summary>How a sequencer lane (Velocity, Filter, Attack, ...) behaves on one factory preset.</summary>
public sealed record LaneInfo(string Lane, string Name, string? Field);

/// <summary>The span and shape of a recipe macro on one factory preset.</summary>
public sealed record MacroInfo(string Label, string Unit, string Curve, float Min, float Max, float? Center);

/// <summary>How the filter cutoff (0..1) becomes Hz on one factory preset.</summary>
public sealed record CutoffInfo(float Min, float Max, string Curve, float? Center);

/// <summary>One factory preset: the starting point a user preset is built on.</summary>
public sealed class FactoryPreset
{
    public int Index { get; init; }
    public string Name { get; init; } = "";
    public int Engine { get; init; }
    public int ParamSet { get; init; }
    /// <summary>"Feedback FM", "Prism", ...; null when the preset has no recipe.</summary>
    public string? Recipe { get; init; }
    public PatchValues Values { get; init; } = null!;
    /// <summary>Ranges that are narrower than the schema's for this preset's layout.</summary>
    public RangeOverrides Limits { get; init; } = RangeOverrides.None;
    public IReadOnlyList<LaneInfo> Lanes { get; init; } = Array.Empty<LaneInfo>();
    /// <summary>Always three entries; null where the preset does not use that macro.</summary>
    public IReadOnlyList<MacroInfo?> Macros { get; init; } = Array.Empty<MacroInfo?>();
    public CutoffInfo Cutoff { get; init; } = new(120, 5000, "exp", null);
    public IReadOnlyList<(float X, float Hz)> CutoffSamples { get; init; } = Array.Empty<(float, float)>();

    public bool HasRecipe => Recipe is not null;
    public override string ToString() => Name;
}

/// <summary>The factory bank as shipped with this build of the editor.</summary>
public sealed class FactoryCatalog
{
    public IReadOnlyList<FactoryPreset> Presets { get; }
    public int LayoutVersion { get; }

    private FactoryCatalog(int layoutVersion, IReadOnlyList<FactoryPreset> presets)
    {
        LayoutVersion = layoutVersion;
        Presets = presets;
    }

    public FactoryPreset this[int index] => Presets[index];
    public int Count => Presets.Count;

    public FactoryPreset? FindByName(string name) =>
        Presets.FirstOrDefault(p => string.Equals(p.Name, name, StringComparison.OrdinalIgnoreCase));

    public static FactoryCatalog Load(PatchSchema schema) =>
        Parse(EmbeddedResources.ReadText("factory-presets.json"), schema);

    public static FactoryCatalog Parse(string json, PatchSchema schema)
    {
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        if (root.GetProperty("format").GetString() != "pico2seq-factory-presets")
            throw new FormatException("not a Pico2Seq factory preset list");
        var layout = root.GetProperty("layoutVersion").GetInt32();
        if (layout != schema.LayoutVersion)
            throw new FormatException($"factory presets (layout {layout}) do not match the schema (layout {schema.LayoutVersion})");

        var presets = new List<FactoryPreset>();
        foreach (var p in root.GetProperty("presets").EnumerateArray())
        {
            var values = new PatchValues(schema);
            foreach (var v in p.GetProperty("values").EnumerateObject())
                values[v.Name] = v.Value.GetSingle();

            var lanes = p.GetProperty("lanes").EnumerateArray().Select(l => new LaneInfo(
                l.GetProperty("lane").GetString()!, l.GetProperty("name").GetString()!,
                l.GetProperty("field").ValueKind == JsonValueKind.Null ? null : l.GetProperty("field").GetString())).ToList();

            var macros = p.GetProperty("macros").EnumerateArray().Select(m => m.ValueKind == JsonValueKind.Null
                ? null
                : new MacroInfo(m.GetProperty("label").GetString()!, m.GetProperty("unit").GetString()!,
                    m.GetProperty("curve").GetString()!, m.GetProperty("min").GetSingle(), m.GetProperty("max").GetSingle(),
                    m.GetProperty("center").ValueKind == JsonValueKind.Null ? null : m.GetProperty("center").GetSingle())).ToList();

            var c = p.GetProperty("cutoff");
            var samples = c.GetProperty("samples").EnumerateArray()
                .Select(s => (s[0].GetSingle(), s[1].GetSingle())).ToList();

            presets.Add(new FactoryPreset
            {
                Index = p.GetProperty("index").GetInt32(),
                Name = p.GetProperty("name").GetString()!,
                Engine = p.GetProperty("engine").GetInt32(),
                ParamSet = p.GetProperty("paramSet").GetInt32(),
                Recipe = p.GetProperty("recipe").ValueKind == JsonValueKind.Null ? null : p.GetProperty("recipe").GetString(),
                Values = values,
                Limits = PatchSchema.ReadOverrides(p.GetProperty("limits")),
                Lanes = lanes,
                Macros = macros,
                Cutoff = new CutoffInfo(c.GetProperty("min").GetSingle(), c.GetProperty("max").GetSingle(),
                    c.GetProperty("curve").GetString()!,
                    c.GetProperty("center").ValueKind == JsonValueKind.Null ? null : c.GetProperty("center").GetSingle()),
                CutoffSamples = samples,
            });
        }
        for (int i = 0; i < presets.Count; i++)
            if (presets[i].Index != i)
                throw new FormatException("factory presets are not in index order");
        return new FactoryCatalog(layout, presets);
    }

    /// <summary>A new preset that sounds exactly like a factory preset, ready to be edited.</summary>
    public UserPreset NewFrom(int factoryIndex, PatchSchema schema)
    {
        var f = Presets[factoryIndex];
        return new UserPreset
        {
            Name = f.Name.Length > schema.NameMaxLength ? f.Name[..schema.NameMaxLength] : f.Name,
            BaseIndex = f.Index,
            Values = f.Values.Clone(),
        };
    }
}
