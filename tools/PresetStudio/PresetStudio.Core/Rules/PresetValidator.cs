using PresetStudio.Model;
using PresetStudio.Schema;

namespace PresetStudio.Rules;

public enum IssueSeverity { Warning, Error }

/// <summary>A problem the box would refuse (Error) or that is probably not what you meant (Warning).</summary>
public sealed record Issue(IssueSeverity Severity, string Message, UserPreset? Preset = null, string? FieldKey = null)
{
    public override string ToString() => Preset is null ? Message : $"{Preset.Name}: {Message}";
}

/// <summary>
/// Checks a preset, and a whole library, the way the box will on upload - so a problem shows up beside the
/// control that causes it, not as a rejected transfer. The box's own check stays authoritative.
/// </summary>
public sealed class PresetValidator
{
    private const float Slack = 1e-4f;
    private readonly PatchSchema _schema;
    private readonly FactoryCatalog _factory;
    private readonly LimitResolver _limits;

    public PresetValidator(PatchSchema schema, FactoryCatalog factory, LimitResolver limits)
    {
        _schema = schema;
        _factory = factory;
        _limits = limits;
    }

    public List<Issue> Validate(UserPreset p)
    {
        var issues = new List<Issue>();
        void Error(string message, string? key = null) => issues.Add(new Issue(IssueSeverity.Error, message, p, key));

        if (string.IsNullOrWhiteSpace(p.Name))
            Error("The name is empty.");
        else if (p.Name.Length > _schema.NameMaxLength)
            Error($"The name is longer than {_schema.NameMaxLength} characters.");
        else if (p.Name.Any(ch => ch < ' ' || ch > '~'))
            Error("The name may only use plain keyboard characters (letters, digits, punctuation).");

        var b = _schema.Browser;
        if (p.Page < b.FirstUserPage || p.Page >= b.FirstUserPage + b.UserPages)
            Error($"Page {p.Page + 1} does not exist; user presets live on pages {b.FirstUserPage + 1} to {b.FirstUserPage + b.UserPages}.");
        if (p.Pad < 0 || p.Pad >= b.PadsPerPage)
            Error($"Pad {p.Pad} is not available; presets use pads 0 to {b.PadsPerPage - 1} (pad {b.PageKeyPad} flips pages).");
        if (p.BaseIndex < 0 || p.BaseIndex >= _factory.Count)
        {
            Error("The base preset does not exist.");
            return issues;
        }

        foreach (var (field, value) in p.Values.Enumerate())
        {
            if (float.IsNaN(value) || float.IsInfinity(value))
            {
                Error($"{field.Label} is not a number.", field.Key);
                continue;
            }
            if (field.IsChoice)
            {
                if (field.Choices!.All(c => c.Value != (int)value))
                    Error($"{field.Label} has a value ({value:0}) the box does not know.", field.Key);
                continue;
            }
            if (field.Type == FieldType.Flag) continue;
            var r = _limits.Resolve(field, p);
            if (value < r.Min - Slack || value > r.Max + Slack)
                Error($"{r.Label} is {value:0.###}, outside {r.Min:0.###} to {r.Max:0.###}.", field.Key);
        }

        if ((int)p.Values["source.engine"] == _schema.Engines.Recipe && !_factory[p.BaseIndex].HasRecipe)
            Error($"The Recipe engine needs a base preset built on a recipe; '{_factory[p.BaseIndex].Name}' has none.", "source.engine");
        return issues;
    }

    /// <summary>Checks every preset plus the things only a whole bank can get wrong.</summary>
    public List<Issue> ValidateLibrary(IReadOnlyList<UserPreset> presets)
    {
        var issues = new List<Issue>();
        foreach (var p in presets)
            issues.AddRange(Validate(p));
        var b = _schema.Browser;
        if (presets.Count > b.MaxPresets)
            issues.Add(new Issue(IssueSeverity.Error, $"The box holds {b.MaxPresets} presets; this library has {presets.Count}."));
        foreach (var group in presets.GroupBy(p => (p.Page, p.Pad)).Where(g => g.Count() > 1))
            issues.Add(new Issue(IssueSeverity.Error,
                $"{string.Join(" and ", group.Select(p => $"'{p.Name}'"))} are both on page {group.Key.Page + 1}, pad {group.Key.Pad}."));
        foreach (var group in presets.GroupBy(p => p.Name, StringComparer.OrdinalIgnoreCase).Where(g => g.Count() > 1))
            issues.Add(new Issue(IssueSeverity.Warning,
                $"{group.Count()} presets are called '{group.Key}'; the box's display will show the same name for each."));
        return issues;
    }
}
