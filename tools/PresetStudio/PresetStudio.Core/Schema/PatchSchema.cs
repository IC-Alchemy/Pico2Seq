using System.Reflection;
using System.Text.Json;

namespace PresetStudio.Schema;

/// <summary>How a value is stored inside the 232-byte patch.</summary>
public enum FieldType { Float, Int32, Byte, Flag }

/// <summary>One selectable value of a choice field (engine, waveform, filter type or mode).</summary>
public sealed record Choice(int Value, string Label);

/// <summary>One editable patch value, exactly as the firmware's field table describes it.</summary>
public sealed class FieldDef
{
    public int Index { get; init; }
    /// <summary>Stable id used in files, e.g. "osc1.level".</summary>
    public string Key { get; init; } = "";
    /// <summary>The short label the box's own display uses.</summary>
    public string Label { get; init; } = "";
    /// <summary>Firmware editor group id ("Osc1", "Filter", ...).</summary>
    public string Group { get; init; } = "";
    public int Offset { get; init; }
    public FieldType Type { get; init; }
    public int Mask { get; init; }
    /// <summary>Number, Percent, Seconds, Semitones, Cents, Hertz, Toggle or Choice.</summary>
    public string Unit { get; init; } = "Number";
    public bool Integer { get; init; }
    public float Min { get; init; }
    public float Max { get; init; }
    public bool Log { get; init; }
    public ShowRule Show { get; init; } = ShowRule.Always;
    public IReadOnlyList<Choice>? Choices { get; init; }

    public bool IsChoice => Choices is not null;
    public bool IsToggle => Type == FieldType.Flag || Unit == "Toggle";

    public override string ToString() => Key;
}

public sealed record EngineIds(int Osc, int Waveguide, int NoiseFx, int Hypersaw, int Recipe)
{
    public int Resolve(string name) => name switch
    {
        "osc" => Osc,
        "waveguide" => Waveguide,
        "noisefx" => NoiseFx,
        "hypersaw" => Hypersaw,
        "recipe" => Recipe,
        _ => throw new FormatException($"unknown engine '{name}' in patch schema"),
    };
}

public sealed record ParamSetIds(int Standard, int Waveguide, int Hypersaw, int NoiseStorm, int HardSync);

public sealed record WaveformIds(int HardSyncSaw, int Square, int BandLimitedSquare, int Noise);

/// <summary>Where the record's identity bytes live (the patch follows at <see cref="PatchOffset"/>).</summary>
public sealed record RecordLayout(
    int NameOffset, int PageOffset, int PadOffset, int BaseIndexOffset, int ColorOffset, int PatchOffset,
    int ParamSetOffset, int PresetIndexOffset, int FlagsOffset, int UsePatchBasesMask);

public sealed record BrowserLayout(
    int FactoryPage, int FirstUserPage, int UserPages, int PadsPerPage, int PageKeyPad,
    int GridWidth, int GridHeight, int MaxPresets, int MaxBankBytes)
{
    /// <summary>Browser pages shown on the box, factory page included.</summary>
    public int BrowserPages => 1 + UserPages;
}

/// <summary>Narrower [min, max] ranges that apply to some fields under a given engine or factory base.</summary>
public sealed class RangeOverrides
{
    public static readonly RangeOverrides None = new(new Dictionary<string, (float, float)>());
    private readonly IReadOnlyDictionary<string, (float Min, float Max)> _ranges;
    public RangeOverrides(IReadOnlyDictionary<string, (float Min, float Max)> ranges) => _ranges = ranges;
    public bool TryGet(string key, out (float Min, float Max) range) => _ranges.TryGetValue(key, out range);
}

/// <summary>
/// The patch layout and every editable value, loaded from <c>patch-schema.json</c>. That file is generated
/// from the firmware's own tables (tests/unit/test_preset_resources.cpp), so the editor and the box cannot
/// disagree about where a value lives or what range it may take.
/// </summary>
public sealed class PatchSchema
{
    public int LayoutVersion { get; private init; }
    public uint TableHash { get; private init; }
    public int RecordSize { get; private init; }
    public int PatchSize { get; private init; }
    public int NameSize { get; private init; }
    public int NameMaxLength => NameSize - 1;
    public RecordLayout Record { get; private init; } = null!;
    public BrowserLayout Browser { get; private init; } = null!;
    public ParamSetIds ParamSets { get; private init; } = null!;
    public WaveformIds Waveforms { get; private init; } = null!;
    public EngineIds Engines { get; private init; } = null!;
    public IReadOnlyList<FieldDef> Fields { get; private init; } = Array.Empty<FieldDef>();
    public IReadOnlyList<string> LaneIds { get; private init; } = Array.Empty<string>();
    public IReadOnlyDictionary<int, RangeOverrides> EngineLimits { get; private init; } = new Dictionary<int, RangeOverrides>();

    private IReadOnlyDictionary<string, FieldDef> _byKey = new Dictionary<string, FieldDef>();

    public FieldDef this[string key] => _byKey.TryGetValue(key, out var f) ? f : throw new KeyNotFoundException($"no patch field '{key}'");
    public bool TryGetField(string key, out FieldDef field) => _byKey.TryGetValue(key, out field!);

    public static PatchSchema Load() => Parse(EmbeddedResources.ReadText("patch-schema.json"));

