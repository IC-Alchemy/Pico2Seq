using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Windows.Input;
using PresetStudio.Editing;
using PresetStudio.Link;
using PresetStudio.Model;
using PresetStudio.Rules;
using PresetStudio.Storage;

namespace PresetStudio.Shell;

/// <summary>
/// The whole editor as one bindable object: the library and its presets, the pad grid, the help panel, and the
/// connection to the box with everything you can do over it. The window only displays this and forwards clicks;
/// all the behaviour lives here, where the tests can reach it.
/// </summary>
public sealed class MainViewModel : ObservableObject, IDisposable
{
    public const string LibraryFilter = "Preset Studio library (*.p2lib)|*.p2lib|All files (*.*)|*.*";
    public const string PresetFilter = "Preset Studio preset (*.p2preset)|*.p2preset|All files (*.*)|*.*";
    public const string AutoDetect = "Auto-detect";

    private readonly StudioContext _studio;
    private readonly IDialogService _dialogs;
    private readonly SynchronizationContext? _ui;
    private readonly SettingsStore? _settings;
    private PresetItemViewModel? _selectedItem;
    private ParameterViewModel? _focused;
    private int _shownPage;
    private string _status = "Welcome. Add a preset from the factory bank to begin, or open a library.";
    private bool _busy;
    private double _progress;
    private string _progressText = "";
    private string? _selectedPort = AutoDetect;
    private bool _live;
    private bool _syncingSelection;
    private PresetEditor? _hookedEditor;

    public MainViewModel(StudioContext studio, DeviceSession device, IDialogService dialogs,
        SettingsStore? settings = null, SynchronizationContext? ui = null)
    {
        _studio = studio;
        Device = device;
        _dialogs = dialogs;
        _settings = settings;
        _ui = ui ?? SynchronizationContext.Current;
        Library = new LibraryEditor(studio);
        Help = new HelpPanelViewModel();
        Pages = Enumerable.Range(studio.Schema.Browser.FirstUserPage, studio.Schema.Browser.UserPages)
            .Select(p => new PageChoice(p, $"Page {p + 1}")).ToList();
        _shownPage = studio.Schema.Browser.FirstUserPage;
        PadNumbers = Enumerable.Range(0, studio.Schema.Browser.PadsPerPage).ToList();

        Library.LibraryChanged += OnLibraryChanged;
        Library.PresetEdited += OnPresetEdited;
        Library.PropertyChanged += OnLibraryPropertyChanged;
        Device.PropertyChanged += OnDevicePropertyChanged;
        Device.Faulted += e => OnUi(() => Status = "Live audition stopped: " + ExplainShort(e));

        var onError = new Action<Exception>(ReportError);
        NewCommand = new RelayCommand(NewLibrary);
        OpenCommand = new RelayCommand(() => OpenLibrary());
        SaveCommand = new RelayCommand(() => Save(), () => Library.IsDirty || Library.FilePath is null);
        SaveAsCommand = new RelayCommand(() => SaveAs());
        ImportCommand = new RelayCommand(ImportPreset);
        ExportCommand = new RelayCommand(ExportPreset, () => Library.Selected is not null);
        AddPresetCommand = new RelayCommand(p => AddFromFactory(p is int i ? i : 0));
        DuplicateCommand = new RelayCommand(DuplicateSelected, () => Library.Selected is not null);
        DeleteCommand = new RelayCommand(DeleteSelected, () => Library.Selected is not null);
        ResetAllCommand = new RelayCommand(ResetAll, () => Editor is { ChangedCount: > 0 });
        ResetParameterCommand = new RelayCommand(p => (p as ParameterViewModel)?.ResetToBase());
        RefreshPortsCommand = new RelayCommand(RefreshPorts);
        ConnectCommand = new AsyncRelayCommand(ConnectAsync, onError, () => !IsConnected);
        DisconnectCommand = new RelayCommand(Disconnect, () => IsConnected);
        PullCommand = new AsyncRelayCommand(PullAsync, onError);
        PushCommand = new AsyncRelayCommand(PushAsync, onError);
        AuditionCommand = new AsyncRelayCommand(AuditionNowAsync, onError, () => Library.Selected is not null);
        GrabCommand = new AsyncRelayCommand(GrabAsync, onError);
        QuickStartCommand = new RelayCommand(_dialogs.ShowQuickStart);

        RefreshPorts();
        RebuildItems();
        ShowWelcome();
    }

