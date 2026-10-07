using System.Diagnostics;
using PresetStudio.Editing;
using PresetStudio.Link;
using PresetStudio.Model;
using PresetStudio.Shell;
using PresetStudio.Storage;
using Xunit;

namespace PresetStudio.Tests;

internal sealed class FakeDialogs : IDialogService
{
    public Queue<string?> OpenPaths = new();
    public Queue<string?> SavePaths = new();
    public bool ConfirmAnswer = true;
    public bool SyncAnswer = true;
    public SavePromptResult SaveAnswer = SavePromptResult.Discard;
    public List<string> Errors = new();
    public List<string> Infos = new();
    public List<string> Confirms = new();
    public SyncPreview? LastSync;
    public int QuickStarts;

    public string? ChooseFileToOpen(string title, string filter) => OpenPaths.Count > 0 ? OpenPaths.Dequeue() : null;
    public string? ChooseFileToSave(string title, string filter, string suggestedName) => SavePaths.Count > 0 ? SavePaths.Dequeue() : null;
    public bool Confirm(string title, string message, string acceptText = "OK", string cancelText = "Cancel") { Confirms.Add(title); return ConfirmAnswer; }
    public SavePromptResult AskSave(string title, string message) => SaveAnswer;
    public void ShowInfo(string title, string message) => Infos.Add(title + ": " + message);
    public void ShowError(string title, string message) => Errors.Add(title + ": " + message);
    public bool ConfirmSync(SyncPreview preview) { LastSync = preview; return SyncAnswer; }
    public void ShowQuickStart() => QuickStarts++;
}

public class ShellTests : IDisposable
{
    private readonly StudioContext _s = TestSupport.Studio;
    private readonly string _dir = Directory.CreateTempSubdirectory("p2shell").FullName;
    private readonly List<Process> _processes = new();
    private readonly FakeDialogs _dialogs = new();

    public void Dispose()
    {
        foreach (var p in _processes) { try { if (!p.HasExited) p.Kill(); } catch { } p.Dispose(); }
        Directory.Delete(_dir, recursive: true);
    }

    private MainViewModel NewVm(params string[] ports)
    {
        var device = new DeviceSession(_s, () => ports, Launch);
        return new MainViewModel(_s, device, _dialogs, new SettingsStore(Path.Combine(_dir, "settings.json")), ui: null);
    }

    private ILinkTransport Launch(string port)
    {
        var exe = TestSupport.SimulatorPath();
        Skip.If(exe is null, "preset_link_sim is not built");
        var psi = new ProcessStartInfo(exe!) { RedirectStandardInput = true, RedirectStandardOutput = true, UseShellExecute = false };
        psi.ArgumentList.Add("--bank");
        psi.ArgumentList.Add(Path.Combine(_dir, port + ".bank"));
        var p = Process.Start(psi)!;
        _processes.Add(p);
        return new StreamTransport(p.StandardOutput.BaseStream, p.StandardInput.BaseStream, port);
    }

    private int Factory(string name) => _s.Factory.FindByName(name)!.Index;

    // ---- no hardware needed -------------------------------------------------------------

    [Fact]
    public void Starting_empty_shows_the_welcome_and_an_empty_grid()
    {
        using var vm = NewVm();
        Assert.Empty(vm.Presets);
        Assert.Equal(32, vm.Pads.Count);
        Assert.True(vm.Pads[31].IsPageKey);
        Assert.All(vm.Pads.Take(31), p => Assert.True(p.IsEmpty));
        Assert.Equal("Pico2Seq Preset Studio", vm.Help.Title);
        Assert.Equal(new[] { "Page 2", "Page 3" }, vm.Pages.Select(p => p.Title));
        Assert.Null(vm.Editor);
        Assert.False(vm.IsConnected);
        Assert.Contains("Pico2Seq Preset Studio", vm.WindowTitle);
    }

    [Fact]
    public void Adding_presets_fills_the_list_grid_and_selection()
    {
        using var vm = NewVm();
        vm.AddFromFactory(Factory("Bass"));
        vm.AddFromFactory(Factory("Lead"));
        Assert.Equal(2, vm.Presets.Count);
        Assert.Equal("Lead", vm.SelectedPreset!.Name);
        Assert.Same(vm.Library.Selected, vm.SelectedPreset.Preset);
        Assert.Equal("Lead", vm.Editor!.Name);
        Assert.Equal("Bass", vm.Pads[0].Label);
        Assert.Equal("Lead", vm.Pads[1].Label);
        Assert.True(vm.Pads[1].IsSelected);
        Assert.False(vm.Pads[0].IsSelected);
        Assert.Equal(2, vm.Pages[0].Count);
        Assert.Equal("Page 2 (2)", vm.Pages[0].Caption);
        Assert.Contains("* ", vm.WindowTitle.Replace(" –", " ") + " ");
    }

