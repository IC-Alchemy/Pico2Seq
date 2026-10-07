using System.Text.Json;
using PresetStudio.Link;
using Xunit;

namespace PresetStudio.Tests;

public class LinkProtocolTests
{
    [Fact]
    public void Frames_are_byte_identical_to_what_the_firmware_expects()
    {
        // Each requests/reply pair was produced by the firmware's own session in tests/unit/test_preset_resources.cpp.
        using var golden = TestSupport.Golden();
        foreach (var f in golden.RootElement.GetProperty("frames").EnumerateArray())
        {
            var name = f.GetProperty("name").GetString();
            var payload = TestSupport.FromHex(f.GetProperty("payload").GetString()!);
            var expected = f.GetProperty("request").GetString();
            var mine = LinkProtocol.Encode((byte)f.GetProperty("command").GetInt32(), (byte)f.GetProperty("seq").GetInt32(), payload);
            Assert.True(expected == TestSupport.ToHex(mine), $"request frame '{name}' differs from the firmware's");
        }
    }

    [Fact]
    public void Replies_from_the_firmware_parse_and_carry_the_right_type_and_sequence()
    {
        using var golden = TestSupport.Golden();
        foreach (var f in golden.RootElement.GetProperty("frames").EnumerateArray())
        {
            var parser = new FrameParser();
            var bytes = TestSupport.FromHex(f.GetProperty("reply").GetString()!);
            LinkFrame? got = null;
            foreach (var b in bytes)
                if (parser.Push(b, 0) == FrameParser.PushResult.Frame) got = parser.Frame;
            Assert.NotNull(got);
            Assert.Equal((byte)f.GetProperty("seq").GetInt32(), got!.Seq);
            var command = (byte)f.GetProperty("command").GetInt32();
            var isError = got.Type == LinkProtocol.ErrorType;
            if (!isError) Assert.Equal((byte)(command | LinkProtocol.ReplyBit), got.Type);
        }
    }

    [Fact]
    public void Hello_reply_from_the_firmware_describes_a_compatible_device()
    {
        using var golden = TestSupport.Golden();
        var hello = golden.RootElement.GetProperty("frames").EnumerateArray().First(f => f.GetProperty("name").GetString() == "hello");
        var bytes = TestSupport.FromHex(hello.GetProperty("reply").GetString()!);
        var parser = new FrameParser();
        foreach (var b in bytes) parser.Push(b, 0);
        var info = DeviceInfo.Parse(parser.Frame!.Payload);
        Assert.Equal(LinkProtocol.ProtocolVersion, info.Protocol);
        Assert.Equal(TestSupport.Studio.Factory.Count, info.FactoryCount);
        Assert.Equal(62, info.MaxPresets);
        Assert.Equal(0, info.PresetCount);
        Assert.Null(info.Incompatibility(TestSupport.Studio.Schema));
    }

    [Fact]
    public void Error_reply_names_the_field_the_firmware_objected_to()
    {
        using var golden = TestSupport.Golden();
        var f = golden.RootElement.GetProperty("frames").EnumerateArray().First(x => x.GetProperty("name").GetString() == "put_invalid");
        var parser = new FrameParser();
        foreach (var b in TestSupport.FromHex(f.GetProperty("reply").GetString()!)) parser.Push(b, 0);
        var p = parser.Frame!.Payload;
        Assert.Equal(LinkProtocol.ErrorType, parser.Frame.Type);
        Assert.Equal(LinkProtocol.Command.BankPut, p[0]);
        var ex = DeviceRejectedException.From(p[0], p[1], p[2], p[3], TestSupport.Studio.Schema, "Golden Bass");
        Assert.Equal(LinkProtocol.ErrorCode.InvalidRecord, ex.Code);
        Assert.Equal("filter.resonance", ex.Field!.Key);
        Assert.Contains("Golden Bass", ex.Message);
        Assert.Contains("filter.resonance", ex.Message);
    }