    /// <summary>Applies what was remembered from last time: port, audition voice, and reopens the last library.</summary>
    public void RestoreSession()
    {
        if (_settings is null) return;
        var s = _settings.Current;
        if (s.LastPort is not null && Ports.Contains(s.LastPort)) SelectedPort = s.LastPort;
        Device.AuditionVoice = Math.Clamp(s.AuditionVoice, 1, 4) - 1;
        Raise(nameof(AuditionVoice));
        if (s.LastLibrary is { } path && File.Exists(path))
        {
            try
            {
                Library.Open(path);
                Status = $"Opened {Path.GetFileName(path)}: {Library.Presets.Count} presets.";
            }
            catch (Exception e) when (e is FormatException or IOException or System.Text.Json.JsonException)
            {
                Status = $"Could not reopen {Path.GetFileName(path)}: starting with an empty library.";
            }
        }
        _live = s.LiveAudition;
        Raise(nameof(LiveAudition));
    }

    // ---- exposed parts ----------------------------------------------------------------------

    public StudioContext Studio => _studio;
    public LibraryEditor Library { get; }
    public DeviceSession Device { get; }
    public HelpPanelViewModel Help { get; }
    public ObservableCollection<PresetItemViewModel> Presets { get; } = new();
    public ObservableCollection<PadViewModel> Pads { get; } = new();
    public ObservableCollection<string> Ports { get; } = new();
    public IReadOnlyList<PageChoice> Pages { get; }
    public IReadOnlyList<int> PadNumbers { get; }
    public IReadOnlyList<FactoryPreset> FactoryPresets => _studio.Factory.Presets;
    public IReadOnlyList<int> Voices { get; } = new[] { 1, 2, 3, 4 };
    public PresetEditor? Editor => Library.Editor;

    public string WindowTitle => $"{Library.Title} – Pico2Seq Preset Studio";

    public string Status { get => _status; set => Set(ref _status, value); }
    public bool IsBusy { get => _busy; private set { if (Set(ref _busy, value)) Raise(nameof(IsNotBusy)); } }
    public bool IsNotBusy => !IsBusy;
    /// <summary>0..1 while a transfer runs.</summary>
    public double Progress { get => _progress; private set => Set(ref _progress, value); }
    public string ProgressText { get => _progressText; private set => Set(ref _progressText, value); }

    // ---- commands ---------------------------------------------------------------------------

    public ICommand NewCommand { get; }
    public ICommand OpenCommand { get; }
    public ICommand SaveCommand { get; }
    public ICommand SaveAsCommand { get; }
    public ICommand ImportCommand { get; }
    public ICommand ExportCommand { get; }
    public ICommand AddPresetCommand { get; }
    public ICommand DuplicateCommand { get; }
    public ICommand DeleteCommand { get; }
    public ICommand ResetAllCommand { get; }
    public ICommand ResetParameterCommand { get; }
    public ICommand RefreshPortsCommand { get; }
    public AsyncRelayCommand ConnectCommand { get; }
    public ICommand DisconnectCommand { get; }
    public AsyncRelayCommand PullCommand { get; }
    public AsyncRelayCommand PushCommand { get; }
    public AsyncRelayCommand AuditionCommand { get; }
    public AsyncRelayCommand GrabCommand { get; }
    public ICommand QuickStartCommand { get; }

    // ---- selection and the pad grid ---------------------------------------------------------

    public PresetItemViewModel? SelectedPreset
    {
        get => _selectedItem;
        set
        {
            if (_syncingSelection || ReferenceEquals(_selectedItem, value)) return;
            Library.Selected = value?.Preset;
        }
    }

    public int ShownPage
    {
        get => _shownPage;
        set
        {
            if (value == _shownPage) return;
            _shownPage = value;
            Raise();
            RebuildPads();
        }
    }

    /// <summary>The selected preset's page (browser page index), changed by moving it; an occupied target swaps.</summary>
    public int SelectedPresetPage
    {
        get => Library.Selected?.Page ?? _shownPage;
        set
        {
            if (Library.Selected is { } p && value != p.Page) MoveSelected(value, p.Pad);
        }
    }

