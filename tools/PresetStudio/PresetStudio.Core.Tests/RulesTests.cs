using PresetStudio.Model;
using PresetStudio.Rules;
using PresetStudio.Schema;
using Xunit;

namespace PresetStudio.Tests;

public class RulesTests
{
    private readonly Editing.StudioContext _s = TestSupport.Studio;

    [Fact]
    public void Every_factory_preset_is_valid_as_a_user_preset()
    {
        foreach (var f in _s.Factory.Presets)
        {
            var p = _s.Factory.NewFrom(f.Index, _s.Schema);
            var issues = _s.Validator.Validate(p);
            Assert.True(issues.Count == 0, $"{f.Name}: {string.Join("; ", issues)}");
        }
    }

    [Fact]
    public void Validator_finds_what_the_box_would_refuse()
    {
        var p = TestSupport.NewPreset("Bass");
        p.Values["filter.resonance"] = 1.5f;
        p.Values["osc1.wave"] = 7f;
        p.Values["osc2.harmony"] = 13f;
        p.Name = "";
        p.Page = 0;
        p.Pad = 31;
        var issues = _s.Validator.Validate(p);
        Assert.Contains(issues, i => i.FieldKey == "filter.resonance");
        Assert.Contains(issues, i => i.FieldKey == "osc1.wave");
        Assert.Contains(issues, i => i.FieldKey == "osc2.harmony");
        Assert.Contains(issues, i => i.Message.Contains("name is empty"));
        Assert.Contains(issues, i => i.Message.Contains("Page 1 does not exist"));
        Assert.Contains(issues, i => i.Message.Contains("Pad 31"));
        Assert.All(issues, i => Assert.Equal(IssueSeverity.Error, i.Severity));
    }

    [Fact]
    public void Non_numbers_are_refused()
    {
        var p = TestSupport.NewPreset("Bass");
        p.Values["filter.cutoff"] = float.NaN;
        p.Values["out.level"] = float.PositiveInfinity;
        var issues = _s.Validator.Validate(p);
        Assert.Contains(issues, i => i.FieldKey == "filter.cutoff");
        Assert.Contains(issues, i => i.FieldKey == "out.level");
    }

    [Fact]
    public void Recipe_engine_needs_a_recipe_base()
    {
        var p = TestSupport.NewPreset("Bass");
        p.Values["source.engine"] = _s.Schema.Engines.Recipe;
        Assert.Contains(_s.Validator.Validate(p), i => i.Message.Contains("Recipe engine needs a base preset"));
        var recipe = _s.Factory.Presets.First(f => f.HasRecipe);
        var q = _s.Factory.NewFrom(recipe.Index, _s.Schema);
        q.Page = 1;
        Assert.Empty(_s.Validator.Validate(q));
    }

    [Fact]
    public void Library_checks_catch_clashes_overflow_and_duplicate_names()
    {
        var a = TestSupport.NewPreset("Bass", pad: 3);
        var b = TestSupport.NewPreset("Lead", pad: 3);
        b.Name = a.Name;
        var issues = _s.Validator.ValidateLibrary(new[] { a, b });
        Assert.Contains(issues, i => i.Severity == IssueSeverity.Error && i.Message.Contains("both on page 2, pad 3"));
        Assert.Contains(issues, i => i.Severity == IssueSeverity.Warning && i.Message.Contains("presets are called"));

        var many = Enumerable.Range(0, 63).Select(i => TestSupport.NewPreset("Bass", pad: i % 31)).ToList();
        Assert.Contains(_s.Validator.ValidateLibrary(many), i => i.Message.Contains("holds 62 presets"));
    }

    [Fact]
    public void Ranges_narrow_with_the_base_preset_and_widen_back_when_the_engine_changes()
    {
        // Oscillator voices limit the envelope attack to the sequencer lane's 2 seconds...
        var osc = TestSupport.NewPreset("Lead");
        var attack = _s.Schema["env.attack"];
        Assert.Equal(2f, _s.Limits.Resolve(attack, osc).Max);
        // ...and on the waveguide engine the same value may reach 10 seconds.
        osc.Values["source.engine"] = _s.Schema.Engines.Waveguide;
        Assert.Equal(10f, _s.Limits.Resolve(attack, osc).Max);
    }

    [Fact]
    public void Recipe_macros_take_their_names_and_spans_from_the_base_preset()
    {
        var phase = _s.Factory.FindByName("PhaseMorph")!;
        var p = _s.Factory.NewFrom(phase.Index, _s.Schema);
        var skew = _s.Limits.Resolve(_s.Schema["recipe.macro2"], p);
        Assert.Equal("Skew", skew.Label);
        Assert.Equal(-1f, skew.Min);
        Assert.Equal(1f, skew.Max);
    }

    [Fact]
    public void Clamp_rounds_integers_snaps_choices_and_survives_garbage()
    {
        var p = TestSupport.NewPreset("Bass");
        Assert.Equal(1f, _s.Limits.Clamp(_s.Schema["filter.resonance"], p, 7f));
        Assert.Equal(0f, _s.Limits.Clamp(_s.Schema["filter.resonance"], p, -3f));
        Assert.Equal(5f, _s.Limits.Clamp(_s.Schema["osc1.harmony"], p, 4.6f));
        Assert.Equal(3f, _s.Limits.Clamp(_s.Schema["osc1.wave"], p, 3.2f));
        Assert.Equal(255f, _s.Limits.Clamp(_s.Schema["osc1.wave"], p, 200f)); // nearest allowed id
        Assert.Equal(0f, _s.Limits.Clamp(_s.Schema["filter.resonance"], p, float.NaN));
    }
}
