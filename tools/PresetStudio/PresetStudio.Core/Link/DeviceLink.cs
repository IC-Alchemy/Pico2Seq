using System.Buffers.Binary;
using PresetStudio.Schema;

namespace PresetStudio.Link;

/// <summary>Progress of a transfer, for a progress bar and a status line.</summary>
public sealed record LinkProgress(string Step, int Done, int Total);

/// <summary>
/// The editor's side of the preset link: one request, one reply, in turn. Everything that changes
/// the box's stored presets is all-or-nothing on the box (a half-sent bank is thrown away), so a cable
/// pulled mid-transfer leaves the old presets exactly as they were.
/// Not thread-safe; use one link from one thread at a time.
/// </summary>
public sealed class DeviceLink : IDisposable
{
    private readonly ILinkTransport _transport;
    private readonly PatchSchema _schema;
    private readonly FrameParser _parser = new();
    private readonly int _replyTimeoutMs;
    private byte _seq;

    public DeviceLink(ILinkTransport transport, PatchSchema schema, int replyTimeoutMs = 1500)
    {
        _transport = transport;
        _schema = schema;
        _replyTimeoutMs = replyTimeoutMs;
    }

    public string Description => _transport.Description;

    /// <summary>Everything the box printed that was not part of a reply (its log), for a diagnostics view.</summary>
    public event Action<string>? ConsoleText;

    public void Dispose() => _transport.Dispose();

    // ---- requests --------------------------------------------------------------------------

    /// <summary>Asks the box who it is. Throws <see cref="IncompatibleDeviceException"/> if the firmware does not fit this editor.</summary>
    public DeviceInfo Hello()
    {
        var info = DeviceInfo.Parse(Exchange(LinkProtocol.Command.Hello, ReadOnlySpan<byte>.Empty, idempotent: true).Payload);
        if (info.Incompatibility(_schema) is { } why) throw new IncompatibleDeviceException(why);
        return info;
    }

    /// <summary>Like <see cref="Hello"/> but returns the raw facts even for a mismatched firmware (for diagnostics).</summary>
    public DeviceInfo HelloUnchecked() =>
        DeviceInfo.Parse(Exchange(LinkProtocol.Command.Hello, ReadOnlySpan<byte>.Empty, idempotent: true).Payload);

    /// <summary>Reads every stored preset record, in the box's file order.</summary>
    public List<byte[]> DownloadBank(DeviceInfo info, IProgress<LinkProgress>? progress = null, CancellationToken cancel = default)
    {
        var records = new List<byte[]>(info.PresetCount);
        for (int i = 0; i < info.PresetCount; i++)
        {
            cancel.ThrowIfCancellationRequested();
            progress?.Report(new LinkProgress("Reading presets from the Pico", i, info.PresetCount));
            var index = new byte[2];
            BinaryPrimitives.WriteUInt16LittleEndian(index, (ushort)i);
            records.Add(CheckRecord(Exchange(LinkProtocol.Command.BankRead, index, idempotent: true).Payload));
        }
        progress?.Report(new LinkProgress("Reading presets from the Pico", info.PresetCount, info.PresetCount));
        return records;
    }

    /// <summary>
    /// Replaces the box's whole user bank with these records. The box keeps its old bank until the final
    /// commit succeeds. Records are validated by the box as they arrive; a refusal throws
    /// <see cref="DeviceRejectedException"/> and the old bank stays.
    /// </summary>
    /// <param name="names">Preset names in the same order, used only to make error messages read well.</param>
    public void UploadBank(IReadOnlyList<byte[]> records, IReadOnlyList<string>? names = null,
        IProgress<LinkProgress>? progress = null, CancellationToken cancel = default)
    {
        const string step = "Sending presets to the Pico";
        var begun = false;
        try
        {
            var count = new byte[2];
            BinaryPrimitives.WriteUInt16LittleEndian(count, (ushort)records.Count);
            progress?.Report(new LinkProgress(step, 0, records.Count));
            Exchange(LinkProtocol.Command.BankBegin, count, idempotent: false);
            begun = true;
            for (int i = 0; i < records.Count; i++)
            {
                cancel.ThrowIfCancellationRequested();
                try
                {
                    Exchange(LinkProtocol.Command.BankPut, records[i], idempotent: false, presetName: names?[i]);
                }
                catch (DeviceRejectedException)
                {
                    throw;
                }
                progress?.Report(new LinkProgress(step, i + 1, records.Count));
            }
            Exchange(LinkProtocol.Command.BankCommit, ReadOnlySpan<byte>.Empty, idempotent: false);
            begun = false; // committed: nothing left to abort
        }
        catch
        {
            if (begun) TryAbort();
            throw;
        }
    }