    public static PatchSchema Parse(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        if (root.GetProperty("format").GetString() != "pico2seq-patch-schema")
            throw new FormatException("not a Pico2Seq patch schema");

        var engines = root.GetProperty("engines");
        var engineIds = new EngineIds(
            engines.GetProperty("osc").GetInt32(), engines.GetProperty("waveguide").GetInt32(),
            engines.GetProperty("noisefx").GetInt32(), engines.GetProperty("hypersaw").GetInt32(),
            engines.GetProperty("recipe").GetInt32());

        var fields = new List<FieldDef>();
        foreach (var f in root.GetProperty("fields").EnumerateArray())
        {
            var type = f.GetProperty("type").GetString() switch
            {
                "float" => FieldType.Float,
                "int32" => FieldType.Int32,
                "byte" => FieldType.Byte,
                "flag" => FieldType.Flag,
                var other => throw new FormatException($"unknown field type '{other}'"),
            };
            List<Choice>? choices = null;
            if (f.TryGetProperty("choices", out var cs))
            {
                choices = new List<Choice>();
                foreach (var c in cs.EnumerateArray())
                    choices.Add(new Choice(c.GetProperty("value").GetInt32(), c.GetProperty("label").GetString()!));
            }
            fields.Add(new FieldDef
            {
                Index = fields.Count,
                Key = f.GetProperty("key").GetString()!,
                Label = f.GetProperty("label").GetString()!,
                Group = f.GetProperty("group").GetString()!,
                Offset = f.GetProperty("offset").GetInt32(),
                Type = type,
                Mask = f.GetProperty("mask").GetInt32(),
                Unit = f.GetProperty("unit").GetString()!,
                Integer = f.GetProperty("integer").GetBoolean(),
                Min = f.GetProperty("min").GetSingle(),
                Max = f.GetProperty("max").GetSingle(),
                Log = f.GetProperty("log").GetBoolean(),
                Show = ShowRule.Parse(f.GetProperty("show").GetString()!, engineIds),
                Choices = choices,
            });
        }

        var rec = root.GetProperty("record");
        var br = root.GetProperty("browser");
        var ps = root.GetProperty("paramSets");
        var wf = root.GetProperty("waveforms");
        var engineLimits = new Dictionary<int, RangeOverrides>();
        foreach (var e in root.GetProperty("engineLimits").EnumerateObject())
            engineLimits[int.Parse(e.Name)] = ReadOverrides(e.Value);

        var schema = new PatchSchema
        {
            LayoutVersion = root.GetProperty("layoutVersion").GetInt32(),
            TableHash = root.GetProperty("tableHash").GetUInt32(),
            RecordSize = root.GetProperty("recordSize").GetInt32(),
            PatchSize = root.GetProperty("patchSize").GetInt32(),
            NameSize = root.GetProperty("nameSize").GetInt32(),
            Record = new RecordLayout(
                rec.GetProperty("nameOffset").GetInt32(), rec.GetProperty("pageOffset").GetInt32(),
                rec.GetProperty("padOffset").GetInt32(), rec.GetProperty("baseIndexOffset").GetInt32(),
                rec.GetProperty("colorOffset").GetInt32(), rec.GetProperty("patchOffset").GetInt32(),
                rec.GetProperty("paramSetOffset").GetInt32(), rec.GetProperty("presetIndexOffset").GetInt32(),
                rec.GetProperty("flagsOffset").GetInt32(), rec.GetProperty("usePatchBasesMask").GetInt32()),
            Browser = new BrowserLayout(
                br.GetProperty("factoryPage").GetInt32(), br.GetProperty("firstUserPage").GetInt32(),
                br.GetProperty("userPages").GetInt32(), br.GetProperty("padsPerPage").GetInt32(),
                br.GetProperty("pageKeyPad").GetInt32(), br.GetProperty("gridWidth").GetInt32(),
                br.GetProperty("gridHeight").GetInt32(), br.GetProperty("maxPresets").GetInt32(),
                br.GetProperty("maxBankBytes").GetInt32()),
            ParamSets = new ParamSetIds(
                ps.GetProperty("standard").GetInt32(), ps.GetProperty("waveguide").GetInt32(),
                ps.GetProperty("hypersaw").GetInt32(), ps.GetProperty("noisestorm").GetInt32(),
                ps.GetProperty("hardsync").GetInt32()),
            Waveforms = new WaveformIds(
                wf.GetProperty("hardSyncSaw").GetInt32(), wf.GetProperty("square").GetInt32(),
                wf.GetProperty("bandLimitedSquare").GetInt32(), wf.GetProperty("noise").GetInt32()),
            Engines = engineIds,
            Fields = fields,
            LaneIds = root.GetProperty("laneIds").EnumerateArray().Select(x => x.GetString()!).ToList(),
            EngineLimits = engineLimits,
        };
        schema._byKey = fields.ToDictionary(f => f.Key);
        return schema;
    }

    /// <summary>Reads <c>{ "key": [min, max], ... }</c>.</summary>
    internal static RangeOverrides ReadOverrides(JsonElement element)
    {
        var map = new Dictionary<string, (float, float)>();
        foreach (var p in element.EnumerateObject())
        {
            var pair = p.Value.EnumerateArray().ToArray();
            map[p.Name] = (pair[0].GetSingle(), pair[1].GetSingle());
        }
        return new RangeOverrides(map);
    }
}

/// <summary>Reads the JSON files compiled into this assembly.</summary>
public static class EmbeddedResources
{
    public static string ReadText(string fileName)
    {
        var assembly = typeof(EmbeddedResources).Assembly;
        using var stream = assembly.GetManifestResourceStream("PresetStudio.Resources." + fileName)
            ?? throw new FileNotFoundException($"embedded resource '{fileName}' is missing");
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }
}
