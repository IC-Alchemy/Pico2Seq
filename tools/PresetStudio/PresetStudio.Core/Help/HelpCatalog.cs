using System.Text.Json;
using PresetStudio.Schema;

namespace PresetStudio.Help;

/// <summary>Plain-language explanation of one value or setting.</summary>
public sealed record HelpEntry(string Title, string Summary, string Detail, IReadOnlyList<string> Tips);

public sealed record TabSection(string Title, IReadOnlyList<string> FieldKeys);

/// <summary>One tab of the parameter editor and the sections (with their values) on it.</summary>
public sealed record TabDef(string Id, string Title, string Summary, IReadOnlyList<TabSection> Sections);

/// <summary>
/// The words that go with the numbers: what each value does, what you will hear, and how the tabs are
/// arranged. Hand-written (<c>field-help.json</c>); the tests insist every value in the firmware's
/// schema has an entry and sits on exactly one tab.
/// </summary>
public sealed class HelpCatalog
{
    public IReadOnlyDictionary<string, HelpEntry> Identity { get; }
    public IReadOnlyDictionary<string, HelpEntry> Fields { get; }
    public IReadOnlyList<TabDef> Tabs { get; }

    private HelpCatalog(IReadOnlyDictionary<string, HelpEntry> identity, IReadOnlyDictionary<string, HelpEntry> fields, IReadOnlyList<TabDef> tabs)
    {
        Identity = identity;
        Fields = fields;
        Tabs = tabs;
    }

    public HelpEntry For(string fieldKey) =>
        Fields.TryGetValue(fieldKey, out var h) ? h : new HelpEntry(fieldKey, "", "", Array.Empty<string>());

    public static HelpCatalog Load() => Parse(EmbeddedResources.ReadText("field-help.json"));

    public static HelpCatalog Parse(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        static HelpEntry Entry(JsonElement e) => new(
            e.GetProperty("title").GetString()!, e.GetProperty("summary").GetString()!,
            e.GetProperty("detail").GetString()!,
            e.GetProperty("tips").EnumerateArray().Select(t => t.GetString()!).ToList());

        var identity = root.GetProperty("identity").EnumerateObject().ToDictionary(p => p.Name, p => Entry(p.Value));
        var fields = root.GetProperty("fields").EnumerateObject().ToDictionary(p => p.Name, p => Entry(p.Value));
        var tabs = root.GetProperty("tabs").EnumerateArray().Select(t => new TabDef(
            t.GetProperty("id").GetString()!, t.GetProperty("title").GetString()!, t.GetProperty("summary").GetString()!,
            t.GetProperty("sections").EnumerateArray().Select(s => new TabSection(
                s.GetProperty("title").GetString()!,
                s.GetProperty("fields").EnumerateArray().Select(f => f.GetString()!).ToList())).ToList())).ToList();
        return new HelpCatalog(identity, fields, tabs);
    }

    /// <summary>Problems that mean the help does not match the schema (empty when consistent).</summary>
    public IReadOnlyList<string> CheckAgainst(PatchSchema schema)
    {
        var problems = new List<string>();
        var placed = new Dictionary<string, int>();
        foreach (var key in Tabs.SelectMany(t => t.Sections).SelectMany(s => s.FieldKeys))
            placed[key] = placed.GetValueOrDefault(key) + 1;
        foreach (var f in schema.Fields)
        {
            if (!Fields.ContainsKey(f.Key)) problems.Add($"no help for '{f.Key}'");
            if (!placed.TryGetValue(f.Key, out var n)) problems.Add($"'{f.Key}' is on no tab");
            else if (n > 1) problems.Add($"'{f.Key}' is on {n} tabs");
        }
        foreach (var key in Fields.Keys.Where(k => !schema.TryGetField(k, out _)))
            problems.Add($"help for unknown field '{key}'");
        foreach (var key in placed.Keys.Where(k => !schema.TryGetField(k, out _)))
            problems.Add($"tab lists unknown field '{key}'");
        foreach (var id in new[] { "name", "color", "page", "pad", "base", "notes" })
            if (!Identity.ContainsKey(id)) problems.Add($"no help for '{id}'");
        return problems;
    }
}