    /// <summary>Throws away a half-sent bank on the box (harmless if none is open).</summary>
    public void Abort() => Exchange(LinkProtocol.Command.BankAbort, ReadOnlySpan<byte>.Empty, idempotent: true);

    private void TryAbort()
    {
        try { Abort(); }
        catch (LinkException) { /* the box abandons a silent upload by itself after a few seconds */ }
    }

    /// <summary>Plays a record on voice 0-3 on the box, as if the player had loaded and tweaked it there.</summary>
    public void Audition(int voice, byte[] record, string? presetName = null)
    {
        if (voice is < 0 or > 3) throw new ArgumentOutOfRangeException(nameof(voice));
        var payload = new byte[1 + record.Length];
        payload[0] = (byte)voice;
        record.CopyTo(payload, 1);
        Exchange(LinkProtocol.Command.Audition, payload, idempotent: true, presetName: presetName);
    }

    /// <summary>A factory preset as the box has it (for comparing with this editor's built-in copy).</summary>
    public byte[] ReadFactory(int index) =>
        CheckRecord(Exchange(LinkProtocol.Command.FactoryRead, new[] { (byte)index }, idempotent: true).Payload);

    /// <summary>Captures the sound a voice is playing right now, including tweaks made on the box.</summary>
    public byte[] ReadVoice(int voice)
    {
        if (voice is < 0 or > 3) throw new ArgumentOutOfRangeException(nameof(voice));
        return CheckRecord(Exchange(LinkProtocol.Command.VoiceRead, new[] { (byte)voice }, idempotent: true).Payload);
    }

    private byte[] CheckRecord(byte[] payload) =>
        payload.Length == _schema.RecordSize
            ? payload
            : throw new LinkException($"The Pico sent a record of {payload.Length} bytes instead of {_schema.RecordSize}; its firmware does not match this editor.");

    // ---- one request / reply ---------------------------------------------------------------

    private LinkFrame Exchange(byte command, ReadOnlySpan<byte> payload, bool idempotent, string? presetName = null)
    {
        // Idempotent requests (reads, hello, audition) are simply repeated if a reply goes missing.
        // A bank record is not: sending it twice would be refused as a duplicate, so it fails instead.
        var attempts = idempotent ? 3 : 1;
        for (int attempt = 1; ; attempt++)
        {
            var seq = ++_seq;
            if (seq == 0) seq = ++_seq;
            DiscardStaleInput();
            _transport.Write(LinkProtocol.Encode(command, seq, payload));
            var reply = WaitForReply(command, seq);
            if (reply is not null)
            {
                if (reply.Type == LinkProtocol.ErrorType)
                {
                    var p = reply.Payload;
                    if (p.Length != 4) throw new LinkException("The Pico sent a damaged error report.");
                    throw DeviceRejectedException.From(p[0], p[1], p[2], p[3], _schema, presetName);
                }
                return reply;
            }
            if (attempt >= attempts)
                throw new LinkTimeoutException(
                    "The Pico did not answer. Check the cable, close any serial monitor that has the port open, and make sure the Pico is running firmware with the preset link.");
        }
    }

    private void DiscardStaleInput()
    {
        var scratch = new byte[256];
        while (_transport.Read(scratch, 0) > 0) { }
        _parser.Reset();
    }

    private LinkFrame? WaitForReply(byte command, byte seq)
    {
        var deadline = Environment.TickCount64 + _replyTimeoutMs;
        var buffer = new byte[256];
        var text = new System.Text.StringBuilder();
        while (true)
        {
            var remaining = (int)(deadline - Environment.TickCount64);
            if (remaining <= 0) return null;
            var n = _transport.Read(buffer, remaining);
            if (n < 0) throw new LinkClosedException("The connection to the Pico was lost.");
            for (int i = 0; i < n; i++)
            {
                switch (_parser.Push(buffer[i], Environment.TickCount64))
                {
                    case FrameParser.PushResult.Console:
                        if (ConsoleText is not null)
                        {
                            if (buffer[i] == '\n') { ConsoleText(text.ToString()); text.Clear(); }
                            else if (buffer[i] != '\r' && text.Length < 400) text.Append((char)buffer[i]);
                        }
                        break;
                    case FrameParser.PushResult.Frame:
                    {
                        var f = _parser.Frame!;
                        if (f.Seq != seq) break; // an answer to something older
                        if (f.Type == (byte)(command | LinkProtocol.ReplyBit) || f.Type == LinkProtocol.ErrorType)
                            return f;
                        break;
                    }
                }
            }
        }
    }
}
