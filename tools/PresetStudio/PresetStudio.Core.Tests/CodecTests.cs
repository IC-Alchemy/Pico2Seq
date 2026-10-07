using System.Text.Json;
using PresetStudio.Codec;
using PresetStudio.Model;
using PresetStudio.Rules;
using Xunit;

namespace PresetStudio.Tests;

public class CodecTests
{
    private readonly Editing.StudioContext _s = TestSupport.Studio;

    [Fact]
    public void Crc32_matches_the_standard_check_value_and_continues_across_pieces()
    {
        var data = "123456789"u8.ToArray();
        Assert.Equal(0xCBF43926u, Crc32.Compute(data));
        Assert.Equal(Crc32.Compute(data), Crc32.Update(Crc32.Compute(data.AsSpan(0, 4)), data.AsSpan(4)));
        Assert.Equal(0u, Crc32.Compute(ReadOnlySpan<byte>.Empty));
    }

    [Fact]
    public void Every_factory_preset_encodes_to_exactly_the_bytes_the_firmware_produces()
    {
        // tests/unit/test_preset_resources.cpp dumps each factory preset as the firmware would store it.
        using var golden = TestSupport.Golden();
        var records = golden.RootElement.GetProperty("records").EnumerateArray().ToList();
        Assert.Equal(_s.Factory.Count, records.Count);
        foreach (var r in records)
        {
            var preset = _s.Factory.NewFrom(r.GetProperty("base").GetInt32(), _s.Schema);
            preset.Name = r.GetProperty("name").GetString()!;
            preset.Page = r.GetProperty("page").GetInt32();
            preset.Pad = r.GetProperty("pad").GetInt32();
            preset.Color = Rgb.FromUInt32(r.GetProperty("color").GetUInt32());
            var expected = r.GetProperty("hex").GetString()!;
            Assert.Equal(expected, TestSupport.ToHex(_s.Codec.Encode(preset)));
        }
    }

    [Fact]
    public void Decoding_a_record_and_encoding_it_again_is_lossless()
    {
        using var golden = TestSupport.Golden();
        foreach (var r in golden.RootElement.GetProperty("records").EnumerateArray())
        {
            var bytes = TestSupport.FromHex(r.GetProperty("hex").GetString()!);
            var preset = _s.Codec.Decode(bytes);
            Assert.Equal(r.GetProperty("name").GetString(), preset.Name);
            Assert.Equal(bytes, _s.Codec.Encode(preset));
        }
    }

    [Fact]
    public void Encoding_canonicalises_what_the_editor_does_not_own()
    {
        var p = TestSupport.NewPreset("Bass");
        p.Values["base.octave"] = 13f;      // not a whole octave
        p.Values["base.note"] = 2.6f;       // not a whole step
        var record = _s.Codec.Encode(p);
        var back = _s.Codec.Decode(record);
        Assert.Equal(12f, back.Values["base.octave"]);
        Assert.Equal(3f, back.Values["base.note"]);
        // use-patch-bases is always set and presetIndex mirrors the base
        var layout = _s.Schema.Record;
        Assert.Equal(p.BaseIndex, record[layout.PresetIndexOffset]);
        Assert.Equal(1, record[layout.FlagsOffset] & layout.UsePatchBasesMask);
    }

    [Fact]
    public void Lane_set_follows_the_engine_and_hard_sync_like_the_box()
    {
        var p = TestSupport.NewPreset("Bass");
        var ps = _s.Schema.ParamSets;
        p.Values["source.engine"] = _s.Schema.Engines.Osc;
        p.Values["source.oscCount"] = 2;
        p.Values["osc2.wave"] = _s.Schema.Waveforms.HardSyncSaw;
        Assert.Equal(ps.HardSync, _s.Codec.DeriveParamSet(p));
        p.Values["source.oscCount"] = 1; // the hard-sync oscillator is no longer active
        Assert.Equal(ps.Standard, _s.Codec.DeriveParamSet(p));
        p.Values["source.engine"] = _s.Schema.Engines.Waveguide;
        Assert.Equal(ps.Waveguide, _s.Codec.DeriveParamSet(p));
        p.Values["source.engine"] = _s.Schema.Engines.Hypersaw;
        Assert.Equal(ps.Hypersaw, _s.Codec.DeriveParamSet(p));
        p.Values["source.engine"] = _s.Schema.Engines.NoiseFx;
        Assert.Equal(ps.NoiseStorm, _s.Codec.DeriveParamSet(p));
    }

    [Fact]
    public void Names_are_trimmed_to_printable_ascii_within_the_limit()
    {
        Assert.Equal("Warm Bass", _s.Codec.SanitizeName("Warm Bass"));
        Assert.Equal("123456789012345", _s.Codec.SanitizeName("123456789012345678"));
        Assert.Equal("Caf? Pad", _s.Codec.SanitizeName("Café Pad"));
        Assert.Equal("", _s.Codec.SanitizeName(null));
        var p = TestSupport.NewPreset();
        p.Name = "Tab\there";
        var record = _s.Codec.Encode(p);
        Assert.Equal("Tab?here", _s.Codec.Decode(record).Name);
    }

    [Fact]
    public void Filter_cutoff_curve_matches_the_firmware_at_every_check_point()
    {
        foreach (var f in _s.Factory.Presets)
            foreach (var (x, hz) in f.CutoffSamples)
            {
                var mine = CutoffMap.ToHertz(f.Cutoff, x);
                Assert.True(Math.Abs(mine - hz) <= 1e-3f * Math.Max(1f, hz), $"{f.Name} at {x}: {mine} vs firmware {hz}");
            }
    }

    [Fact]
    public void Cutoff_map_inverts()
    {
        foreach (var f in _s.Factory.Presets)
            foreach (var x in new[] { 0.05f, 0.3f, 0.5f, 0.8f })
            {
                var hz = CutoffMap.ToHertz(f.Cutoff, x);
                Assert.Equal(x, CutoffMap.FromHertz(f.Cutoff, hz), 3);
            }
    }

    [Fact]
    public void Rgb_hex_round_trips()
    {
        Assert.Equal(new Rgb(0x12, 0xAB, 0xFF), Rgb.ParseHex("#12abff"));
        Assert.Equal("#12ABFF", new Rgb(0x12, 0xAB, 0xFF).ToHex());
        Assert.False(Rgb.TryParseHex("12345", out _));
        Assert.False(Rgb.TryParseHex("#GGGGGG", out _));
        Assert.False(Rgb.TryParseHex(null, out _));
        Assert.Equal(new Rgb(64, 32, 16), new Rgb(255, 128, 64).Scaled(0.25));
    }

    [Fact]
    public void Record_size_is_enforced()
    {
        Assert.Throws<ArgumentException>(() => _s.Codec.Decode(new byte[255]));
    }
}
