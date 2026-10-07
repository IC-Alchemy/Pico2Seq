using PresetStudio.Codec;

namespace PresetStudio.Link;

/// <summary>Wire constants and framing, byte for byte what <c>src/presetlink/PresetLinkProtocol.h</c> defines.</summary>
public static class LinkProtocol
{
    public const byte Sync0 = 0xA5;
    public const byte Sync1 = 0x5A;
    public const byte ProtocolVersion = 1;
    public const int MaxPayload = 288;
    public const int FrameOverhead = 10;
    public const byte ReplyBit = 0x80;
    public const byte ErrorType = 0xFF;
    public const int ByteTimeoutMs = 250;

    public static class Command
    {
        public const byte Hello = 0x01;
        public const byte BankBegin = 0x02;
        public const byte BankPut = 0x03;
        public const byte BankCommit = 0x04;
        public const byte BankAbort = 0x05;
        public const byte BankRead = 0x06;
        public const byte Audition = 0x07;
        public const byte FactoryRead = 0x08;
        public const byte VoiceRead = 0x09;
    }

    public enum ErrorCode : byte
    {
        UnknownCommand = 1, BadPayload, BadState, Busy, NoSpace, Storage,
        InvalidRecord, OutOfRange, SlotTaken, CountMismatch,
    }

    /// <summary>Why the box refused a record (<c>usercodec::Problem</c>).</summary>
    public enum RecordProblem : byte
    {
        None, BadName, BadPlace, BadBase, BadReserved, BadField, EngineNeedsRecipe,
    }

    /// <summary>sync, type, seq, length, payload, CRC-32 of type..payload.</summary>
    public static byte[] Encode(byte type, byte seq, ReadOnlySpan<byte> payload)
    {
        if (payload.Length > MaxPayload) throw new ArgumentException("payload too large for one frame", nameof(payload));
        var frame = new byte[FrameOverhead + payload.Length];
        frame[0] = Sync0;
        frame[1] = Sync1;
        frame[2] = type;
        frame[3] = seq;
        frame[4] = (byte)payload.Length;
        frame[5] = (byte)(payload.Length >> 8);
        payload.CopyTo(frame.AsSpan(6));
        var crc = Crc32.Compute(frame.AsSpan(2, 4 + payload.Length));
        for (int i = 0; i < 4; i++) frame[6 + payload.Length + i] = (byte)(crc >> (8 * i));
        return frame;
    }
}

public sealed record LinkFrame(byte Type, byte Seq, byte[] Payload);

/// <summary>
/// Pulls frames out of a byte stream that also carries the firmware's log text. Bytes that belong to no
/// frame come back as <see cref="PushResult.Console"/> so the caller can show or ignore them.
/// </summary>
public sealed class FrameParser
{
    public enum PushResult { Console, Pending, Frame, Dropped }
    private enum State { Sync0, Sync1, Header, Payload, Crc }

    private State _state = State.Sync0;
    private readonly byte[] _header = new byte[4];
    private readonly byte[] _crc = new byte[4];
    private byte[] _payload = Array.Empty<byte>();
    private int _have;
    private int _length;
    private long _lastByteMs;

    public LinkFrame? Frame { get; private set; }

    public bool InFrame => _state != State.Sync0;

    public void Reset()
    {
        _state = State.Sync0;
        _have = 0;
    }

    public PushResult Push(byte b, long nowMs)
    {
        if (_state != State.Sync0 && nowMs - _lastByteMs > LinkProtocol.ByteTimeoutMs)
            Reset();
        _lastByteMs = nowMs;

        switch (_state)
        {
            case State.Sync0:
                if (b != LinkProtocol.Sync0) return PushResult.Console;
                _state = State.Sync1;
                return PushResult.Pending;

            case State.Sync1:
                if (b == LinkProtocol.Sync1)
                {
                    _state = State.Header;
                    _have = 0;
                    return PushResult.Pending;
                }
                if (b == LinkProtocol.Sync0) return PushResult.Pending;
                _state = State.Sync0;
                return PushResult.Console;

            case State.Header:
                _header[_have++] = b;
                if (_have < 4) return PushResult.Pending;
                _length = _header[2] | (_header[3] << 8);
                _have = 0;
                if (_length > LinkProtocol.MaxPayload)
                {
                    Reset();
                    return PushResult.Dropped;
                }
                _payload = new byte[_length];
                _state = _length > 0 ? State.Payload : State.Crc;
                return PushResult.Pending;

            case State.Payload:
                _payload[_have++] = b;
                if (_have < _length) return PushResult.Pending;
                _have = 0;
                _state = State.Crc;
                return PushResult.Pending;

            default: // Crc
            {
                _crc[_have++] = b;
                if (_have < 4) return PushResult.Pending;
                var expected = (uint)(_crc[0] | (_crc[1] << 8) | (_crc[2] << 16) | (_crc[3] << 24));
                var actual = Crc32.Update(Crc32.Compute(_header), _payload);
                Reset();
                if (actual != expected) return PushResult.Dropped;
                Frame = new LinkFrame(_header[0], _header[1], _payload);
                return PushResult.Frame;
            }
        }
    }
}