    [Fact]
    public void Clicking_pads_selects_presets_and_moves_the_selected_one_to_empty_pads()
    {
        using var vm = NewVm();
        vm.AddFromFactory(Factory("Bass"));
        vm.AddFromFactory(Factory("Lead"));          // selected, on pad 1
        vm.PadClicked(vm.Pads[0]);                   // click the bass
        Assert.Equal("Bass", vm.Editor!.Name);
        vm.PadClicked(vm.Pads[12]);                  // click an empty pad: the bass moves there
        Assert.Equal(12, vm.Library.Selected!.Pad);
        Assert.Equal("Bass", vm.Pads[12].Label);
        Assert.True(vm.Pads[0].IsEmpty);
        Assert.Equal(12, vm.SelectedPresetPad);
        vm.PadClicked(vm.Pads[31]);                  // the page key is not a place
        Assert.Equal(12, vm.Library.Selected.Pad);
        Assert.Contains("page key", vm.Status);
    }

    [Fact]
    public void Dropping_on_an_occupied_pad_swaps_and_the_other_page_is_reachable()
    {
        using var vm = NewVm();
        vm.AddFromFactory(Factory("Bass"));
        vm.AddFromFactory(Factory("Lead"));
        var bass = vm.Library.Presets[0];
        var lead = vm.Library.Presets[1];
        vm.DropOnPad(bass.Id, vm.Pads[1]);
        Assert.Equal(1, bass.Pad);
        Assert.Equal(0, lead.Pad);
        Assert.Contains("Swapped", vm.Status);

        vm.ShownPage = 2;
        Assert.All(vm.Pads.Take(31), p => Assert.True(p.IsEmpty));
        vm.DropOnPad(bass.Id, vm.Pads[30]);
        Assert.Equal((2, 30), (bass.Page, bass.Pad));
        vm.DropOnPad(bass.Id, vm.Pads[31]);          // the page key refuses
        Assert.Equal((2, 30), (bass.Page, bass.Pad));
        Assert.Equal(1, vm.Pages[1].Count);
    }

    [Fact]
    public void Page_and_pad_pickers_move_the_selected_preset_and_follow_it()
    {
        using var vm = NewVm();
        vm.AddFromFactory(Factory("Bass"));
        vm.SelectedPresetPad = 7;
        Assert.Equal(7, vm.Library.Selected!.Pad);
        vm.SelectedPresetPage = 2;
        Assert.Equal(2, vm.ShownPage);               // the grid follows the preset to its new page
        Assert.Equal("Bass", vm.Pads[7].Label);
    }

    [Fact]
    public void Selecting_a_preset_on_another_page_switches_the_grid_to_it()
    {
        using var vm = NewVm();
        vm.AddFromFactory(Factory("Bass"));
        vm.AddFromFactory(Factory("Lead"));
        vm.SelectedPresetPage = 2;                   // Lead goes to page 3
        vm.SelectedPreset = vm.Presets.First(p => p.Name == "Bass");
        Assert.Equal(1, vm.ShownPage);
        vm.SelectedPreset = vm.Presets.First(p => p.Name == "Lead");
        Assert.Equal(2, vm.ShownPage);
    }

    [Fact]
    public void Editing_updates_the_list_row_pad_label_and_help_in_place()
    {
        using var vm = NewVm();
        vm.AddFromFactory(Factory("Bass"));
        var item = vm.SelectedPreset!;
        vm.Editor!.Name = "Warm Bass";
        vm.Editor.ColorHex = "#112233";
        Assert.Equal("Warm Bass", item.Name);
        Assert.Equal(new Rgb(0x11, 0x22, 0x33), item.Color);
        Assert.Equal("Warm Bass", vm.Pads[0].Label);
        Assert.Equal(new Rgb(0x11, 0x22, 0x33), vm.Pads[0].Color);
        Assert.Same(item, vm.SelectedPreset);        // the row was refreshed, not replaced

        var res = vm.Editor.Parameter("filter.resonance");
        vm.FocusParameter(res);
        Assert.Equal("Resonance", vm.Help.Title);
        Assert.False(vm.Help.IsChanged);
        res.Value = 0.77f;
        Assert.True(vm.Help.IsChanged);
        Assert.Contains("77 %", vm.Help.Current);
        Assert.Contains("Base preset (Bass)", vm.Help.BaseText);
        Assert.Contains("Range: 0 %", vm.Help.Range);
        vm.FocusParameter(vm.Editor.Parameter("string.t60"));
        Assert.Contains("Waveguide", vm.Help.Unavailable);
    }

