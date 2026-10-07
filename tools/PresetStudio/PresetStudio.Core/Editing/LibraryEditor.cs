using PresetStudio.Model;
using PresetStudio.Rules;
using PresetStudio.Storage;

namespace PresetStudio.Editing;

/// <summary>
/// The working library: the presets laid out on the box's pages, which is also exactly what gets sent to
/// the box. Adding, moving, copying and removing presets keeps every preset on its own pad; saving writes
/// the library file on your computer.
/// </summary>
public sealed class LibraryEditor : ObservableObject
{
    private readonly StudioContext _studio;
    private UserPreset? _selected;
    private PresetEditor? _editor;
    private bool _dirty;
    private string? _filePath;

    public LibraryEditor(StudioContext studio)
    {
        _studio = studio;
        Library = new PresetLibrary();
    }

    public StudioContext Studio => _studio;
    public PresetLibrary Library { get; private set; }
    public IReadOnlyList<UserPreset> Presets => Library.Presets;

    /// <summary>Raised when presets are added, removed or swapped, or the whole library is replaced.</summary>
    public event Action? LibraryChanged;
    /// <summary>Raised when a value of the selected preset changed (for live audition).</summary>
    public event Action<PresetEditor, EditKind>? PresetEdited;

    public string? FilePath
    {
        get => _filePath;
        private set { if (Set(ref _filePath, value)) { Raise(nameof(Title)); } }
    }

    public bool IsDirty
    {
        get => _dirty;
        private set { if (Set(ref _dirty, value)) Raise(nameof(Title)); }
    }

    public string Title => (FilePath is null ? Library.Name : Path.GetFileNameWithoutExtension(FilePath)) + (IsDirty ? " *" : "");

    public UserPreset? Selected
    {
        get => _selected;
        set
        {
            if (ReferenceEquals(_selected, value)) return;
            _selected = value;
            if (_editor is not null) _editor.Edited -= OnEdited;
            _editor = value is null ? null : new PresetEditor(value, _studio);
            if (_editor is not null) _editor.Edited += OnEdited;
            Raise();
            Raise(nameof(Editor));
        }
    }

    /// <summary>Editor for the selected preset, or null.</summary>
    public PresetEditor? Editor => _editor;

    private void OnEdited(PresetEditor editor, EditKind kind)
    {
        // Renames, colours and moves do not change which presets exist, so they are reported as an edit
        // (the list and grid update that preset in place) rather than as a new library.
        IsDirty = true;
        PresetEdited?.Invoke(editor, kind);
    }

    // ---- placing presets --------------------------------------------------------------------

    public UserPreset? PresetAt(int page, int pad) => Library.Presets.FirstOrDefault(p => p.Page == page && p.Pad == pad);

    public bool IsFull => Library.Presets.Count >= _studio.Schema.Browser.MaxPresets;

    /// <summary>The first free pad, starting from the given page and wrapping through the user pages.</summary>
    public (int Page, int Pad)? FindFreeSlot(int startPage = -1)
    {
        var b = _studio.Schema.Browser;
        var first = startPage >= b.FirstUserPage && startPage < b.FirstUserPage + b.UserPages ? startPage : b.FirstUserPage;
        for (int i = 0; i < b.UserPages; i++)
        {
            var page = b.FirstUserPage + (first - b.FirstUserPage + i) % b.UserPages;
            for (int pad = 0; pad < b.PadsPerPage; pad++)
                if (PresetAt(page, pad) is null) return (page, pad);
        }
        return null;
    }

    /// <summary>A name not yet used: "Bass", "Bass 2", "Bass 3", ... within the box's name length.</summary>
    public string UniqueName(string wanted)
    {
        var max = _studio.Schema.NameMaxLength;
        var baseName = _studio.Codec.SanitizeName(wanted);
        if (baseName.Length == 0) baseName = "Preset";
        bool Taken(string n) => Library.Presets.Any(p => string.Equals(p.Name, n, StringComparison.OrdinalIgnoreCase));
        if (!Taken(baseName)) return baseName;
        for (int n = 2; ; n++)
        {
            var suffix = " " + n;
            var stem = baseName.Length + suffix.Length > max ? baseName[..(max - suffix.Length)] : baseName;
            var candidate = stem + suffix;
            if (!Taken(candidate)) return candidate;
        }
    }

    // ---- editing the library ----------------------------------------------------------------

    /// <summary>A new preset that sounds like a factory preset, on the first free pad. Returns null if the box is full.</summary>
    public UserPreset? AddFromFactory(int factoryIndex, int preferredPage = -1)
    {
        if (IsFull || FindFreeSlot(preferredPage) is not { } slot) return null;
        var preset = _studio.Factory.NewFrom(factoryIndex, _studio.Schema);
        preset.Name = UniqueName(preset.Name);
        preset.Page = slot.Page;
        preset.Pad = slot.Pad;
        preset.Color = SuggestColor(factoryIndex);
        Add(preset);
        return preset;
    }

