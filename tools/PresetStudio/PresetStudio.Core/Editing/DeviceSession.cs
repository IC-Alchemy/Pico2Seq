using PresetStudio.Link;
using PresetStudio.Model;

namespace PresetStudio.Editing;

/// <summary>
/// The connection to the box as the app sees it: find it, connect, read and send the bank, audition a sound
/// on a voice, grab the sound a voice is playing. Everything that touches the cable happens one at a time,
/// off the UI thread; a lost cable simply turns the session back into "not connected".
/// </summary>
public sealed class DeviceSession : ObservableObject, IDisposable
{
    private readonly StudioContext _studio;
    private readonly Func<IReadOnlyList<string>> _listPorts;
    private readonly Func<string, ILinkTransport> _open;
    private readonly BankSync _sync;
    private readonly object _gate = new();
    private readonly AuditionPump _pump;
    private DeviceLink? _link;
    private DeviceInfo? _info;
    private string? _port;
    private string _status = "Not connected";
    private int _auditionVoice;

    public DeviceSession(StudioContext studio, Func<IReadOnlyList<string>> listPorts, Func<string, ILinkTransport> open)
    {
        _studio = studio;
        _listPorts = listPorts;
        _open = open;
        _sync = new BankSync(studio);
        _pump = new AuditionPump(SendAudition);
        _pump.Failed += e => Faulted?.Invoke(e);
    }

    /// <summary>A session over the computer's real serial ports.</summary>
    public static DeviceSession ForSerialPorts(StudioContext studio) =>
        new(studio, SerialPortTransport.ListPorts, port => SerialPortTransport.Open(port));

    public bool IsConnected => _link is not null;
    public string? PortName => _port;
    public DeviceInfo? Info => _info;

    public string Status
    {
        get => _status;
        private set => Set(ref _status, value);
    }

    /// <summary>The box's log text (everything it prints that is not part of the preset link).</summary>
    public event Action<string>? ConsoleText;
    /// <summary>An operation failed in the background (a live audition, for example).</summary>
    public event Action<Exception>? Faulted;

    public IReadOnlyList<string> ListPorts() => _listPorts();

    // ---- connecting -------------------------------------------------------------------------

    public Task ConnectAsync(string port, CancellationToken cancel = default) => Task.Run(() =>
    {
        Disconnect();
        var transport = _open(port);
        var link = new DeviceLink(transport, _studio.Schema);
        try
        {
            link.ConsoleText += t => ConsoleText?.Invoke(t);
            var info = link.Hello(); // throws if this is not a Pico2Seq, or its firmware does not fit
            lock (_gate)
            {
                _link = link;
                _info = info;
                _port = port;
            }
            Status = Describe(info, port);
            Raise(nameof(IsConnected));
            Raise(nameof(Info));
            Raise(nameof(PortName));
        }
        catch
        {
            link.Dispose();
            throw;
        }
    }, cancel);

    /// <summary>Tries each serial port in turn. Returns the port that answered, or null.</summary>
    public async Task<string?> AutoConnectAsync(CancellationToken cancel = default)
    {
        Exception? incompatible = null;
        foreach (var port in ListPorts())
        {
            cancel.ThrowIfCancellationRequested();
            Status = $"Looking for the Pico on {port}...";
            try
            {
                await ConnectAsync(port, cancel).ConfigureAwait(false);
                return port;
            }
            catch (IncompatibleDeviceException e)
            {
                incompatible = e; // it IS a Pico2Seq, just one this editor cannot use: say so rather than "not found"
            }
            catch (Exception e) when (e is LinkException or IOException or UnauthorizedAccessException or InvalidOperationException)
            {
                // not it (or busy); try the next one
            }
        }
        if (incompatible is not null)
        {
            Status = incompatible.Message;
            throw incompatible;
        }
        Status = "No Pico2Seq found. Check the USB cable and that the Pico is powered.";
        return null;
    }

    public void Disconnect()
    {
        DeviceLink? link;
        lock (_gate)
        {
            link = _link;
            _link = null;
            _info = null;
            _port = null;
        }
        if (link is null) return;
        _pump.WaitIdle(500);
        link.Dispose();
        Status = "Not connected";
        Raise(nameof(IsConnected));
        Raise(nameof(Info));
        Raise(nameof(PortName));
    }

