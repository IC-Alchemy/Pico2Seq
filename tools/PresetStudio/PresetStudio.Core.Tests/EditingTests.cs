using PresetStudio.Display;
using PresetStudio.Editing;
using PresetStudio.Model;
using Xunit;

namespace PresetStudio.Tests;

public class EditingTests
{
    private readonly StudioContext _s = TestSupport.Studio;

    private PresetEditor EditorFor(string factory, int pad = 0) => new(TestSupport.NewPreset(factory, pad), _s);

    [Fact]
    public void A_new_preset_starts_identical_to_its_base()
    {
        var e = EditorFor("Bass");
        Assert.Equal(0, e.ChangedCount);
        Assert.Empty(e.ChangedParameters);
        Assert.Empty(e.Issues);
        Assert.All(e.Parameters, p => Assert.False(p.IsChanged));
    }

    [Fact]
    public void Editing_a_value_marks_it_changed_and_can_be_reset()
    {
        var e = EditorFor("Bass");
        var raised = new List<EditKind>();
        e.Edited += (_, k) => raised.Add(k);
        var res = e.Parameter("filter.resonance");
        var before = res.Value;
        res.Value = 0.9f;
        Assert.True(res.IsChanged);
        Assert.Equal(1, e.ChangedCount);
        Assert.Equal(new[] { EditKind.Value }, raised);
        res.ResetToBase();
        Assert.Equal(before, res.Value);
        Assert.False(res.IsChanged);
        Assert.Equal(0, e.ChangedCount);
    }

    [Fact]
    public void Values_are_clamped_to_the_range_for_this_preset()
    {
        var e = EditorFor("Bass");
        var res = e.Parameter("filter.resonance");
        res.Value = 5f;
        Assert.Equal(1f, res.Value);
        res.Value = -2f;
        Assert.Equal(0f, res.Value);
        e.Parameter("osc1.harmony").Value = 3.7f;
        Assert.Equal(4f, e.Parameter("osc1.harmony").Value);
    }

    [Fact]
    public void Switching_the_engine_changes_what_applies_and_pulls_ranges_in()
    {
        var e = EditorFor("Lead");
        Assert.True(e.Parameter("osc1.level").IsEnabled);
        Assert.False(e.Parameter("string.t60").IsEnabled);
        Assert.Contains("Waveguide", e.Parameter("string.t60").DisabledReason);

        e.Parameter("env.attack").Value = 1.5f; // allowed on oscillator voices
        e.Parameter("source.engine").Value = _s.Schema.Engines.Waveguide;
        Assert.False(e.Parameter("osc1.level").IsEnabled);
        Assert.True(e.Parameter("string.t60").IsEnabled);
        Assert.Equal(10f, e.Parameter("env.attack").Max);

        e.Parameter("source.engine").Value = _s.Schema.Engines.Osc;
        Assert.Equal(2f, e.Parameter("env.attack").Max);
        e.Parameter("env.attack").Value = 8f;
        Assert.Equal(2f, e.Parameter("env.attack").Value);
    }

    [Fact]
    public void Back_on_oscillators_there_is_always_at_least_one()
    {
        var e = EditorFor("Lead");
        e.Parameter("source.oscCount").Value = 0;
        e.Parameter("source.engine").Value = _s.Schema.Engines.Waveguide;
        e.Parameter("source.engine").Value = _s.Schema.Engines.Osc;
        Assert.Equal(1f, e.Parameter("source.oscCount").Value);
    }

    [Fact]
    public void Oscillator_controls_follow_the_oscillator_count_and_waveform()
    {
        var e = EditorFor("Lead");
        e.Parameter("source.oscCount").Value = 1;
        Assert.True(e.Parameter("osc1.wave").IsEnabled);
        Assert.False(e.Parameter("osc2.wave").IsEnabled);
        Assert.Contains("at least 2", e.Parameter("osc2.level").DisabledReason);
        e.Parameter("osc1.wave").Value = _s.Schema.Waveforms.Square;
        Assert.True(e.Parameter("osc1.pulse").IsEnabled);
        e.Parameter("osc1.wave").Value = 2; // saw
        Assert.False(e.Parameter("osc1.pulse").IsEnabled);
    }

    [Fact]
    public void Stage_switches_gate_their_controls()
    {
        var e = EditorFor("Lead");
        e.Parameter("filter.enabled").BoolValue = false;
        Assert.False(e.Parameter("filter.cutoff").IsEnabled);
        Assert.False(e.Parameter("filter.drive").IsEnabled);
        e.Parameter("filter.enabled").BoolValue = true;
        e.Parameter("filter.type").Value = 1; // state-variable: no ladder-only controls
        Assert.True(e.Parameter("filter.cutoff").IsEnabled);
        Assert.False(e.Parameter("filter.drive").IsEnabled);
        e.Parameter("drive.enabled").BoolValue = false;
        Assert.False(e.Parameter("drive.amount").IsEnabled);
        e.Parameter("env.enabled").BoolValue = false;
        Assert.False(e.Parameter("env.release").IsEnabled);
    }

