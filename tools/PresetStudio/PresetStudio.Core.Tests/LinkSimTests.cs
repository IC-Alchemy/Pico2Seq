using System.Diagnostics;
using PresetStudio.Editing;
using PresetStudio.Link;
using PresetStudio.Model;
using Xunit;

namespace PresetStudio.Tests;

/// <summary>Collects progress reports synchronously (Progress&lt;T&gt; posts to another thread).</summary>
internal sealed class SyncProgress : IProgress<LinkProgress>
{
    public List<LinkProgress> Reports { get; } = new();
    public void Report(LinkProgress value) => Reports.Add(value);
}

/// <summary>
/// The real client against the firmware's own link code. <c>preset_link_sim</c> (built by the host C++ test
/// build) runs the same frame parser, command session, bank store and validator as the Pico, with a file for
/// flash; these tests skip, with a note, if it has not been built.
/// </summary>
public class LinkSimTests : IDisposable
{
    private readonly StudioContext _s = TestSupport.Studio;
    private readonly string _dir = Directory.CreateTempSubdirectory("p2sim").FullName;
    private readonly List<IDisposable> _open = new();

    public void Dispose()
    {
        foreach (var d in _open) d.Dispose();
        Directory.Delete(_dir, recursive: true);
    }

    private sealed class Sim : IDisposable
    {
        private readonly Process _process;
        public DeviceLink Link { get; }

        public Sim(string exe, string bankPath, bool chatty, int capacity)
        {
            var psi = new ProcessStartInfo(exe)
            {
                RedirectStandardInput = true,
                RedirectStandardOutput = true,
                UseShellExecute = false,
            };
            psi.ArgumentList.Add("--bank");
            psi.ArgumentList.Add(bankPath);
            psi.ArgumentList.Add("--capacity");
            psi.ArgumentList.Add(capacity.ToString());
            if (chatty) psi.ArgumentList.Add("--chatty");
            _process = Process.Start(psi)!;
            var transport = new StreamTransport(_process.StandardOutput.BaseStream, _process.StandardInput.BaseStream, "simulator");
            Link = new DeviceLink(transport, TestSupport.Studio.Schema, replyTimeoutMs: 3000);
        }