    [Fact]
    public void Changing_the_base_keeps_the_edits_and_start_over_asks_first()
    {
        using var vm = NewVm();
        vm.AddFromFactory(Factory("Bass"));
        vm.Editor!.Parameter("filter.resonance").Value = 0.66f;
        vm.SelectedBaseIndex = Factory("Lead");
        Assert.Equal(Factory("Lead"), vm.Library.Selected!.BaseIndex);
        Assert.Equal(0.66f, vm.Library.Selected.Values["filter.resonance"]);
        Assert.True(vm.ResetAllCommand.CanExecute(null));

        _dialogs.ConfirmAnswer = false;
        vm.ResetAllCommand.Execute(null);
        Assert.Equal(0.66f, vm.Library.Selected.Values["filter.resonance"]);
        _dialogs.ConfirmAnswer = true;
        vm.ResetAllCommand.Execute(null);
        Assert.Equal(_s.Factory[Factory("Lead")].Values["filter.resonance"], vm.Library.Selected.Values["filter.resonance"]);
        Assert.False(vm.ResetAllCommand.CanExecute(null));
    }

    [Fact]
    public void Files_save_open_export_and_import_through_the_dialogs()
    {
        using var vm = NewVm();
        vm.AddFromFactory(Factory("Bass"));
        vm.Editor!.Name = "Saved One";
        var lib = Path.Combine(_dir, "a.p2lib");
        _dialogs.SavePaths.Enqueue(lib);
        Assert.True(vm.SaveAs());
        Assert.False(vm.Library.IsDirty);
        Assert.Contains("Saved a.p2lib", vm.Status);

        var exported = Path.Combine(_dir, "one.p2preset");
        _dialogs.SavePaths.Enqueue(exported);
        vm.ExportPreset();
        Assert.True(File.Exists(exported));

        using var other = NewVm();
        _dialogs.OpenPaths.Enqueue(lib);
        other.OpenLibrary();
        Assert.Equal("Saved One", other.Presets.Single().Name);
        _dialogs.OpenPaths.Enqueue(exported);
        other.ImportPreset();
        Assert.Equal(2, other.Presets.Count);
        Assert.Equal("Saved One 2", other.Presets.Last().Name);
    }

    [Fact]
    public void Closing_with_unsaved_changes_asks_and_honours_the_answer()
    {
        using var vm = NewVm();
        Assert.True(vm.ConfirmCanClose());           // nothing to lose
        vm.AddFromFactory(Factory("Bass"));
        _dialogs.SaveAnswer = SavePromptResult.Cancel;
        Assert.False(vm.ConfirmCanClose());
        _dialogs.SaveAnswer = SavePromptResult.Discard;
        Assert.True(vm.ConfirmCanClose());
        _dialogs.SaveAnswer = SavePromptResult.Save;
        _dialogs.SavePaths.Enqueue(Path.Combine(_dir, "close.p2lib"));
        Assert.True(vm.ConfirmCanClose());
        Assert.True(File.Exists(Path.Combine(_dir, "close.p2lib")));
        // Once the library has a file, "Save" writes to it without asking again.
        vm.AddFromFactory(Factory("Lead"));
        Assert.True(vm.ConfirmCanClose());
        // A new library has no file yet: if the player then cancels the file dialog, closing is cancelled too.
        using var fresh = NewVm();
        fresh.AddFromFactory(Factory("Lead"));
        _dialogs.SavePaths.Enqueue(null);
        Assert.False(fresh.ConfirmCanClose());
    }

    [Fact]
    public void Bad_files_are_reported_in_words_not_exceptions()
    {
        using var vm = NewVm();
        var junk = Path.Combine(_dir, "junk.p2lib");
        File.WriteAllText(junk, "this is not json");
        _dialogs.OpenPaths.Enqueue(junk);
        vm.OpenLibrary();
        Assert.Single(_dialogs.Errors);
        Assert.Contains("damaged", _dialogs.Errors[0]);
        File.WriteAllText(junk, "{\"format\":\"pico2seq-library\",\"layoutVersion\":9,\"presets\":[]}");
        _dialogs.OpenPaths.Enqueue(junk);
        vm.OpenLibrary();
        Assert.Contains("Update Preset Studio", _dialogs.Errors[1]);
    }