    [Fact]
    public void The_recipe_engine_is_offered_only_for_recipe_bases()
    {
        var plain = EditorFor("Bass");
        Assert.DoesNotContain(plain.Parameter("source.engine").Choices, c => c.Value == _s.Schema.Engines.Recipe);
        var recipe = new PresetEditor(_s.Factory.NewFrom(_s.Factory.Presets.First(f => f.HasRecipe).Index, _s.Schema), _s);
        Assert.Contains(recipe.Parameter("source.engine").Choices, c => c.Value == _s.Schema.Engines.Recipe);
    }

    [Fact]
    public void Recipe_controls_are_labelled_by_the_recipe()
    {
        var phase = new PresetEditor(_s.Factory.NewFrom(_s.Factory.FindByName("PhaseMorph")!.Index, _s.Schema), _s);
        Assert.Equal("Shape", phase.Parameter("recipe.macro1").Label);
        Assert.Equal("Skew", phase.Parameter("recipe.macro2").Label);
        Assert.True(phase.Parameter("recipe.phaseFold").IsEnabled);
        Assert.False(phase.Parameter("recipe.fmFeedback").IsEnabled);
        Assert.Equal(-1f, phase.Parameter("recipe.macro2").Min);
        var plain = EditorFor("Bass");
        Assert.NotEqual("Skew", plain.Parameter("recipe.macro2").Label);
    }

    [Fact]
    public void Changing_the_base_can_keep_the_edits_or_start_over()
    {
        var e = EditorFor("Bass");
        e.Parameter("filter.resonance").Value = 0.77f;
        var lead = _s.Factory.FindByName("Lead")!.Index;
        e.ChangeBase(lead, keepValues: true);
        Assert.Equal(lead, e.Preset.BaseIndex);
        Assert.Equal(0.77f, e.Parameter("filter.resonance").Value);
        Assert.True(e.Parameter("filter.resonance").IsChanged); // relative to the new base

        e.ChangeBase(lead, keepValues: false);
        Assert.Equal(0, e.ChangedCount);
    }

    [Fact]
    public void Changing_to_a_non_recipe_base_drops_a_recipe_engine()
    {
        var recipe = _s.Factory.Presets.First(f => f.HasRecipe);
        var e = new PresetEditor(_s.Factory.NewFrom(recipe.Index, _s.Schema), _s);
        e.ChangeBase(_s.Factory.FindByName("Bass")!.Index, keepValues: true);
        Assert.Equal(_s.Schema.Engines.Osc, (int)e.Parameter("source.engine").Value);
        Assert.Empty(e.Issues);
    }

    [Fact]
    public void Names_are_sanitised_and_identity_edits_are_reported()
    {
        var e = EditorFor("Bass");
        var kinds = new List<EditKind>();
        e.Edited += (_, k) => kinds.Add(k);
        e.Name = "A very long preset name indeed";
        Assert.Equal(15, e.Name.Length);
        e.ColorHex = "#FF8800";
        Assert.Equal(new Rgb(255, 136, 0), e.Color);
        e.ColorHex = "not a colour";
        Assert.Equal(new Rgb(255, 136, 0), e.Color);
        e.Place(2, 7);
        Assert.Equal((2, 7), (e.Page, e.Pad));
        Assert.Equal(new[] { EditKind.Identity, EditKind.Identity, EditKind.Identity }, kinds);
    }

    [Fact]
    public void Text_entry_understands_units()
    {
        var e = EditorFor("Lead");
        var release = e.Parameter("env.release");
        release.EditText = "250ms";
        Assert.Equal(0.25f, release.Value, 4);
        release.EditText = "1.5 s";
        Assert.Equal(1.5f, release.Value, 4);
        release.EditText = "gibberish";
        Assert.Equal(1.5f, release.Value, 4);

        var res = e.Parameter("filter.resonance");
        res.EditText = "37%";
        Assert.Equal(0.37f, res.Value, 4);
        res.EditText = "50";
        Assert.Equal(0.5f, res.Value, 4);

        var detune = e.Parameter("osc2.detune");
        detune.EditText = "+7";
        Assert.Equal(7f, detune.Value);
        Assert.Equal("+7 st", detune.Text);

        e.Parameter("hp.cutoff").EditText = "off";
        Assert.Equal(0f, e.Parameter("hp.cutoff").Value);
        e.Parameter("hp.cutoff").EditText = "1.2k";
        Assert.Equal(1200f, e.Parameter("hp.cutoff").Value);
        e.Parameter("osc1.wave").EditText = "square";
        Assert.Equal(_s.Schema.Waveforms.Square, (int)e.Parameter("osc1.wave").Value);
    }