    /// <summary>Adds an existing preset (import, paste) to the library, on its own pad if free, else the next free one.</summary>
    public UserPreset? AddExisting(UserPreset preset)
    {
        if (IsFull) return null;
        if (preset.Values.Schema.RecordSize != _studio.Schema.RecordSize) throw new ArgumentException("preset belongs to a different schema");
        if (PresetAt(preset.Page, preset.Pad) is not null)
        {
            if (FindFreeSlot(preset.Page) is not { } slot) return null;
            preset.Page = slot.Page;
            preset.Pad = slot.Pad;
        }
        preset.Name = UniqueName(preset.Name);
        Add(preset);
        return preset;
    }

    private void Add(UserPreset preset)
    {
        Library.Presets.Add(preset);
        IsDirty = true;
        LibraryChanged?.Invoke();
        Selected = preset;
    }

    public UserPreset? Duplicate(UserPreset source)
    {
        if (IsFull || FindFreeSlot(source.Page) is not { } slot) return null;
        var copy = source.Duplicate();
        copy.Name = UniqueName(source.Name);
        copy.Page = slot.Page;
        copy.Pad = slot.Pad;
        Add(copy);
        return copy;
    }

    public void Remove(UserPreset preset)
    {
        var index = Library.Presets.IndexOf(preset);
        if (index < 0) return;
        Library.Presets.RemoveAt(index);
        IsDirty = true;
        if (ReferenceEquals(Selected, preset))
            Selected = Library.Presets.Count == 0 ? null : Library.Presets[Math.Min(index, Library.Presets.Count - 1)];
        LibraryChanged?.Invoke();
    }

    /// <summary>
    /// Moves a preset to a pad. If another preset is there the two swap places, so dragging onto an occupied
    /// pad never loses anything.
    /// </summary>
    public void MoveTo(UserPreset preset, int page, int pad)
    {
        var b = _studio.Schema.Browser;
        if (page < b.FirstUserPage || page >= b.FirstUserPage + b.UserPages || pad < 0 || pad >= b.PadsPerPage)
            throw new ArgumentOutOfRangeException(nameof(pad), "that pad does not hold presets");
        if (preset.Page == page && preset.Pad == pad) return;
        var other = PresetAt(page, pad);
        if (other is not null)
        {
            other.Page = preset.Page;
            other.Pad = preset.Pad;
        }
        preset.Page = page;
        preset.Pad = pad;
        IsDirty = true;
        Raise(nameof(Editor));
        LibraryChanged?.Invoke();
    }

    /// <summary>A starting colour that differs between kinds of sound: hue follows the base preset.</summary>
    private Rgb SuggestColor(int factoryIndex)
    {
        var hue = (factoryIndex * 47 % 360) / 360.0;
        return FromHsv(hue, 0.85, 1.0);
    }

    private static Rgb FromHsv(double h, double s, double v)
    {
        var i = (int)Math.Floor(h * 6);
        var f = h * 6 - i;
        var p = v * (1 - s);
        var q = v * (1 - f * s);
        var t = v * (1 - (1 - f) * s);
        var (r, g, b) = (i % 6) switch
        {
            0 => (v, t, p),
            1 => (q, v, p),
            2 => (p, v, t),
            3 => (p, q, v),
            4 => (t, p, v),
            _ => (v, p, q),
        };
        return new Rgb((byte)Math.Round(r * 255), (byte)Math.Round(g * 255), (byte)Math.Round(b * 255));
    }

    // ---- files ------------------------------------------------------------------------------

    public void New()
    {
        Selected = null;
        Library = new PresetLibrary();
        FilePath = null;
        IsDirty = false;
        Raise(nameof(Library));
        Raise(nameof(Title));
        LibraryChanged?.Invoke();
    }

    public void Open(string path)
    {
        var library = _studio.Files.LoadLibrary(path);
        Selected = null;
        Library = library;
        FilePath = path;
        IsDirty = false;
        Raise(nameof(Library));
        Raise(nameof(Title));
        LibraryChanged?.Invoke();
        Selected = library.Presets.FirstOrDefault();
    }

    public void Save()
    {
        if (FilePath is null) throw new InvalidOperationException("this library has no file yet; use SaveAs");
        _studio.Files.SaveLibrary(FilePath, Library);
        IsDirty = false;
    }

    public void SaveAs(string path)
    {
        if (string.IsNullOrWhiteSpace(Path.GetExtension(path))) path += LibraryFile.LibraryExtension;
        Library.Name = Path.GetFileNameWithoutExtension(path);
        _studio.Files.SaveLibrary(path, Library);
        FilePath = path;
        IsDirty = false;
    }

    public UserPreset? ImportPreset(string path) => AddExisting(_studio.Files.LoadPreset(path));

    public void ExportPreset(UserPreset preset, string path) => _studio.Files.SavePreset(path, preset);

    /// <summary>Replaces the library with presets read from the box (the file path is cleared: it is a new document).</summary>
    public void ReplaceWith(IEnumerable<UserPreset> presets, string name)
    {
        Selected = null;
        Library = new PresetLibrary { Name = name };
        Library.Presets.AddRange(presets);
        FilePath = null;
        IsDirty = true;
        Raise(nameof(Library));
        Raise(nameof(Title));
        LibraryChanged?.Invoke();
        Selected = Library.Presets.FirstOrDefault();
    }

    public IReadOnlyList<Issue> Validate() => _studio.Validator.ValidateLibrary(Library.Presets);
}
