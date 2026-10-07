using System.IO.Ports;

namespace PresetStudio.Link;

/// <summary>A byte pipe to the box. Reads time out instead of blocking forever.</summary>
public interface ILinkTransport : IDisposable
{
    string Description { get; }
    void Write(ReadOnlySpan<byte> data);
    /// <summary>Bytes read into <paramref name="buffer"/>; 0 if nothing arrived in time; -1 if the pipe closed.</summary>
    int Read(Span<byte> buffer, int timeoutMs);
}

/// <summary>
/// A transport over two streams (a serial port's stream, or a child process's pipes in the tests). A
/// background thread keeps draining the input so the box never waits on us, and reads can time out.
/// </summary>
public class StreamTransport : ILinkTransport
{
    private readonly Stream _input;
    private readonly Stream _output;
    private readonly Queue<byte> _received = new();
    private readonly object _gate = new();
    private readonly Thread _reader;
    private bool _closed;
    private bool _disposed;

    public StreamTransport(Stream input, Stream output, string description)
    {
        _input = input;
        _output = output;
        Description = description;
        _reader = new Thread(ReadLoop) { IsBackground = true, Name = "PresetStudio link reader" };
        _reader.Start();
    }

    public string Description { get; }

    private void ReadLoop()
    {
        var buffer = new byte[512];
        try
        {
            while (true)
            {
                var n = _input.Read(buffer, 0, buffer.Length);
                if (n <= 0) break;
                lock (_gate)
                {
                    for (int i = 0; i < n; i++) _received.Enqueue(buffer[i]);
                    Monitor.PulseAll(_gate);
                }
            }
        }
        catch (Exception)
        {
            // Port unplugged or closed while reading: the same as end of stream.
        }
        finally
        {
            lock (_gate)
            {
                _closed = true;
                Monitor.PulseAll(_gate);
            }
        }
    }

    public void Write(ReadOnlySpan<byte> data)
    {
        if (_disposed) throw new ObjectDisposedException(nameof(StreamTransport));
        try
        {
            _output.Write(data);
            _output.Flush();
        }
        catch (Exception e) when (e is IOException or InvalidOperationException or UnauthorizedAccessException or TimeoutException)
        {
            throw new LinkClosedException("The connection to the Pico was lost while sending.", e);
        }
    }

    public int Read(Span<byte> buffer, int timeoutMs)
    {
        lock (_gate)
        {
            var deadline = Environment.TickCount64 + Math.Max(0, timeoutMs);
            while (_received.Count == 0)
            {
                if (_closed) return -1;
                var remaining = deadline - Environment.TickCount64;
                if (remaining <= 0 || !Monitor.Wait(_gate, (int)remaining)) return 0;
            }
            var count = Math.Min(buffer.Length, _received.Count);
            for (int i = 0; i < count; i++) buffer[i] = _received.Dequeue();
            return count;
        }
    }

    public virtual void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        try { _output.Dispose(); } catch { /* already gone */ }
        try { _input.Dispose(); } catch { /* already gone */ }
        _reader.Join(500);
    }
}

/// <summary>The Pico's USB serial port. It enumerates as a standard COM port; no driver is needed on Windows 10 and later.</summary>
public sealed class SerialPortTransport : StreamTransport
{
    private readonly SerialPort _port;

    private SerialPortTransport(SerialPort port)
        : base(port.BaseStream, port.BaseStream, port.PortName)
    {
        _port = port;
    }

    /// <summary>COM ports present right now, in natural order (COM3 before COM12).</summary>
    public static IReadOnlyList<string> ListPorts() =>
        SerialPort.GetPortNames()
            .OrderBy(n => n.Length > 3 && int.TryParse(n[3..], out var i) ? i : int.MaxValue)
            .ThenBy(n => n, StringComparer.OrdinalIgnoreCase)
            .ToList();

    public static SerialPortTransport Open(string portName)
    {
        // DTR must be raised: the Pico's USB serial only talks while a terminal is "connected".
        var port = new SerialPort(portName, 115200, Parity.None, 8, StopBits.One)
        {
            DtrEnable = true,
            RtsEnable = true,
            ReadTimeout = SerialPort.InfiniteTimeout,
            WriteTimeout = 3000,
        };
        try
        {
            port.Open();
        }
        catch (Exception e) when (e is UnauthorizedAccessException or IOException or InvalidOperationException or ArgumentException)
        {
            port.Dispose();
            throw new LinkClosedException(
                e is UnauthorizedAccessException
                    ? $"{portName} is in use by another program (a serial monitor or the Arduino IDE?). Close it and try again."
                    : $"Could not open {portName}: {e.Message}", e);
        }
        return new SerialPortTransport(port);
    }

    public override void Dispose()
    {
        try { _port.Close(); } catch { /* unplugged */ }
        base.Dispose();
        _port.Dispose();
    }
}