    public int SelectedPresetPad
    {
        get => Library.Selected?.Pad ?? 0;
        set
        {
            if (Library.Selected is { } p && value != p.Pad) MoveSelected(p.Page, value);
        }
    }

    public int SelectedBaseIndex
    {
        get => Library.Selected?.BaseIndex ?? 0;
        set
        {
            if (Editor is null || value == Editor.Preset.BaseIndex) { Raise(); return; }
            Editor.ChangeBase(value, keepValues: true);
            Raise();
            Status = $"'{Editor.Name}' is now built on {_studio.Factory[value].Name}. Your values were kept; use \"Start over from base\" to take the base preset's.";
        }
    }

    private void MoveSelected(int page, int pad)
    {
        var preset = Library.Selected!;
        var other = Library.PresetAt(page, pad);
        Library.MoveTo(preset, page, pad);
        Editor?.NotifyPlaceChanged();
        RaisePlace();
        Status = other is null
            ? $"Moved '{preset.Name}' to page {page + 1}, pad {pad}."
            : $"Swapped '{preset.Name}' with '{other.Name}'.";
        if (page != _shownPage) ShownPage = page;
    }

    public void PadClicked(PadViewModel pad)
    {
        if (pad.IsPageKey) { Status = "Pad 31 is the page key on the box; it cannot hold a preset."; return; }
        if (pad.Preset is not null) { Library.Selected = pad.Preset; return; }
        if (Library.Selected is null) { Status = "Add a preset first, then click a pad to put it there."; return; }
        MoveSelected(pad.Page, pad.Index);
    }

    public void DropOnPad(Guid presetId, PadViewModel target)
    {
        var preset = Library.Presets.FirstOrDefault(p => p.Id == presetId);
        if (preset is null || target.IsPageKey) return;
        var other = target.Preset;
        Library.MoveTo(preset, target.Page, target.Index);
        if (ReferenceEquals(Library.Selected, preset)) { Editor?.NotifyPlaceChanged(); RaisePlace(); }
        Status = other is null || ReferenceEquals(other, preset)
            ? $"Moved '{preset.Name}' to page {target.Page + 1}, pad {target.Index}."
            : $"Swapped '{preset.Name}' with '{other.Name}'.";
    }

    private void RaisePlace()
    {
        Raise(nameof(SelectedPresetPage));
        Raise(nameof(SelectedPresetPad));
    }

    private void RebuildItems()
    {
        _syncingSelection = true;
        try
        {
            Presets.Clear();
            foreach (var p in Library.Presets.OrderBy(p => p.Page).ThenBy(p => p.Pad))
                Presets.Add(new PresetItemViewModel(p, _studio));
            _selectedItem = Presets.FirstOrDefault(i => ReferenceEquals(i.Preset, Library.Selected));
            Raise(nameof(SelectedPreset));
        }
        finally { _syncingSelection = false; }
        RebuildPads();
        UpdatePages();
        Raise(nameof(WindowTitle));
    }

    private void UpdatePages()
    {
        foreach (var p in Pages) p.Count = Library.Presets.Count(x => x.Page == p.Page);
    }

    private void RebuildPads()
    {
        Pads.Clear();
        var b = _studio.Schema.Browser;
        for (int i = 0; i < b.GridWidth * b.GridHeight; i++)
        {
            var isKey = i == b.PageKeyPad;
            var preset = isKey ? null : Library.PresetAt(_shownPage, i);
            Pads.Add(new PadViewModel(i, _shownPage, preset, isKey, preset is not null && ReferenceEquals(preset, Library.Selected), b.GridWidth));
        }
    }

    // ---- reacting to the library ------------------------------------------------------------

    private void OnLibraryChanged() => RebuildItems();