    [Fact]
    public void Values_display_the_way_the_box_shows_them()
    {
        var s = _s.Schema;
        string Show(string key, float v) => ValueFormatter.Format(s[key], s[key].Unit, v);
        Assert.Equal("37 %", Show("filter.resonance", 0.37f));
        Assert.Equal("250 ms", Show("env.release", 0.25f));
        Assert.Equal("1.5 s", Show("env.release", 1.5f));
        Assert.Equal("Off", Show("hp.cutoff", 0f));
        Assert.Equal("80 Hz", Show("hp.cutoff", 80f));
        Assert.Equal("1.2 kHz", Show("hp.cutoff", 1200f));
        Assert.Equal("+7 st", Show("osc2.detune", 7f));
        Assert.Equal("-12 st", Show("base.octave", -12f));
        Assert.Equal("6 ct", Show("string.detune", 6f));
        Assert.Equal("On", Show("env.enabled", 1f));
        Assert.Equal("Saw", Show("osc1.wave", 2f));
        Assert.Equal("Noise", Show("osc1.wave", 255f));
        Assert.Equal("LP24", Show("filter.mode", 0f));
        Assert.Equal("3", Show("source.oscCount", 3f));
    }

    [Fact]
    public void Sliders_are_logarithmic_for_time_and_frequency_and_round_trip()
    {
        var e = EditorFor("Lead");
        foreach (var key in new[] { "env.release", "env.decay", "filter.resonance", "osc2.detune", "base.glide" })
        {
            var p = e.Parameter(key);
            foreach (var pos in new[] { 0.1, 0.5, 0.9 })
            {
                p.SliderPosition = pos;
                Assert.Equal(pos, p.SliderPosition, 2);
            }
        }
        // Halfway along a time slider is the geometric middle, not the arithmetic one.
        var release = e.Parameter("env.release");
        release.SliderPosition = 0.5;
        Assert.InRange(release.Value, 0.2f, 0.4f);
        // The high-pass has an Off stop at the far left.
        var hp = e.Parameter("hp.cutoff");
        hp.SliderPosition = 0;
        Assert.Equal(0f, hp.Value);
        hp.SliderPosition = 0.5;
        Assert.InRange(hp.Value, 100f, 2000f);
    }

    [Fact]
    public void The_filter_cutoff_shows_hertz_and_lanes_explain_themselves()
    {
        var bass = EditorFor("Bass");
        Assert.Contains("Hz", bass.Parameter("filter.cutoff").Hint);
        var wg = EditorFor("WgPluck");
        Assert.Contains("Decay lane", wg.Parameter("string.t60").Hint);
        Assert.Contains("T60", wg.Parameter("string.t60").Hint);
    }

    [Fact]
    public void Tabs_cover_every_value_and_count_edits()
    {
        var e = EditorFor("Bass");
        Assert.Equal(_s.Schema.Fields.Count, e.Tabs.SelectMany(t => t.Sections).SelectMany(s => s.Parameters).Count());
        var filterTab = e.Tabs.First(t => t.Title == "Filter");
        Assert.Equal(0, filterTab.ChangedCount);
        e.Parameter("filter.resonance").Value = 0.9f;
        Assert.Equal(1, filterTab.ChangedCount);
        Assert.True(filterTab.HasEnabled);
    }
}

public class AuditionPumpTests
{
    [Fact]
    public void Only_the_latest_request_survives_a_burst_and_the_last_one_always_goes_out()
    {
        var sent = new List<string>();
        using var pump = new AuditionPump((_, name) => { lock (sent) sent.Add(name); }, minIntervalMs: 60);
        for (int i = 0; i < 50; i++) pump.Submit(new byte[] { (byte)i }, $"v{i}");
        Assert.True(pump.WaitIdle());
        lock (sent)
        {
            Assert.Equal("v49", sent[^1]);
            Assert.True(sent.Count < 10, $"{sent.Count} sends for a 50-change burst");
        }
    }

    [Fact]
    public void Sends_are_spaced_and_never_overlap()
    {
        var times = new List<long>();
        var inside = 0;
        var overlapped = false;
        using var pump = new AuditionPump((_, _) =>
        {
            if (Interlocked.Increment(ref inside) > 1) overlapped = true;
            lock (times) times.Add(Environment.TickCount64);
            Thread.Sleep(10);
            Interlocked.Decrement(ref inside);
        }, minIntervalMs: 50);
        for (int i = 0; i < 6; i++)
        {
            pump.Submit(new byte[1], "x");
            Thread.Sleep(15);
        }
        Assert.True(pump.WaitIdle());
        Assert.False(overlapped);
        lock (times)
            for (int i = 1; i < times.Count; i++)
                Assert.True(times[i] - times[i - 1] >= 40, "sends must be at least ~the interval apart");
    }

    [Fact]
    public void A_failure_is_reported_and_the_next_request_still_goes_out()
    {
        var failures = new List<Exception>();
        var ok = 0;
        var calls = 0;
        using var pump = new AuditionPump((_, _) =>
        {
            if (Interlocked.Increment(ref calls) == 1) throw new InvalidOperationException("cable out");
            Interlocked.Increment(ref ok);
        }, minIntervalMs: 5);
        pump.Failed += e => { lock (failures) failures.Add(e); };
        pump.Submit(new byte[1], "a");
        Assert.True(pump.WaitIdle());
        pump.Submit(new byte[1], "b");
        Assert.True(pump.WaitIdle());
        Assert.Single(failures);
        Assert.Equal(1, ok);
    }
}
