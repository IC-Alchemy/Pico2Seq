using PresetStudio.Editing;
using PresetStudio.Model;
using PresetStudio.Rules;

namespace PresetStudio.Shell;

/// <summary>One row of the preset list.</summary>
public sealed class PresetItemViewModel : ObservableObject
{
    private readonly StudioContext _studio;

    public PresetItemViewModel(UserPreset preset, StudioContext studio)
    {
        Preset = preset;
        _studio = studio;
    }

    public UserPreset Preset { get; }
    public string Name => Preset.Name;
    public Rgb Color => Preset.Color;
    /// <summary>"Page 2 / pad 5" - the numbers the box shows.</summary>
    public string Place => $"Page {Preset.Page + 1} · pad {Preset.Pad}";
    public string BaseName => _studio.Factory[Preset.BaseIndex].Name;
    public int ErrorCount => _studio.Validator.Validate(Preset).Count(i => i.Severity == IssueSeverity.Error);
    public bool HasErrors => ErrorCount > 0;

    public void Refresh()
    {
        Raise(nameof(Name));
        Raise(nameof(Color));
        Raise(nameof(Place));
        Raise(nameof(BaseName));
        Raise(nameof(ErrorCount));
        Raise(nameof(HasErrors));
    }
}

/// <summary>One pad of the 8 x 4 grid on the box, for the page being shown.</summary>
public sealed class PadViewModel : ObservableObject
{
    public PadViewModel(int index, int page, UserPreset? preset, bool isPageKey, bool isSelected, int columns)
    {
        Index = index;
        Page = page;
        Preset = preset;
        IsPageKey = isPageKey;
        IsSelected = isSelected;
        Row = index / columns;
        Column = index % columns;
    }

    public int Index { get; }
    public int Page { get; }
    public int Row { get; }
    public int Column { get; }
    public UserPreset? Preset { get; }
    public bool IsPageKey { get; }
    public bool IsSelected { get; }
    public bool IsOccupied => Preset is not null;
    public bool IsEmpty => !IsPageKey && Preset is null;

    /// <summary>What the pad shows: the preset's name, "page key" or nothing.</summary>
    public string Label => IsPageKey ? "page →" : Preset?.Name ?? "";

    /// <summary>The preset's LED colour; empty and page-key pads are a neutral grey.</summary>
    public Rgb Color => IsPageKey ? new Rgb(70, 70, 70) : Preset?.Color ?? new Rgb(34, 34, 38);

    /// <summary>Dark text on light pads and light text on dark ones.</summary>
    public bool UseDarkText
    {
        get
        {
            var c = Color;
            return (0.299 * c.R + 0.587 * c.G + 0.114 * c.B) > 150;
        }
    }

    public string ToolTip => IsPageKey
        ? "Pad 31 flips to the next page of the preset browser on the box. It cannot hold a preset."
        : Preset is null
            ? $"Pad {Index} (empty). Click to move the selected preset here."
            : $"{Preset.Name} – pad {Index}. Click to edit; drag onto another pad to move or swap.";
}

/// <summary>The words in the help panel: what the value under the pointer (or focus) is and does.</summary>
public sealed class HelpPanelViewModel : ObservableObject
{
    private string _title = "";
    private string _summary = "";
    private string _detail = "";
    private string _range = "";
    private string _current = "";
    private string _baseText = "";
    private string _hint = "";
    private string _unavailable = "";
    private IReadOnlyList<string> _tips = Array.Empty<string>();
    private bool _isChanged;

    public string Title { get => _title; private set => Set(ref _title, value); }
    public string Summary { get => _summary; private set => Set(ref _summary, value); }
    public string Detail { get => _detail; private set => Set(ref _detail, value); }
    /// <summary>"Range: 0 – 100 %" - empty for choices and switches.</summary>
    public string Range { get => _range; private set => Set(ref _range, value); }
    public string Current { get => _current; private set => Set(ref _current, value); }
    /// <summary>"Base preset (Bass): 37 %".</summary>
    public string BaseText { get => _baseText; private set => Set(ref _baseText, value); }
    public string Hint { get => _hint; private set => Set(ref _hint, value); }
    /// <summary>Why the value is greyed out right now, or empty.</summary>
    public string Unavailable { get => _unavailable; private set => Set(ref _unavailable, value); }
    public IReadOnlyList<string> Tips { get => _tips; private set => Set(ref _tips, value); }
    public bool IsChanged { get => _isChanged; private set => Set(ref _isChanged, value); }
    public bool HasTips => Tips.Count > 0;
    public bool HasValue => Current.Length > 0;

    public void ShowParameter(ParameterViewModel p, PresetEditor editor)
    {
        Title = p.Help.Title;
        Summary = p.Help.Summary;
        Detail = p.Help.Detail;
        Tips = p.Help.Tips;
        Range = p.IsNumeric
            ? $"Range: {Display.ValueFormatter.Format(p.Field, p.Unit, p.Min)} to {Display.ValueFormatter.Format(p.Field, p.Unit, p.Max)}"
            : "";
        Current = $"Now: {p.Text}";
        BaseText = $"Base preset ({editor.BaseFactory.Name}): {p.BaseText}";
        Hint = p.Hint;
        Unavailable = p.IsEnabled ? "" : p.DisabledReason;
        IsChanged = p.IsChanged;
        Raise(nameof(HasTips));
        Raise(nameof(HasValue));
    }

    public void ShowText(string title, string summary, string detail, IReadOnlyList<string>? tips = null)
    {
        Title = title;
        Summary = summary;
        Detail = detail;
        Tips = tips ?? Array.Empty<string>();
        Range = Current = BaseText = Hint = Unavailable = "";
        IsChanged = false;
        Raise(nameof(HasTips));
        Raise(nameof(HasValue));
    }
}

/// <summary>
/// A choice in the page selector above the grid. The list of these never changes (so the selector keeps its
/// selection); only the count of presets on the page does.
/// </summary>
public sealed class PageChoice : ObservableObject
{
    private int _count;

    public PageChoice(int page, string title)
    {
        Page = page;
        Title = title;
    }

    public int Page { get; }
    public string Title { get; }

    public int Count
    {
        get => _count;
        set
        {
            if (!Set(ref _count, value)) return;
            Raise(nameof(Caption));
        }
    }

    public string Caption => Count == 0 ? Title : $"{Title} ({Count})";
}
