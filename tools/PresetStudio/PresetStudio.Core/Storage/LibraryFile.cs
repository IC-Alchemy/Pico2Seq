using System.Globalization;
using System.Text;
using System.Text.Json;
using PresetStudio.Model;
using PresetStudio.Schema;

namespace PresetStudio.Storage;

/// <summary>A named collection of presets: what you edit, save on your computer and send to the box.</summary>
public sealed class PresetLibrary
{
    public string Name { get; set; } = "My Presets";
    public List<UserPreset> Presets { get; } = new();
}

/// <summary>
/// Library files (<c>.p2lib</c>) and single-preset files (<c>.p2preset</c>) are readable JSON. Presets are
/// stored by value with the factory preset they are built on named (not numbered), so a file survives a
/// firmware update that adds factory presets, and you can open it in a text editor if you ever need to.
/// </summary>
public sealed class LibraryFile
{
    public const string LibraryExtension = ".p2lib";
    public const string PresetExtension = ".p2preset";
    private const string LibraryFormat = "pico2seq-library";
    private const string PresetFormat = "pico2seq-preset";

    private readonly PatchSchema _schema;
    private readonly FactoryCatalog _factory;

    public LibraryFile(PatchSchema schema, FactoryCatalog factory)
    {
        _schema = schema;
        _factory = factory;
    }

    // ---- writing ---------------------------------------------------------------------------

    public string WriteLibrary(PresetLibrary library)
    {
        using var stream = new MemoryStream();
        using (var w = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = true }))
        {
            w.WriteStartObject();
            w.WriteString("format", LibraryFormat);
            w.WriteNumber("version", 1);
            w.WriteNumber("layoutVersion", _schema.LayoutVersion);
            w.WriteString("name", library.Name);
            w.WriteStartArray("presets");
            foreach (var p in library.Presets) WritePreset(w, p);
            w.WriteEndArray();
            w.WriteEndObject();
        }
        return Encoding.UTF8.GetString(stream.ToArray()) + "\n";
    }

    public string WritePresetFile(UserPreset preset)
    {
        using var stream = new MemoryStream();
        using (var w = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = true }))
        {
            w.WriteStartObject();
            w.WriteString("format", PresetFormat);
            w.WriteNumber("version", 1);
            w.WriteNumber("layoutVersion", _schema.LayoutVersion);
            w.WritePropertyName("preset");
            WritePreset(w, preset);
            w.WriteEndObject();
        }
        return Encoding.UTF8.GetString(stream.ToArray()) + "\n";
    }

    private void WritePreset(Utf8JsonWriter w, UserPreset p)
    {
        w.WriteStartObject();
        w.WriteString("id", p.Id);
        w.WriteString("name", p.Name);
        w.WriteString("color", p.Color.ToHex());
        w.WriteNumber("page", p.Page + 1); // files use the number the box shows (page 2 = first user page)
        w.WriteNumber("pad", p.Pad);
        w.WriteString("base", _factory[p.BaseIndex].Name);
        w.WriteString("notes", p.Notes);
        w.WriteStartObject("values");
        foreach (var (field, value) in p.Values.Enumerate())
        {
            w.WritePropertyName(field.Key);
            // "R" keeps the exact float; integers (flags, choices, counts) print without a fraction.
            if (field.Type == FieldType.Float) w.WriteRawValue(value.ToString("R", CultureInfo.InvariantCulture));
            else w.WriteNumberValue((int)value);
        }
        w.WriteEndObject();
        w.WriteEndObject();
    }

    // ---- reading ---------------------------------------------------------------------------

    public PresetLibrary ReadLibrary(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        CheckHeader(root, LibraryFormat);
        var library = new PresetLibrary { Name = root.TryGetProperty("name", out var n) ? n.GetString() ?? "My Presets" : "My Presets" };
        foreach (var p in root.GetProperty("presets").EnumerateArray())
            library.Presets.Add(ReadPreset(p));
        return library;
    }

    public UserPreset ReadPresetFile(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        CheckHeader(root, PresetFormat);
        return ReadPreset(root.GetProperty("preset"));
    }

    private void CheckHeader(JsonElement root, string expectedFormat)
    {
        if (root.ValueKind != JsonValueKind.Object || !root.TryGetProperty("format", out var f) || f.GetString() != expectedFormat)
            throw new FormatException(expectedFormat == LibraryFormat
                ? "This is not a Preset Studio library file."
                : "This is not a Preset Studio preset file.");
        var layout = root.TryGetProperty("layoutVersion", out var l) ? l.GetInt32() : 0;
        if (layout > _schema.LayoutVersion)
            throw new FormatException(
                $"This file was made by a newer Preset Studio (layout {layout}); this one understands layout {_schema.LayoutVersion}. Update Preset Studio to open it.");
    }

    private UserPreset ReadPreset(JsonElement e)
    {
        var baseName = e.GetProperty("base").GetString() ?? "";
        var baseFactory = _factory.FindByName(baseName)
            ?? throw new FormatException($"The preset is built on '{baseName}', which this version of the box's firmware does not have.");

        // Start from the base preset so a file written before a value existed still loads completely.
        var values = baseFactory.Values.Clone();
        foreach (var v in e.GetProperty("values").EnumerateObject())
        {
            if (!_schema.TryGetField(v.Name, out var field))
                continue; // a value from a newer layout: ignored, never an error
            values[field] = v.Value.GetSingle();
        }
        Rgb color = Rgb.White;
        if (e.TryGetProperty("color", out var c) && c.GetString() is { } hex && Rgb.TryParseHex(hex, out var parsed))
            color = parsed;
        return new UserPreset
        {
            Id = e.TryGetProperty("id", out var id) && Guid.TryParse(id.GetString(), out var g) ? g : Guid.NewGuid(),
            Name = e.GetProperty("name").GetString() ?? "Preset",
            Color = color,
            Page = e.GetProperty("page").GetInt32() - 1,
            Pad = e.GetProperty("pad").GetInt32(),
            BaseIndex = baseFactory.Index,
            Notes = e.TryGetProperty("notes", out var notes) ? notes.GetString() ?? "" : "",
            Values = values,
        };
    }

    // ---- files -----------------------------------------------------------------------------

    /// <summary>Writes through a temporary file so a crash or full disk never leaves half a library.</summary>
    public void SaveLibrary(string path, PresetLibrary library) => AtomicWrite(path, WriteLibrary(library));

    public PresetLibrary LoadLibrary(string path) => ReadLibrary(File.ReadAllText(path, Encoding.UTF8));

    public void SavePreset(string path, UserPreset preset) => AtomicWrite(path, WritePresetFile(preset));

    public UserPreset LoadPreset(string path) => ReadPresetFile(File.ReadAllText(path, Encoding.UTF8));

    private static void AtomicWrite(string path, string text)
    {
        var temp = path + ".tmp";
        File.WriteAllText(temp, text, new UTF8Encoding(false));
        File.Move(temp, path, overwrite: true);
    }
}
