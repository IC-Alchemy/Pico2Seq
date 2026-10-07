using PresetStudio.Help;
using PresetStudio.Schema;
using Xunit;

namespace PresetStudio.Tests;

public class SchemaTests
{
    private readonly PatchSchema _schema = TestSupport.Studio.Schema;

    [Fact]
    public void Schema_matches_the_firmware_layout_constants()
    {
        Assert.Equal(1, _schema.LayoutVersion);
        Assert.Equal(256, _schema.RecordSize);
        Assert.Equal(232, _schema.PatchSize);
        Assert.Equal(24, _schema.Record.PatchOffset);
        Assert.Equal(16, _schema.NameSize);
        Assert.Equal(15, _schema.NameMaxLength);
        Assert.Equal(31, _schema.Browser.PadsPerPage);
        Assert.Equal(31, _schema.Browser.PageKeyPad);
        Assert.Equal(2, _schema.Browser.UserPages);
        Assert.Equal(8, _schema.Browser.GridWidth);
        Assert.Equal(4, _schema.Browser.GridHeight);
        Assert.Equal(62, _schema.Browser.MaxPresets);
    }

    [Fact]
    public void Every_field_fits_inside_the_patch_without_overlap()
    {
        var used = new bool[_schema.PatchSize];
        foreach (var f in _schema.Fields)
        {
            var width = f.Type is FieldType.Float or FieldType.Int32 ? 4 : 1;
            Assert.InRange(f.Offset + width, 1, _schema.PatchSize);
            if (f.Type == FieldType.Flag) continue;
            for (int i = 0; i < width; i++)
            {
                Assert.False(used[f.Offset + i], $"{f.Key} overlaps another field");
                used[f.Offset + i] = true;
            }
        }
    }

    [Fact]
    public void Keys_are_unique_and_lookup_works()
    {
        Assert.Equal(_schema.Fields.Count, _schema.Fields.Select(f => f.Key).Distinct().Count());
        foreach (var f in _schema.Fields) Assert.Same(f, _schema[f.Key]);
        Assert.False(_schema.TryGetField("no.such", out _));
    }

    [Fact]
    public void Choice_fields_list_every_value_the_box_offers()
    {
        Assert.Equal(new[] { "Oscillators", "Waveguide", "Noise FX", "Hypersaw", "Recipe" },
            _schema["source.engine"].Choices!.Select(c => c.Label));
        var wave = _schema["osc1.wave"].Choices!;
        Assert.Equal(8, wave.Count);
        Assert.Equal(255, wave[^1].Value);
        Assert.Equal("Noise", wave[^1].Label);
        Assert.Equal(new[] { "LP24", "LP12", "BP24", "BP12", "HP24", "HP12" }, _schema["filter.mode"].Choices!.Select(c => c.Label));
    }

    [Fact]
    public void High_pass_cutoff_may_be_zero_for_off()
    {
        var hp = _schema["hp.cutoff"];
        Assert.Equal(0f, hp.Min);
        Assert.Equal(20000f, hp.Max);
    }

    [Fact]
    public void Help_covers_every_field_and_every_field_is_on_one_tab()
    {
        Assert.Empty(TestSupport.Studio.Help.CheckAgainst(_schema));
    }

    [Fact]
    public void Help_text_is_substantial_for_every_field()
    {
        foreach (var f in _schema.Fields)
        {
            var h = TestSupport.Studio.Help.For(f.Key);
            Assert.True(h.Title.Length >= 3, $"{f.Key} has no title");
            Assert.True(h.Summary.Length >= 10, $"{f.Key} has no summary");
            Assert.True(h.Detail.Length >= 30, $"{f.Key} has no explanation");
        }
    }

    [Fact]
    public void Help_check_reports_missing_entries()
    {
        var help = HelpCatalog.Parse("""
            { "identity": {}, "fields": {}, "tabs": [] }
            """);
        var problems = help.CheckAgainst(_schema);
        Assert.Contains(problems, p => p.Contains("no help for 'base.note'"));
        Assert.Contains(problems, p => p.Contains("'base.note' is on no tab"));
    }

    [Fact]
    public void Show_rules_describe_why_a_value_is_unavailable()
    {
        Assert.Contains("Waveguide", _schema["string.t60"].Show.WhyUnavailable(_schema));
        Assert.Contains("square", _schema["osc1.pulse"].Show.WhyUnavailable(_schema));
        Assert.Equal("", _schema["out.level"].Show.WhyUnavailable(_schema));
    }
}