    [Fact]
    public async Task Sending_a_library_with_errors_is_blocked_before_touching_the_cable()
    {
        using var vm = NewVm("COM7");
        vm.AddFromFactory(Factory("Bass"));
        vm.AddFromFactory(Factory("Lead"));
        vm.Library.Presets[1].Pad = vm.Library.Presets[0].Pad;       // two presets, one pad
        await vm.PushAsync();
        Assert.Single(_dialogs.Errors);
        Assert.Contains("both on page 2, pad 0", _dialogs.Errors[0]);
        Assert.False(vm.IsConnected);                                  // it did not even try to connect
    }

    [Fact]
    public async Task Auto_detect_failure_explains_what_to_check()
    {
        using var vm = NewVm();     // no serial ports at all
        await vm.ConnectAsync();
        Assert.Contains("No Pico2Seq found", _dialogs.Errors.Single());
        Assert.Contains("USB cable", _dialogs.Errors[0]);
        Assert.False(vm.IsConnected);
    }

    [Fact]
    public void Restoring_the_last_session_brings_back_the_library_and_voice()
    {
        var settingsPath = Path.Combine(_dir, "settings.json");
        var lib = Path.Combine(_dir, "last.p2lib");
        using (var first = NewVm("COM3"))
        {
            first.AddFromFactory(Factory("Bass"));
            _dialogs.SavePaths.Enqueue(lib);
            first.SaveAs();
            first.AuditionVoice = 3;
            first.LiveAudition = true;
        }
        using var second = NewVm("COM3");
        second.RestoreSession();
        Assert.Single(second.Presets);
        Assert.Equal(3, second.AuditionVoice);
        Assert.True(second.LiveAudition);
        Assert.True(File.Exists(settingsPath));
    }

    [Fact]
    public void The_toolbar_commands_are_enabled_only_when_they_make_sense()
    {
        using var vm = NewVm("COM7");
        Assert.False(vm.DuplicateCommand.CanExecute(null));
        Assert.False(vm.DeleteCommand.CanExecute(null));
        Assert.False(vm.ExportCommand.CanExecute(null));
        Assert.False(vm.AuditionCommand.CanExecute(null));
        Assert.False(vm.DisconnectCommand.CanExecute(null));
        Assert.True(vm.ConnectCommand.CanExecute(null));
        vm.AddFromFactory(0);
        Assert.True(vm.DuplicateCommand.CanExecute(null));
        Assert.True(vm.DeleteCommand.CanExecute(null));
        Assert.True(vm.ExportCommand.CanExecute(null));
        Assert.True(vm.AuditionCommand.CanExecute(null));
        vm.DeleteSelected();
        Assert.Empty(vm.Presets);
        Assert.False(vm.DeleteCommand.CanExecute(null));
    }

    // ---- with the simulated box ---------------------------------------------------------

    [SkippableFact]
    public async Task The_whole_journey_new_edit_send_and_load_back()
    {
        using var vm = NewVm("COM7");
        vm.AddFromFactory(Factory("Bass"));
        vm.Editor!.Name = "Sub Wobble";
        vm.Editor.ColorHex = "#2060FF";
        vm.Editor.Parameter("filter.resonance").Value = 0.55f;
        vm.AddFromFactory(Factory("WgBell"));
        vm.SelectedPresetPage = 2;
        vm.SelectedPresetPad = 30;

        await vm.PushAsync();                       // connects by itself, previews, asks, sends
        Assert.Empty(_dialogs.Errors);
        Assert.NotNull(_dialogs.LastSync);
        Assert.Equal(2, _dialogs.LastSync!.Diff.Added);
        Assert.Equal(2, _dialogs.LastSync.PresetCount);
        Assert.True(_dialogs.LastSync.FitsInStorage);
        Assert.Equal(BankSync.BankBytes(2), _dialogs.LastSync.BytesNeeded);
        Assert.True(vm.IsConnected);
        Assert.Contains("Sent 2 presets", vm.Status);
        Assert.Contains("pad 31", vm.Status);
        Assert.Equal(2, vm.Device.Info!.PresetCount);

        // Sending again changes nothing, and says so instead of showing an empty list.
        await vm.PushAsync();
        Assert.Contains("already has exactly", _dialogs.Infos.Last());

        // A different window loads it back.
        using var other = NewVm("COM7");
        await other.PullAsync();
        Assert.Equal(2, other.Presets.Count);
        var back = other.Presets.First(p => p.Name == "Sub Wobble").Preset;
        Assert.Equal(new Rgb(0x20, 0x60, 0xFF), back.Color);
        Assert.Equal(0.55f, back.Values["filter.resonance"]);
        Assert.Equal((2, 30), (other.Presets.First(p => p.Name != "Sub Wobble").Preset.Page, other.Presets.First(p => p.Name != "Sub Wobble").Preset.Pad));
    }