    private void OnLibraryPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        switch (e.PropertyName)
        {
            case nameof(LibraryEditor.Selected): OnSelectionChanged(); break;
            case nameof(LibraryEditor.Title): Raise(nameof(WindowTitle)); break;
            case nameof(LibraryEditor.IsDirty): CommandRefresh(); break;
        }
    }

    private void OnSelectionChanged()
    {
        if (_hookedEditor is not null) _hookedEditor.PropertyChanged -= OnEditorPropertyChanged;
        _hookedEditor = Library.Editor;
        if (_hookedEditor is not null) _hookedEditor.PropertyChanged += OnEditorPropertyChanged;

        _syncingSelection = true;
        try
        {
            _selectedItem = Presets.FirstOrDefault(i => ReferenceEquals(i.Preset, Library.Selected));
            Raise(nameof(SelectedPreset));
        }
        finally { _syncingSelection = false; }
        if (Library.Selected is { } p && p.Page != _shownPage && Pages.Any(x => x.Page == p.Page)) ShownPage = p.Page;
        else RebuildPads();
        Raise(nameof(Editor));
        Raise(nameof(SelectedBaseIndex));
        RaisePlace();
        FocusParameter(null);
        CommandRefresh();
        if (_live && Library.Selected is { } selected) Device.QueueAudition(selected);
    }

    private void OnEditorPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(PresetEditor.ChangedCount)) CommandRefresh();
    }

    private void OnPresetEdited(PresetEditor editor, EditKind kind)
    {
        var item = Presets.FirstOrDefault(i => ReferenceEquals(i.Preset, editor.Preset));
        item?.Refresh();
        if (kind is EditKind.Identity or EditKind.Base) RebuildPads();
        if (kind == EditKind.Base) Raise(nameof(SelectedBaseIndex));
        if (_focused is not null && Editor is not null) Help.ShowParameter(_focused, Editor);
        CommandRefresh();
        if (_live && kind is EditKind.Value or EditKind.Base) Device.QueueAudition(editor.Preset);
    }

    private void CommandRefresh()
    {
        foreach (var c in new object[] { SaveCommand, ExportCommand, DuplicateCommand, DeleteCommand, ResetAllCommand, AuditionCommand })
            (c as RelayCommand)?.RaiseCanExecuteChanged();
        AuditionCommand.RaiseCanExecuteChanged();
    }

    // ---- help panel -------------------------------------------------------------------------

    public void FocusParameter(ParameterViewModel? p)
    {
        _focused = p;
        if (p is not null && Editor is not null) Help.ShowParameter(p, Editor);
        else ShowWelcome();
    }

    public void FocusIdentity(string id)
    {
        _focused = null;
        if (_studio.Help.Identity.TryGetValue(id, out var h))
            Help.ShowText(h.Title, h.Summary, h.Detail, h.Tips);
    }

    private void ShowWelcome()
    {
        Help.ShowText("Pico2Seq Preset Studio",
            "Point at any value to see what it does.",
            "Each value of a preset has a plain-language explanation here: what it controls, what you will hear, its range, " +
            "and how far it has moved from the factory preset you started from (marked with a coloured bar). " +
            "Changes are sent to a voice on the Pico as you make them when Live audition is on.",
            new[]
            {
                "Add a preset from the factory bank, change what you like, give it a name and a colour.",
                "Click an empty pad on the grid to put the selected preset there. Drag presets to move or swap them.",
                "Send to Pico writes your presets to the box's preset pages (stop the transport and press pad 31 to flip pages).",
            });
    }

    // ---- file commands ----------------------------------------------------------------------

    /// <summary>True if it is fine to discard the current library (saved, or the player said so).</summary>
    public bool ConfirmCanClose()
    {
        if (!Library.IsDirty) return true;
        switch (_dialogs.AskSave("Unsaved changes", $"Save changes to \"{Library.Title.TrimEnd(' ', '*')}\" before closing?"))
        {
            case SavePromptResult.Save: return Save();
            case SavePromptResult.Discard: return true;
            default: return false;
        }
    }

    public void NewLibrary()
    {
        if (!ConfirmCanClose()) return;
        Library.New();
        Status = "Started a new, empty library.";
    }

    public void OpenLibrary(string? path = null)
    {
        if (!ConfirmCanClose()) return;
        path ??= _dialogs.ChooseFileToOpen("Open a preset library", LibraryFilter);
        if (path is null) return;
        try
        {
            Library.Open(path);
            _settings?.Update(s => s.LastLibrary = path);
            Status = $"Opened {Path.GetFileName(path)}: {Library.Presets.Count} presets.";
        }
        catch (Exception e) { ReportError(e, "Could not open the library"); }
    }

    public bool Save()
    {
        if (Library.FilePath is null) return SaveAs();
        try
        {
            Library.Save();
            Status = $"Saved {Path.GetFileName(Library.FilePath)}.";
            return true;
        }
        catch (Exception e) { ReportError(e, "Could not save the library"); return false; }
    }

    public bool SaveAs(string? path = null)
    {
        path ??= _dialogs.ChooseFileToSave("Save the preset library", LibraryFilter,
            Library.FilePath is null ? "My Presets" + LibraryFile.LibraryExtension : Path.GetFileName(Library.FilePath));
        if (path is null) return false;
        try
        {
            Library.SaveAs(path);
            _settings?.Update(s => s.LastLibrary = Library.FilePath);
            Status = $"Saved {Path.GetFileName(Library.FilePath)}.";
            return true;
        }
        catch (Exception e) { ReportError(e, "Could not save the library"); return false; }
    }

    public void ImportPreset()
    {
        var path = _dialogs.ChooseFileToOpen("Import a preset", PresetFilter);
        if (path is null) return;
        try
        {
            var imported = Library.ImportPreset(path);
            Status = imported is null
                ? "The library is full (62 presets); remove one first."
                : $"Imported '{imported.Name}' onto page {imported.Page + 1}, pad {imported.Pad}.";
        }
        catch (Exception e) { ReportError(e, "Could not import the preset"); }
    }

    public void ExportPreset()
    {
        if (Library.Selected is not { } preset) return;
        var path = _dialogs.ChooseFileToSave("Export the selected preset", PresetFilter, preset.Name + LibraryFile.PresetExtension);
        if (path is null) return;
        try
        {
            Library.ExportPreset(preset, path);
            Status = $"Exported '{preset.Name}' to {Path.GetFileName(path)}.";
        }
        catch (Exception e) { ReportError(e, "Could not export the preset"); }
    }

    // ---- editing commands -------------------------------------------------------------------

    public void AddFromFactory(int factoryIndex)
    {
        var added = Library.AddFromFactory(factoryIndex, _shownPage);
        Status = added is null
            ? "The box holds 62 presets and the library is full; remove one first."
            : $"Added '{added.Name}' (built on {_studio.Factory[factoryIndex].Name}) to page {added.Page + 1}, pad {added.Pad}.";
    }

    public void DuplicateSelected()
    {
        if (Library.Selected is not { } p) return;
        var copy = Library.Duplicate(p);
        Status = copy is null ? "The library is full; remove a preset first." : $"Copied to '{copy.Name}'.";
    }

    public void DeleteSelected()
    {
        if (Library.Selected is not { } p) return;
        if (!_dialogs.Confirm("Delete preset", $"Delete \"{p.Name}\" from the library?\nIt stays on the Pico until you send the library.", "Delete")) return;
        Library.Remove(p);
        Status = $"Deleted '{p.Name}'.";
    }

    public void ResetAll()
    {
        if (Editor is not { } e || e.ChangedCount == 0) return;
        if (!_dialogs.Confirm("Start over from base", $"Put all {e.ChangedCount} changed values of \"{e.Name}\" back to the {e.BaseFactory.Name} preset?\nName, colour and place are kept.", "Start over")) return;
        e.ResetAllToBase();
        Status = $"All values of '{e.Name}' are back to {e.BaseFactory.Name}.";
    }

    // ---- the connection ---------------------------------------------------------------------

    public bool IsConnected => Device.IsConnected;
    public string ConnectionText => Device.Status;

    public string? SelectedPort { get => _selectedPort; set => Set(ref _selectedPort, value); }

    public bool LiveAudition
    {
        get => _live;
        set
        {
            if (!Set(ref _live, value)) return;
            if (value && Library.Selected is { } p)
            {
                if (!IsConnected) Status = "Live audition is on. Connect to the Pico and the selected preset plays on the chosen voice as you edit it.";
                else Device.QueueAudition(p);
            }
            _settings?.Update(s => s.LiveAudition = value);
        }
    }

    /// <summary>Voice 1-4 the live audition plays on.</summary>
    public int AuditionVoice
    {
        get => Device.AuditionVoice + 1;
        set
        {
            Device.AuditionVoice = value - 1;
            Raise();
            _settings?.Update(s => s.AuditionVoice = value);
        }
    }

    private void OnDevicePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        OnUi(() =>
        {
            Raise(nameof(IsConnected));
            Raise(nameof(ConnectionText));
            Raise(nameof(AuditionVoice));
            (DisconnectCommand as RelayCommand)?.RaiseCanExecuteChanged();
            ConnectCommand.RaiseCanExecuteChanged();
            if (e.PropertyName == nameof(DeviceSession.Status) && !_busy) Status = Device.Status;
        });
    }

    public void RefreshPorts()
    {
        var current = SelectedPort;
        Ports.Clear();
        Ports.Add(AutoDetect);
        foreach (var port in Device.ListPorts()) Ports.Add(port);
        SelectedPort = current is not null && Ports.Contains(current) ? current : AutoDetect;
    }

    public async Task ConnectAsync()
    {
        await RunBusy("Connecting to the Pico...", async _ =>
        {
            if (SelectedPort is null or AutoDetect)
            {
                var port = await Device.AutoConnectAsync();
                if (port is null)
                {
                    _dialogs.ShowError("No Pico2Seq found",
                        "Preset Studio could not find a Pico2Seq on any serial port.\n\n" +
                        "• Is the USB cable plugged into the Pico's USB port (a data cable, not a charge-only one)?\n" +
                        "• Is the Pico running Pico2Seq firmware with the preset link (see Help → Quick start)?\n" +
                        "• Is another program (Arduino IDE serial monitor, a terminal) holding the port? Close it.");
                    return;
                }
                SelectedPort = port;
                _settings?.Update(s => s.LastPort = port);
            }
            else
            {
                await Device.ConnectAsync(SelectedPort);
                _settings?.Update(s => s.LastPort = SelectedPort);
            }
            Status = Device.Status;
        });
    }

    public void Disconnect()
    {
        Device.Disconnect();
        Status = "Disconnected.";
    }

    /// <summary>Connects (finding the port if need be) when not already connected. False if that did not work.</summary>
    private async Task<bool> EnsureConnectedAsync()
    {
        if (IsConnected) return true;
        await ConnectAsync();
        return IsConnected;
    }

    public async Task PullAsync()
    {
        if (!await EnsureConnectedAsync()) return;
        if (Library.Presets.Count > 0 || Library.IsDirty)
            if (!_dialogs.Confirm("Load from the Pico",
                    "This replaces the presets in this window with the ones stored on the Pico.\nYour current library stays as it is on disk if you have saved it.",
                    "Replace"))
                return;
        Device.WaitForAuditionIdle();
        await RunBusy("Reading presets from the Pico...", async progress =>
        {
            var (info, presets) = await Device.PullAsync(progress);
            Library.ReplaceWith(presets, "From the Pico");
            Status = presets.Count == 0
                ? "The Pico has no user presets yet. Add some and use Send to Pico."
                : $"Loaded {presets.Count} preset{(presets.Count == 1 ? "" : "s")} from the Pico.";
        });
    }

    public async Task PushAsync()
    {
        var issues = Library.Validate();
        var errors = issues.Where(i => i.Severity == IssueSeverity.Error).ToList();
        if (errors.Count > 0)
        {
            _dialogs.ShowError("Fix these first",
                "The Pico would refuse this library:\n\n" + string.Join("\n", errors.Take(12).Select(i => "• " + i)) +
                (errors.Count > 12 ? $"\n…and {errors.Count - 12} more." : ""));
            if (errors.FirstOrDefault(e => e.Preset is not null)?.Preset is { } first) Library.Selected = first;
            return;
        }
        if (!await EnsureConnectedAsync()) return;
        Device.WaitForAuditionIdle();

        SyncPreview preview;
        try
        {
            var (info, diff) = await Device.PreviewAsync(Library.Presets);
            var warnings = issues.Select(i => i.ToString()).ToList();
            var needed = BankSync.BankBytes(Library.Presets.Count);
            preview = new SyncPreview(diff, Library.Presets.Count, needed, info.FreeBytes, warnings, Device.PortName ?? "");
        }
        catch (Exception e) { ReportError(e); return; }

        if (preview.Diff.IsEmpty && preview.Diff.Unchanged == Library.Presets.Count && Library.Presets.Count > 0)
        {
            Status = "The Pico already has exactly these presets; nothing to send.";
            _dialogs.ShowInfo("Already up to date", "The Pico already has exactly these presets, so nothing needs to be sent.");
            return;
        }
        if (!_dialogs.ConfirmSync(preview)) return;

        await RunBusy("Sending presets to the Pico...", async progress =>
        {
            await Device.PushAsync(Library.Presets, progress);
            var n = Library.Presets.Count;
            Status = n == 0
                ? "The Pico's user presets were cleared."
                : $"Sent {n} preset{(n == 1 ? "" : "s")} to the Pico. On the box: stop the transport (or long-press Play) to open the preset browser, then press pad 31 to flip pages.";
        });
    }

    public async Task AuditionNowAsync()
    {
        if (Library.Selected is not { } p) return;
        if (!await EnsureConnectedAsync()) return;
        await Device.AuditionAsync(p);
        Status = $"Playing '{p.Name}' on voice {AuditionVoice}. Start the transport on the box to hear it in your pattern.";
    }

    /// <summary>Brings the sound a voice is playing on the box (including knob tweaks) in as a new preset.</summary>
    public async Task GrabAsync(object? voiceParameter)
    {
        var voice = voiceParameter switch
        {
            int i => i,
            string s when int.TryParse(s, out var parsed) => parsed,
            _ => AuditionVoice,
        };
        if (voice is < 1 or > 4) voice = AuditionVoice;
        if (!await EnsureConnectedAsync()) return;
        var grabbed = await Device.GrabVoiceAsync(voice - 1);
        var baseName = _studio.Factory[grabbed.BaseIndex].Name;
        grabbed.Name = Library.UniqueName(grabbed.Name.Length > 0 ? grabbed.Name : baseName);
        grabbed.Color = Library.Selected?.Color ?? new Rgb(0, 200, 255);
        if (Library.FindFreeSlot(_shownPage) is { } slot) { grabbed.Page = slot.Page; grabbed.Pad = slot.Pad; }
        var added = Library.AddExisting(grabbed);
        Status = added is null
            ? "The library is full; remove a preset first."
            : $"Grabbed voice {voice}'s sound as '{added.Name}' (built on {baseName}). Rename it and pick a colour.";
    }

    // ---- plumbing ---------------------------------------------------------------------------

    private async Task RunBusy(string text, Func<IProgress<LinkProgress>, Task> operation)
    {
        IsBusy = true;
        Progress = 0;
        ProgressText = text;
        Status = text;
        var progress = new Progress<LinkProgress>(p =>
        {
            Progress = p.Total == 0 ? 0 : (double)p.Done / p.Total;
            ProgressText = p.Total == 0 ? p.Step : $"{p.Step} ({p.Done} of {p.Total})";
        });
        try
        {
            await operation(progress);
        }
        finally
        {
            IsBusy = false;
            Progress = 0;
            ProgressText = "";
            Raise(nameof(IsConnected));
            Raise(nameof(ConnectionText));
        }
    }

    private void OnUi(Action action)
    {
        if (_ui is null || SynchronizationContext.Current == _ui) action();
        else _ui.Post(_ => action(), null);
    }

    public void ReportError(Exception e) => ReportError(e, null);

    private void ReportError(Exception e, string? context)
    {
        var (title, message) = e switch
        {
            DeviceRejectedException r => (context ?? "The Pico refused the presets", r.Message),
            IncompatibleDeviceException i => ("This Pico can't be used with this Preset Studio", i.Message),
            LinkTimeoutException t => ("The Pico is not answering", t.Message),
            LinkClosedException c => ("Connection problem", c.Message),
            LinkException l => (context ?? "Connection problem", l.Message),
            FormatException f => (context ?? "That file can't be used", f.Message),
            System.Text.Json.JsonException => (context ?? "That file can't be used", "The file is damaged or is not a Preset Studio file."),
            IOException io => (context ?? "File problem", io.Message),
            UnauthorizedAccessException u => (context ?? "File problem", u.Message),
            _ => (context ?? "Something went wrong", e.Message),
        };
        Status = message;
        _dialogs.ShowError(title, message);
    }

    private static string ExplainShort(Exception e) => e switch
    {
        DeviceRejectedException r => r.Message,
        LinkException l => l.Message,
        _ => e.Message,
    };

    public void Dispose()
    {
        Library.LibraryChanged -= OnLibraryChanged;
        Library.PresetEdited -= OnPresetEdited;
        Library.PropertyChanged -= OnLibraryPropertyChanged;
        Device.PropertyChanged -= OnDevicePropertyChanged;
        if (_hookedEditor is not null) _hookedEditor.PropertyChanged -= OnEditorPropertyChanged;
        Device.Dispose();
    }
}
