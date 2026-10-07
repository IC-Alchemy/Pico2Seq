using System.Diagnostics;
using PresetStudio.Editing;
using PresetStudio.Link;
using PresetStudio.Model;
using Xunit;

namespace PresetStudio.Tests;

/// <summary>The app-level connection logic against the firmware simulator (skipped if it is not built).</summary>
public class DeviceSessionTests : IDisposable
{
    private readonly StudioContext _s = TestSupport.Studio;
    private readonly string _dir = Directory.CreateTempSubdirectory("p2sess").FullName;
    private readonly List<Process> _processes = new();

    public void Dispose()
    {
        foreach (var p in _processes)
        {
            try { if (!p.HasExited) p.Kill(); } catch { }
            p.Dispose();
        }
        Directory.Delete(_dir, recursive: true);
    }

    private ILinkTransport LaunchSim(string name)
    {
        var exe = TestSupport.SimulatorPath();
        Skip.If(exe is null, "preset_link_sim is not built");
        if (name == "COM-silent") return new StreamTransport(new System.IO.Pipes.AnonymousPipeServerStream(System.IO.Pipes.PipeDirection.In), Stream.Null, name);
        if (name == "COM-missing") throw new LinkClosedException("COM-missing is in use by another program");
        var psi = new ProcessStartInfo(exe!) { RedirectStandardInput = true, RedirectStandardOutput = true, UseShellExecute = false };
        psi.ArgumentList.Add("--bank");
        psi.ArgumentList.Add(Path.Combine(_dir, name + ".bank"));
        var p = Process.Start(psi)!;
        _processes.Add(p);
        return new StreamTransport(p.StandardOutput.BaseStream, p.StandardInput.BaseStream, name);
    }

    private DeviceSession NewSession(params string[] ports) =>
        new(_s, () => ports, LaunchSim);

    private List<UserPreset> Presets(int n)
    {
        var lib = new LibraryEditor(_s);
        for (int i = 0; i < n; i++) lib.AddFromFactory(i % _s.Factory.Count);
        return lib.Presets.ToList();
    }

    [SkippableFact]
    public async Task Auto_detect_skips_dead_ports_and_finds_the_box()
    {
        using var session = NewSession("COM-missing", "COM-silent", "COM7");
        var port = await session.AutoConnectAsync();
        Assert.Equal("COM7", port);
        Assert.True(session.IsConnected);
        Assert.Equal("COM7", session.PortName);
        Assert.Contains("Connected on COM7", session.Status);
        Assert.Equal(0, session.Info!.PresetCount);
    }

    [SkippableFact]
    public async Task Auto_detect_says_so_when_nothing_answers()
    {
        using var session = NewSession("COM-missing");
        Assert.Null(await session.AutoConnectAsync());
        Assert.False(session.IsConnected);
        Assert.Contains("No Pico2Seq found", session.Status);
    }

    [SkippableFact]
    public async Task Push_preview_and_pull_agree()
    {
        using var session = NewSession("COM7");
        await session.ConnectAsync("COM7");
        var library = Presets(4);

        var (_, before) = await session.PreviewAsync(library);
        Assert.Equal(4, before.Added);
        Assert.Equal(0, before.Removed);

        var steps = new SyncProgress();
        await session.PushAsync(library, steps);
        Assert.NotEmpty(steps.Reports);
        Assert.Equal(4, session.Info!.PresetCount);

        library[1].Values["filter.resonance"] = 0.91f;
        library.RemoveAt(3);
        var (info, after) = await session.PreviewAsync(library);
        Assert.Equal(4, info.PresetCount);
        Assert.Equal(1, after.Changed);
        Assert.Equal(1, after.Removed);
        Assert.Equal(2, after.Unchanged);
        Assert.Contains("Resonance", after.Changes.First(c => c.Kind == BankChangeKind.Changed).Summary);

        var (_, pulled) = await session.PullAsync();
        Assert.Equal(4, pulled.Count);
    }

    [SkippableFact]
    public async Task Diff_explains_changes_in_plain_words()
    {
        using var session = NewSession("COM7");
        await session.ConnectAsync("COM7");
        var library = Presets(2);
        await session.PushAsync(library);
        library[0].Name = "Renamed";
        library[0].Color = new Rgb(1, 2, 3);
        library[0].Values["filter.resonance"] = 0.8f;
        library[0].Values["env.release"] = 0.25f;
        var (_, diff) = await session.PreviewAsync(library);
        var summary = Assert.Single(diff.Changes).Summary;
        Assert.Contains("renamed from", summary);
        Assert.Contains("new colour", summary);
        Assert.Contains("80 %", summary);
        Assert.Contains("250 ms", summary);
    }

    [SkippableFact]
    public async Task Live_audition_follows_the_slider_and_grab_returns_the_sound()
    {
        using var session = NewSession("COM7");
        await session.ConnectAsync("COM7");
        session.AuditionVoice = 2;
        var preset = Presets(1)[0];
        for (int i = 0; i <= 20; i++)
        {
            preset.Values["filter.resonance"] = i / 20f;
            session.QueueAudition(preset);
        }
        Assert.True(session.WaitForAuditionIdle());
        var grabbed = await session.GrabVoiceAsync(2);
        Assert.Equal(1.0f, grabbed.Values["filter.resonance"], 3);
        Assert.Equal(preset.BaseIndex, grabbed.BaseIndex);
    }

    [SkippableFact]
    public async Task A_refused_audition_is_reported_not_swallowed()
    {
        using var session = NewSession("COM7");
        await session.ConnectAsync("COM7");
        var preset = Presets(1)[0];
        preset.Values["out.level"] = 5f;     // outside what the box allows
        var ex = await Assert.ThrowsAsync<DeviceRejectedException>(() => session.AuditionAsync(preset));
        Assert.Equal("out.level", ex.Field!.Key);
        Assert.True(session.IsConnected);    // a refusal is not a lost cable
    }

    [SkippableFact]
    public async Task A_lost_cable_turns_the_session_back_to_not_connected()
    {
        using var session = NewSession("COM7");
        await session.ConnectAsync("COM7");
        _processes.Single().Kill();
        await Assert.ThrowsAnyAsync<LinkException>(() => session.PullAsync());
        Assert.False(session.IsConnected);
        Assert.Equal("Connection lost", session.Status);
        await Assert.ThrowsAsync<LinkClosedException>(() => session.PullAsync());
    }

    [SkippableFact]
    public async Task Reconnecting_replaces_the_old_connection()
    {
        using var session = NewSession("COM7", "COM8");
        await session.ConnectAsync("COM7");
        await session.PushAsync(Presets(2));
        await session.ConnectAsync("COM8");
        Assert.Equal("COM8", session.PortName);
        Assert.Equal(0, session.Info!.PresetCount);   // a different (empty) simulated box
        session.Disconnect();
        Assert.False(session.IsConnected);
        Assert.Equal("Not connected", session.Status);
    }
}