    [SkippableFact]
    public async Task Declining_the_preview_sends_nothing()
    {
        using var vm = NewVm("COM7");
        vm.AddFromFactory(Factory("Bass"));
        _dialogs.SyncAnswer = false;
        await vm.PushAsync();
        Assert.NotNull(_dialogs.LastSync);
        Assert.Equal(0, vm.Device.Info!.PresetCount);
        Assert.DoesNotContain("Sent", vm.Status);
    }

    [SkippableFact]
    public async Task Loading_from_the_Pico_asks_before_replacing_work()
    {
        using var vm = NewVm("COM7");
        vm.AddFromFactory(Factory("Bass"));
        await vm.PushAsync();
        vm.AddFromFactory(Factory("Lead"));
        _dialogs.ConfirmAnswer = false;
        await vm.PullAsync();
        Assert.Equal(2, vm.Presets.Count);          // declined: the unsent Lead is still here
        _dialogs.ConfirmAnswer = true;
        await vm.PullAsync();
        Assert.Single(vm.Presets);
        Assert.Contains("Loaded 1 preset", vm.Status);
    }

    [SkippableFact]
    public async Task Grabbing_a_voice_brings_its_sound_in_as_a_new_preset()
    {
        using var vm = NewVm("COM7");
        vm.AddFromFactory(Factory("Lead"));
        vm.Editor!.Parameter("filter.resonance").Value = 0.42f;
        vm.AuditionVoice = 2;
        await vm.AuditionNowAsync();
        Assert.Contains("voice 2", vm.Status);

        await vm.GrabAsync(2);
        Assert.Equal(2, vm.Presets.Count);
        Assert.Equal(0.42f, vm.Library.Selected!.Values["filter.resonance"], 3);
        Assert.NotEqual(vm.Presets[0].Name, vm.Presets[1].Name);
        Assert.Contains("Grabbed voice 2", vm.Status);
    }

    [SkippableFact]
    public async Task Live_audition_follows_edits_and_selection()
    {
        using var vm = NewVm("COM7");
        vm.AddFromFactory(Factory("Bass"));
        vm.AddFromFactory(Factory("Lead"));
        await vm.ConnectAsync();
        vm.AuditionVoice = 1;
        vm.LiveAudition = true;
        for (int i = 0; i <= 10; i++) vm.Editor!.Parameter("filter.resonance").Value = i / 10f;
        Assert.True(vm.Device.WaitForAuditionIdle());
        var grabbed = await vm.Device.GrabVoiceAsync(0);
        Assert.Equal(1.0f, grabbed.Values["filter.resonance"], 3);

        vm.SelectedPreset = vm.Presets.First(p => p.Name == "Bass");   // browsing plays each preset
        Assert.True(vm.Device.WaitForAuditionIdle());
        var browsed = await vm.Device.GrabVoiceAsync(0);
        Assert.Equal(_s.Factory[Factory("Bass")].Values["filter.resonance"], browsed.Values["filter.resonance"], 3);
    }

    [SkippableFact]
    public async Task A_refusal_from_the_box_is_explained()
    {
        using var vm = NewVm("COM7");
        vm.AddFromFactory(Factory("Bass"));
        await vm.ConnectAsync();
        var preset = vm.Library.Selected!;
        // Bypass the editor's clamping, as a corrupted library file could.
        preset.Values["filter.resonance"] = 3f;
        await vm.AuditionCommand.ExecuteAsync(null);
        Assert.Contains("Resonance", _dialogs.Errors.Single());
        Assert.True(vm.IsConnected);
    }

    [SkippableFact]
    public async Task Cancelling_one_operation_does_not_wedge_the_buttons()
    {
        using var vm = NewVm("COM7");
        await vm.ConnectCommand.ExecuteAsync(null);
        Assert.True(vm.IsConnected);
        Assert.False(vm.ConnectCommand.CanExecute(null));
        vm.DisconnectCommand.Execute(null);
        Assert.False(vm.IsConnected);
        Assert.True(vm.ConnectCommand.CanExecute(null));
    }
}
