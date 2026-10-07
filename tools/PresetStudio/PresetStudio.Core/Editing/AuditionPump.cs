namespace PresetStudio.Editing;

/// <summary>
/// Streams "play this sound on the voice" to the box while a slider is being dragged, without flooding the cable:
/// only the latest request is kept, and sends are spaced out. The first change goes out at once, so the sound
/// follows the hand; the final position is always sent.
/// </summary>
public sealed class AuditionPump : IDisposable
{
    private readonly Action<byte[], string> _send;
    private readonly int _minIntervalMs;
    private readonly object _gate = new();
    private readonly CancellationTokenSource _stop = new();
    private (byte[] Record, string Name)? _pending;
    private long _lastSentTick;
    private bool _running;

    /// <param name="send">Sends one record to the box. Called from a worker thread, never concurrently.</param>
    /// <param name="minIntervalMs">Shortest gap between two sends.</param>
    public AuditionPump(Action<byte[], string> send, int minIntervalMs = 80)
    {
        _send = send;
        _minIntervalMs = minIntervalMs;
    }

    /// <summary>A send failed (cable pulled, preset refused). The pump keeps running; the next submit tries again.</summary>
    public event Action<Exception>? Failed;

    public void Submit(byte[] record, string name)
    {
        lock (_gate)
        {
            _pending = (record, name);
            if (_running) return;
            _running = true;
        }
        Task.Run(Run);
    }

    private void Run()
    {
        while (!_stop.IsCancellationRequested)
        {
            (byte[] Record, string Name) item;
            lock (_gate)
            {
                if (_pending is not { } next) { _running = false; return; }
                item = next;
                _pending = null;
            }
            var wait = _lastSentTick + _minIntervalMs - Environment.TickCount64;
            if (wait > 0) Thread.Sleep((int)wait);
            // A newer request may have arrived while waiting: send that instead.
            lock (_gate)
            {
                if (_pending is { } newer) { item = newer; _pending = null; }
            }
            try
            {
                _send(item.Record, item.Name);
            }
            catch (Exception e)
            {
                Failed?.Invoke(e);
            }
            _lastSentTick = Environment.TickCount64;
        }
        lock (_gate) _running = false;
    }

    /// <summary>Waits until everything submitted so far has been sent (tests, and before disconnecting).</summary>
    public bool WaitIdle(int timeoutMs = 2000)
    {
        var deadline = Environment.TickCount64 + timeoutMs;
        while (Environment.TickCount64 < deadline)
        {
            lock (_gate)
            {
                if (!_running && _pending is null) return true;
            }
            Thread.Sleep(5);
        }
        return false;
    }

    public void Dispose() => _stop.Cancel();
}