        private bool _disposed;

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            Link.Dispose();
            if (!_process.WaitForExit(2000)) _process.Kill();
            _process.Dispose();
        }
    }

    private Sim Start(bool chatty = false, int capacity = 20000, string? bank = null)
    {
        var exe = TestSupport.SimulatorPath();
        Skip.If(exe is null, "preset_link_sim is not built: cmake --build build_test_ninja --target preset_link_sim");
        var sim = new Sim(exe!, bank ?? Path.Combine(_dir, "bank.bin"), chatty, capacity);
        _open.Add(sim);
        return sim;
    }

    private List<UserPreset> SamplePresets(int count)
    {
        var lib = new LibraryEditor(_s);
        for (int i = 0; i < count; i++)
        {
            var p = lib.AddFromFactory(i % _s.Factory.Count)!;
            p.Name = $"Sound {i + 1}";
        }
        return lib.Presets.ToList();
    }

    [SkippableFact]
    public void Hello_reports_a_compatible_firmware()
    {
        var sim = Start();
        var info = sim.Link.Hello();
        Assert.Equal(_s.Factory.Count, info.FactoryCount);
        Assert.Equal(0, info.PresetCount);
        Assert.Equal(62, info.MaxPresets);
        Assert.True(info.TransportRunning);
        Assert.Null(info.Incompatibility(_s.Schema));
    }

    [SkippableFact]
    public void A_bank_pushes_reads_back_identically_and_survives_a_restart()
    {
        var presets = SamplePresets(5);
        presets[2].Values["filter.resonance"] = 0.321f;
        presets[3].Page = 2;
        presets[3].Pad = 30;
        var sync = new BankSync(_s);
        var bank = Path.Combine(_dir, "persist.bin");

        using (var first = Start(bank: bank))
        {
            sync.Push(first.Link, presets);
            var (info, pulled) = sync.Pull(first.Link);
            Assert.Equal(5, info.PresetCount);
            Assert.Equal(5, pulled.Count);
            Assert.True(sync.Compare(presets, pulled).IsEmpty);
        }

        // "Power cycle": a fresh simulator over the same flash file.
        using var second = Start(bank: bank);
        var (info2, again) = sync.Pull(second.Link);
        Assert.Equal(5, info2.PresetCount);
        var diff = sync.Compare(presets, again);
        Assert.True(diff.IsEmpty, string.Join("; ", diff.Changes.Select(c => c.Summary)));
        Assert.Equal(5, diff.Unchanged);
        Assert.Equal(0.321f, again.First(p => p.Name == "Sound 3").Values["filter.resonance"]);
    }

    [SkippableFact]
    public void Log_text_between_frames_does_not_disturb_the_link()
    {
        var sim = Start(chatty: true);
        var lines = new List<string>();
        sim.Link.ConsoleText += lines.Add;
        var sync = new BankSync(_s);
        sync.Push(sim.Link, SamplePresets(3));
        Assert.Equal(3, sync.Pull(sim.Link).Presets.Count);
        Assert.Contains(lines, l => l.StartsWith("[DIAG C0]"));
    }

    [SkippableFact]
    public void The_box_refuses_an_invalid_preset_and_names_the_value()
    {
        var sim = Start();
        var presets = SamplePresets(3);
        presets[1].Values["filter.resonance"] = 2.5f; // the editor would clamp this; send it anyway to hear the box say no
        var records = presets.Select(p => _s.Codec.Encode(p)).ToList();
        var ex = Assert.Throws<DeviceRejectedException>(() => sim.Link.UploadBank(records, presets.Select(p => p.Name).ToList()));
        Assert.Equal(LinkProtocol.ErrorCode.InvalidRecord, ex.Code);
        Assert.Equal("filter.resonance", ex.Field!.Key);
        Assert.Contains("Sound 2", ex.Message);
        // The failed transfer left nothing behind.
        Assert.Equal(0, sim.Link.Hello().PresetCount);
    }

    [SkippableFact]
    public void A_failed_transfer_keeps_the_old_bank()
    {
        var sim = Start();
        var sync = new BankSync(_s);
        var good = SamplePresets(2);
        sync.Push(sim.Link, good);

        var bad = SamplePresets(3);
        bad[2].Pad = bad[0].Pad; // two presets on one pad
        var records = bad.Select(p => _s.Codec.Encode(p)).ToList();
        var ex = Assert.Throws<DeviceRejectedException>(() => sim.Link.UploadBank(records, bad.Select(p => p.Name).ToList()));
        Assert.Equal(LinkProtocol.ErrorCode.SlotTaken, ex.Code);

        var (_, pulled) = sync.Pull(sim.Link);
        Assert.Equal(2, pulled.Count);
        Assert.True(sync.Compare(good, pulled).IsEmpty);
    }

    [SkippableFact]
    public void Too_big_a_bank_is_refused_before_anything_is_written()
    {
        var sim = Start(capacity: 600); // room for one record, not three
        var sync = new BankSync(_s);
        var ex = Assert.Throws<DeviceRejectedException>(() => sync.Push(sim.Link, SamplePresets(3)));
        Assert.Equal(LinkProtocol.ErrorCode.NoSpace, ex.Code);
        Assert.Contains("room", ex.Message);
        Assert.Equal(0, sim.Link.Hello().PresetCount);
    }

    [SkippableFact]
    public void Emptying_the_bank_is_a_valid_transfer()
    {
        var sim = Start();
        var sync = new BankSync(_s);
        sync.Push(sim.Link, SamplePresets(4));
        sync.Push(sim.Link, Array.Empty<UserPreset>());
        Assert.Equal(0, sim.Link.Hello().PresetCount);
    }

    [SkippableFact]
    public void The_full_62_preset_bank_fits_and_reports_progress()
    {
        var sim = Start(capacity: 16 * 1024);
        var sync = new BankSync(_s);
        var progress = new SyncProgress();
        sync.Push(sim.Link, SamplePresets(62), progress);
        Assert.Equal(62, sim.Link.Hello().PresetCount);
        Assert.Contains(progress.Reports, r => r.Step.StartsWith("Sending") && r.Done == 62 && r.Total == 62);
        Assert.Contains(progress.Reports, r => r.Step.StartsWith("Reading") && r.Done == 62);
    }

    [SkippableFact]
    public void Audition_plays_and_voice_capture_returns_what_is_playing()
    {
        var sim = Start();
        var p = SamplePresets(1)[0];
        p.Values["filter.resonance"] = 0.64f;
        sim.Link.Audition(1, _s.Codec.Encode(p), p.Name);
        var grabbed = _s.Codec.Decode(sim.Link.ReadVoice(1));
        Assert.Equal(0.64f, grabbed.Values["filter.resonance"], 4);
        Assert.Equal(p.BaseIndex, grabbed.BaseIndex);
        Assert.Throws<ArgumentOutOfRangeException>(() => sim.Link.Audition(4, _s.Codec.Encode(p)));

        p.Values["out.level"] = 1.0f;
        var bad = _s.Codec.Encode(p);
        bad[_s.Schema.Record.PatchOffset + _s.Schema["out.level"].Offset + 3] = 0x7F; // huge value
        var ex = Assert.Throws<DeviceRejectedException>(() => sim.Link.Audition(0, bad, "Loud"));
        Assert.Equal("out.level", ex.Field!.Key);
    }

    [SkippableFact]
    public void Factory_presets_on_the_box_match_the_ones_built_into_the_editor()
    {
        var sim = Start();
        for (int i = 0; i < _s.Factory.Count; i++)
        {
            var onBox = _s.Codec.Decode(sim.Link.ReadFactory(i));
            Assert.Equal(_s.Factory[i].Name, onBox.Name);
            Assert.True(_s.Factory[i].Values.SameAs(onBox.Values), $"factory preset {_s.Factory[i].Name} differs between box and editor");
        }
        Assert.Throws<DeviceRejectedException>(() => sim.Link.ReadFactory(200));
    }

    [SkippableFact]
    public void A_dead_link_times_out_with_advice()
    {
        var exe = TestSupport.SimulatorPath();
        Skip.If(exe is null, "preset_link_sim is not built");
        // A pipe nobody answers on: the editor must say so, not hang.
        using var silent = new StreamTransport(new MemoryStream(), Stream.Null, "silent");
        using var link = new DeviceLink(silent, _s.Schema, replyTimeoutMs: 100);
        var ex = Assert.Throws<LinkClosedException>(() => link.Hello());
        Assert.Contains("lost", ex.Message);
    }

    [SkippableFact]
    public void An_unresponsive_but_open_port_times_out()
    {
        var pipe = new System.IO.Pipes.AnonymousPipeServerStream(System.IO.Pipes.PipeDirection.In);
        using var silent = new StreamTransport(pipe, Stream.Null, "silent");
        using var link = new DeviceLink(silent, _s.Schema, replyTimeoutMs: 100);
        var ex = Assert.Throws<LinkTimeoutException>(() => link.Hello());
        Assert.Contains("did not answer", ex.Message);
    }
}