    private static string Describe(DeviceInfo info, string port) =>
        $"Connected on {port}: {info.PresetCount} user preset{(info.PresetCount == 1 ? "" : "s")} on the Pico, " +
        $"{info.FreeBytes / 1024.0:0.#} KB free";

    /// <summary>Runs one link operation with exclusive use of the cable; a lost cable disconnects.</summary>
    private T WithLink<T>(Func<DeviceLink, T> action)
    {
        lock (_gate)
        {
            var link = _link ?? throw new LinkClosedException("Not connected to a Pico.");
            try
            {
                return action(link);
            }
            catch (LinkClosedException)
            {
                _link = null;
                _info = null;
                _port = null;
                link.Dispose();
                Status = "Connection lost";
                Raise(nameof(IsConnected));
                Raise(nameof(Info));
                Raise(nameof(PortName));
                throw;
            }
        }
    }

    // ---- the bank ---------------------------------------------------------------------------

    public Task<(DeviceInfo Info, List<UserPreset> Presets)> PullAsync(IProgress<LinkProgress>? progress = null, CancellationToken cancel = default) =>
        Task.Run(() =>
        {
            var result = WithLink(link => _sync.Pull(link, progress, cancel));
            _info = result.Info;
            Status = Describe(result.Info, _port ?? "");
            return result;
        }, cancel);

    /// <summary>What sending <paramref name="library"/> would change on the box.</summary>
    public Task<(DeviceInfo Info, BankDiff Diff)> PreviewAsync(IReadOnlyList<UserPreset> library, CancellationToken cancel = default) =>
        Task.Run(() =>
        {
            var (info, onDevice) = WithLink(link => _sync.Pull(link, null, cancel));
            _info = info;
            return (info, _sync.Compare(library, onDevice));
        }, cancel);

    public Task PushAsync(IReadOnlyList<UserPreset> library, IProgress<LinkProgress>? progress = null, CancellationToken cancel = default) =>
        Task.Run(() =>
        {
            WithLink(link =>
            {
                _sync.Push(link, library, progress, cancel);
                return 0;
            });
            var info = WithLink(link => link.Hello());
            _info = info;
            Status = Describe(info, _port ?? "");
            Raise(nameof(Info));
        }, cancel);

    // ---- sounds -----------------------------------------------------------------------------

    /// <summary>Which voice (0-3) live audition plays on.</summary>
    public int AuditionVoice
    {
        get => _auditionVoice;
        set => Set(ref _auditionVoice, Math.Clamp(value, 0, 3));
    }

    /// <summary>Plays the preset on the chosen voice now.</summary>
    public Task AuditionAsync(UserPreset preset, CancellationToken cancel = default)
    {
        var record = _studio.Codec.Encode(preset);
        var voice = _auditionVoice;
        return Task.Run(() => WithLink(link =>
        {
            link.Audition(voice, record, preset.Name);
            return 0;
        }), cancel);
    }

    /// <summary>
    /// Plays the preset on the chosen voice as soon as the cable is free, dropping older requests - for following a
    /// slider. Does nothing when not connected.
    /// </summary>
    public void QueueAudition(UserPreset preset)
    {
        if (!IsConnected) return;
        _pump.Submit(_studio.Codec.Encode(preset), preset.Name);
    }

    private void SendAudition(byte[] record, string name)
    {
        if (!IsConnected) return;
        var voice = _auditionVoice;
        WithLink(link =>
        {
            link.Audition(voice, record, name);
            return 0;
        });
    }

    /// <summary>The sound voice 1-4 is playing now, including tweaks made on the box, as a new unplaced preset.</summary>
    public Task<UserPreset> GrabVoiceAsync(int voice, CancellationToken cancel = default) => Task.Run(() =>
    {
        var record = WithLink(link => link.ReadVoice(voice));
        return _studio.Codec.Decode(record);
    }, cancel);

    /// <summary>Waits for queued live-audition sends to finish (before reading or sending a bank).</summary>
    public bool WaitForAuditionIdle(int timeoutMs = 2000) => _pump.WaitIdle(timeoutMs);

    public void Dispose()
    {
        Disconnect();
        _pump.Dispose();
    }
}