    [Fact]
    public void Parser_ignores_log_text_and_reports_it_as_console()
    {
        var parser = new FrameParser();
        var text = "[DIAG C0] ids=0,1,2,3 W\n"u8.ToArray();
        var console = 0;
        foreach (var b in text)
            if (parser.Push(b, 0) == FrameParser.PushResult.Console) console++;
        Assert.Equal(text.Length, console);

        var frame = LinkProtocol.Encode(1, 9, new byte[] { 0x57, 0xA5, 0x5A });
        LinkFrame? got = null;
        foreach (var b in frame)
            if (parser.Push(b, 0) == FrameParser.PushResult.Frame) got = parser.Frame;
        Assert.Equal(new byte[] { 0x57, 0xA5, 0x5A }, got!.Payload);
    }

    [Fact]
    public void Parser_drops_corrupt_frames_and_recovers()
    {
        var parser = new FrameParser();
        var bad = LinkProtocol.Encode(3, 1, new byte[] { 1, 2, 3 });
        bad[7] ^= 1;
        var results = bad.Select(b => parser.Push(b, 0)).ToList();
        Assert.Equal(FrameParser.PushResult.Dropped, results[^1]);
        var good = LinkProtocol.Encode(1, 2, ReadOnlySpan<byte>.Empty);
        // ToList: Select(...).Last() on an array would only run the selector on the final byte.
        Assert.Equal(FrameParser.PushResult.Frame, good.Select(b => parser.Push(b, 0)).ToList()[^1]);
        Assert.Equal(2, parser.Frame!.Seq);
    }

    [Fact]
    public void Parser_rejects_impossible_lengths_at_once_and_times_out_half_frames()
    {
        var parser = new FrameParser();
        var results = new byte[] { 0xA5, 0x5A, 3, 1, 0xFF, 0xFF }.Select(b => parser.Push(b, 0)).ToList();
        Assert.Equal(FrameParser.PushResult.Dropped, results[^1]);

        var frame = LinkProtocol.Encode(3, 5, new byte[100]);
        for (int i = 0; i < 40; i++) parser.Push(frame[i], 1000);
        Assert.True(parser.InFrame);
        // A quarter of a second of silence abandons the stale frame, so the next one is not swallowed.
        var next = LinkProtocol.Encode(1, 6, ReadOnlySpan<byte>.Empty);
        var last = FrameParser.PushResult.Pending;
        foreach (var b in next) last = parser.Push(b, 1000 + LinkProtocol.ByteTimeoutMs + 5);
        Assert.Equal(FrameParser.PushResult.Frame, last);
    }

    [Fact]
    public void Encoder_refuses_oversize_payloads()
    {
        Assert.Throws<ArgumentException>(() => LinkProtocol.Encode(3, 1, new byte[LinkProtocol.MaxPayload + 1]));
        Assert.Equal(LinkProtocol.FrameOverhead + LinkProtocol.MaxPayload, LinkProtocol.Encode(3, 1, new byte[LinkProtocol.MaxPayload]).Length);
    }

    [Fact]
    public void Incompatible_firmware_is_explained()
    {
        var schema = TestSupport.Studio.Schema;
        var good = DeviceInfo.Parse(new byte[28].Select((_, i) => (byte)0).ToArray()) with
        {
            Protocol = 1, LayoutVersion = schema.LayoutVersion, RecordSize = schema.RecordSize, PatchSize = schema.PatchSize,
            UserPages = schema.Browser.UserPages, PadsPerPage = schema.Browser.PadsPerPage, MaxPresets = schema.Browser.MaxPresets,
            TableHash = schema.TableHash,
        };
        Assert.Null(good.Incompatibility(schema));
        Assert.Contains("older", (good with { LayoutVersion = 0 }).Incompatibility(schema));
        Assert.Contains("newer", (good with { LayoutVersion = 9 }).Incompatibility(schema));
        Assert.NotNull((good with { TableHash = 1 }).Incompatibility(schema));
        Assert.Contains("newer", (good with { Protocol = 2 }).Incompatibility(schema));
        Assert.Contains("older", (good with { Protocol = 0 }).Incompatibility(schema));
        Assert.NotNull((good with { MaxPresets = 31 }).Incompatibility(schema));
        Assert.Throws<LinkException>(() => DeviceInfo.Parse(new byte[10]));
    }
}
