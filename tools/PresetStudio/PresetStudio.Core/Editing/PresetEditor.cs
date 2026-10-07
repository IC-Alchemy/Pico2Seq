using PresetStudio.Help;
using PresetStudio.Model;
using PresetStudio.Rules;
using PresetStudio.Schema;

namespace PresetStudio.Editing;

public sealed class SectionViewModel : ObservableObject
{
    public SectionViewModel(string title, IReadOnlyList<ParameterViewModel> parameters)
    {
        Title = title;
        Parameters = parameters;
    }
    public string Title { get; }
    public IReadOnlyList<ParameterViewModel> Parameters { get; }
    /// <summary>False when nothing in the section applies to the preset (the section is shown greyed out).</summary>
    public bool HasEnabled => Parameters.Any(p => p.IsEnabled);
    internal void Refresh() => Raise(nameof(HasEnabled));
}

public sealed class TabViewModel : ObservableObject
{
    public TabViewModel(TabDef def, IReadOnlyList<SectionViewModel> sections)
    {
        Def = def;
        Sections = sections;
    }
    public TabDef Def { get; }
    public string Title => Def.Title;
    public string Summary => Def.Summary;
    public IReadOnlyList<SectionViewModel> Sections { get; }
    public bool HasEnabled => Sections.Any(s => s.HasEnabled);
    /// <summary>How many values on this tab differ from the base preset (for a badge on the tab).</summary>
    public int ChangedCount => Sections.Sum(s => s.Parameters.Count(p => p.IsChanged));
    public bool HasChanges => ChangedCount > 0;

    internal void Refresh()
    {
        foreach (var s in Sections) s.Refresh();
        Raise(nameof(HasEnabled));
        Raise(nameof(ChangedCount));
        Raise(nameof(HasChanges));
    }
}

/// <summary>What an edit touched, so the app can decide what to do (mark dirty, audition, redraw the grid).</summary>
public enum EditKind { Value, Identity, Base }

/// <summary>
/// The preset being edited: its identity (name, colour, place, base) and all its values as bindable
/// parameters. Changing one value re-evaluates the others - ranges, what applies, what has changed.
/// </summary>
public sealed class PresetEditor : ObservableObject
{
    private readonly StudioContext _studio;
    private readonly List<ParameterViewModel> _parameters = new();
    private PresetContext _context;

    public PresetEditor(UserPreset preset, StudioContext studio)
    {
        Preset = preset;
        _studio = studio;
        _context = new PresetContext(preset.Values, studio.Factory[preset.BaseIndex]);
        foreach (var field in studio.Schema.Fields)
            _parameters.Add(new ParameterViewModel(this, field, studio.Help.For(field.Key)));
        Tabs = studio.Help.Tabs.Select(tab => new TabViewModel(tab, tab.Sections.Select(section =>
            new SectionViewModel(section.Title, section.FieldKeys.Select(k => _parameters[studio.Schema[k].Index]).ToList())).ToList())).ToList();
    }

    public UserPreset Preset { get; }
    public PatchSchema Schema => _studio.Schema;
    public StudioContext Studio => _studio;
    public LimitResolver Limits => _studio.Limits;
    internal IShowContext Context => _context;
    public IReadOnlyList<ParameterViewModel> Parameters => _parameters;
    public IReadOnlyList<TabViewModel> Tabs { get; }

    /// <summary>Raised after every change, with what kind of change it was.</summary>
    public event Action<PresetEditor, EditKind>? Edited;

    public FactoryPreset BaseFactory => _studio.Factory[Preset.BaseIndex];

    public ParameterViewModel Parameter(string key) => _parameters[Schema[key].Index];

    /// <summary>Recipe controls take their names from the base preset only while the Recipe engine is in use.</summary>
    internal bool UsesRecipeLabels =>
        (int)Preset.Values["source.engine"] == Schema.Engines.Recipe && BaseFactory.Macros.Any(m => m is not null);

    public IEnumerable<ParameterViewModel> ChangedParameters => _parameters.Where(p => p.IsChanged);
    public int ChangedCount => _parameters.Count(p => p.IsChanged);
    public IReadOnlyList<Issue> Issues => _studio.Validator.Validate(Preset);

    // ---- identity ---------------------------------------------------------------------------

    public string Name
    {
        get => Preset.Name;
        set
        {
            var clean = _studio.Codec.SanitizeName(value);
            if (clean == Preset.Name) { Raise(); return; }
            Preset.Name = clean;
            Raise();
            Raise(nameof(Issues));
            Edited?.Invoke(this, EditKind.Identity);
        }
    }

    public Rgb Color
    {
        get => Preset.Color;
        set
        {
            if (value == Preset.Color) return;
            Preset.Color = value;
            Raise();
            Raise(nameof(ColorHex));
            Raise(nameof(Red));
            Raise(nameof(Green));
            Raise(nameof(Blue));
            Edited?.Invoke(this, EditKind.Identity);
        }
    }

    // Channel sliders for the colour picker.
    public double Red { get => Preset.Color.R; set => Color = Preset.Color with { R = (byte)Math.Clamp(Math.Round(value), 0, 255) }; }
    public double Green { get => Preset.Color.G; set => Color = Preset.Color with { G = (byte)Math.Clamp(Math.Round(value), 0, 255) }; }
    public double Blue { get => Preset.Color.B; set => Color = Preset.Color with { B = (byte)Math.Clamp(Math.Round(value), 0, 255) }; }

    public string ColorHex
    {
        get => Preset.Color.ToHex();
        set
        {
            if (Rgb.TryParseHex(value, out var c)) Color = c;
            else Raise();
        }
    }

    public string Notes
    {
        get => Preset.Notes;
        set
        {
            if (value == Preset.Notes) return;
            Preset.Notes = value ?? "";
            Raise();
            Edited?.Invoke(this, EditKind.Identity);
        }
    }

    /// <summary>Moves the preset to another place on the browser's pages. The caller resolves any clash.</summary>
    public void Place(int page, int pad)
    {
        if (page == Preset.Page && pad == Preset.Pad) return;
        Preset.Page = page;
        Preset.Pad = pad;
        Raise(nameof(Page));
        Raise(nameof(Pad));
        Raise(nameof(Issues));
        Edited?.Invoke(this, EditKind.Identity);
    }

    public int Page => Preset.Page;
    public int Pad => Preset.Pad;

    /// <summary>Call after the library moved or swapped this preset, so bindings to the page and pad refresh.</summary>
    public void NotifyPlaceChanged()
    {
        Raise(nameof(Page));
        Raise(nameof(Pad));
        Raise(nameof(Issues));
    }

    // ---- the base preset --------------------------------------------------------------------

    /// <summary>
    /// Builds the preset on another factory preset. With <paramref name="keepValues"/> the values stay as they are
    /// (only the lane layout and recipe change underneath); without it the preset starts over as that factory
    /// preset, losing every edit.
    /// </summary>
    public void ChangeBase(int factoryIndex, bool keepValues)
    {
        if (factoryIndex < 0 || factoryIndex >= _studio.Factory.Count) throw new ArgumentOutOfRangeException(nameof(factoryIndex));
        Preset.BaseIndex = factoryIndex;
        _context = new PresetContext(Preset.Values, _studio.Factory[factoryIndex]);
        if (!keepValues) Preset.Values.CopyFrom(_studio.Factory[factoryIndex].Values);
        else if ((int)Preset.Values["source.engine"] == Schema.Engines.Recipe && !_studio.Factory[factoryIndex].HasRecipe)
            Preset.Values["source.engine"] = Schema.Engines.Osc; // a recipe engine cannot survive a non-recipe base
        Reclamp();
        RefreshAll();
        Raise(nameof(BaseFactory));
        Edited?.Invoke(this, EditKind.Base);
    }

    /// <summary>Puts every value back to the base preset's (identity - name, colour, place - is untouched).</summary>
    public void ResetAllToBase()
    {
        Preset.Values.CopyFrom(BaseFactory.Values);
        RefreshAll();
        Edited?.Invoke(this, EditKind.Value);
    }

    // ---- values -----------------------------------------------------------------------------

    internal void SetValue(ParameterViewModel vm, float value)
    {
        Preset.Values[vm.Field] = value;
        if (vm.Key == "source.engine" && (int)value == Schema.Engines.Osc && (int)Preset.Values["source.oscCount"] == 0)
            Preset.Values["source.oscCount"] = 1; // the box does the same when the engine is switched to oscillators
        Reclamp();
        RefreshAll();
        Edited?.Invoke(this, EditKind.Value);
    }

    /// <summary>Changing the engine or switching a stage off can narrow other values' ranges; pull them in.</summary>
    private void Reclamp()
    {
        foreach (var vm in _parameters)
        {
            if (vm.Field.IsChoice || vm.Field.IsToggle) continue;
            var current = Preset.Values[vm.Field];
            var clamped = _studio.Limits.Clamp(vm.Field, Preset, current);
            if (clamped != current) Preset.Values[vm.Field] = clamped;
        }
    }

    private void RefreshAll()
    {
        foreach (var vm in _parameters) vm.Refresh();
        foreach (var tab in Tabs) tab.Refresh();
        Raise(nameof(ChangedCount));
        Raise(nameof(Issues));
    }

    internal IReadOnlyList<Choice> AvailableChoices(FieldDef field)
    {
        var all = field.Choices!;
        if (field.Key == "source.engine" && !BaseFactory.HasRecipe)
            return all.Where(c => c.Value != Schema.Engines.Recipe).ToList();
        return all;
    }

    /// <summary>Replaces the values wholesale (for "grab from voice" and similar), then refreshes everything.</summary>
    public void ReplaceValues(PatchValues values)
    {
        Preset.Values.CopyFrom(values);
        Reclamp();
        RefreshAll();
        Edited?.Invoke(this, EditKind.Value);
    }
}
