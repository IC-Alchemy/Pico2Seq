using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Pico2Seq_App.Native;
using Pico2Seq_App.ViewModels;
using System.Runtime.InteropServices.WindowsRuntime;

namespace Pico2Seq_App;

/// <summary>
/// The virtual Pico2Seq surface: 32-pad grid with LED-matrix glow and the
/// 128x64 OLED canvas. All musical state lives in the native engine; this
/// page pushes touch events and polls display snapshots at ~30 Hz.
/// </summary>
public sealed partial class MainPage : Page
{
    // Firmware matrix decode (src/matrix/Matrix.cpp): pad (row,col) closes
    // between MPR121 electrodes RowInputs[row] and ColInputs[col].
    private static readonly byte[] RowInputs = { 3, 2, 1, 0 };
    private static readonly byte[] ColInputs = { 4, 5, 6, 7, 8, 9, 10, 11 };

    // Display-only copies of the firmware's static tables
    // (scales.cpp, ShuffleTemplates.h). The engine remains the truth.
    private static readonly string[] ScaleNames =
    {
        "Ionian Major", "Dorian", "Phrygian", "Lydian", "Mixolydian",
        "Aeolian Minor", "Locrian", "Pentatonic Minor", "Phrygian Dominant",
        "Lydian Dominant", "Harmonic Minor", "Wholetone", "Chromatic"
    };
    private static readonly string[] ShuffleNames =
    {
        "No Shuffle", "Teeny Swing", "Lil' Swing", "Neg' Swing", "CornBread",
        "Swing 55%", "Swing 56%", "Swing 57%", "Swing 60%", "Big Swang 60%",
        "Phatty Swang", "Big Swang 62%", "Humanize 1", "Humanize 2",
        "Hip-Hop", "Funk Groove"
    };

    public MainPageViewModel ViewModel { get; } = new();

    private readonly Button[] _pads = new Button[32];
    private ushort _touchBits;

    private readonly byte[] _ledBytes = new byte[EngineApi.LedBytes];
    private readonly byte[] _oledBytes = new byte[EngineApi.OledBytes];
    private readonly byte[] _bgra = new byte[128 * 64 * 4];
    private readonly WriteableBitmap _oledBitmap = new(128, 64);
    private ulong _lastLedSerial = ulong.MaxValue;
    private ulong _lastOledSerial = ulong.MaxValue;
    private DispatcherQueueTimer? _pollTimer;

    public MainPage()
    {
        InitializeComponent();
        OledImage.Source = _oledBitmap;
        BuildPadGrid();
        Loaded += OnLoaded;
        Unloaded += OnUnloaded;
    }

    // x:Bind function bindings for the transport glyph/label.
    public string GlyphFor(bool running) => running ? "\uE769" : "\uE768";
    public string LabelFor(bool running) => running ? "Stop" : "Play";

    private void BuildPadGrid()
    {
        for (int row = 0; row < 4; ++row)
            PadGrid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(52) });
        for (int col = 0; col < 8; ++col)
            PadGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(52) });

        for (int pad = 0; pad < 32; ++pad)
        {
            var button = new Button
            {
                CornerRadius = new CornerRadius(6),
                Padding = new Thickness(0),
                Content = new TextBlock
                {
                    Text = (pad % 16 + 1).ToString(),
                    FontSize = 11,
                    HorizontalAlignment = HorizontalAlignment.Center,
                    VerticalAlignment = VerticalAlignment.Center,
                },
            };
            Grid.SetRow(button, pad / 8);
            Grid.SetColumn(button, pad % 8);
            button.PointerPressed += OnPadPressed;
            button.PointerReleased += OnPadReleased;
            button.PointerCanceled += OnPadReleased;
            button.PointerCaptureLost += OnPadReleased;
            PadGrid.Children.Add(button);
            _pads[pad] = button;
        }
    }

    private void OnPadPressed(object sender, PointerRoutedEventArgs e)
    {
        var button = (Button)sender;
        button.CapturePointer(e.Pointer);
        int pad = Array.IndexOf(_pads, button);
        _touchBits |= ElectrodeBitsFor(pad);
        EngineApi.p2s_push_touch(_touchBits);
    }

    private void OnPadReleased(object sender, PointerRoutedEventArgs e)
    {
        var button = (Button)sender;
        button.ReleasePointerCapture(e.Pointer);
        int pad = Array.IndexOf(_pads, button);
        if (pad < 0)
            return;
        _touchBits &= (ushort)~ElectrodeBitsFor(pad);
        EngineApi.p2s_push_touch(_touchBits);
    }

    private static ushort ElectrodeBitsFor(int pad)
    {
        int row = pad / 8, col = pad % 8;
        return (ushort)((1 << RowInputs[row]) | (1 << ColInputs[col]));
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
            var options = new EngineApi.InitOptions
            {
                StorageDir = IntPtr.Zero,
                StartAudioDevice = true,
                StartControlThread = false, // TEMP bisect
            };
            int rc = EngineApi.p2s_init(options);
            ViewModel.StatusLine = rc == 0 ? "engine running" : "engine FAILED to start";

            _pollTimer = DispatcherQueue.GetForCurrentThread().CreateTimer();
            _pollTimer.Interval = TimeSpan.FromMilliseconds(33);
            _pollTimer.Tick += (_, _) => PollEngine();
            _pollTimer.Start();
        }
        catch (Exception ex)
        {
            // Surface engine-start failures instead of failing fast: the
            // native boundary turns load/link problems into exceptions here.
            ViewModel.StatusLine = "engine error: " + ex.Message;
        }
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        _pollTimer?.Stop();
        EngineApi.p2s_shutdown();
    }

    private void PollEngine()
    {
        var status = new EngineApi.Status();
        EngineApi.p2s_get_status(ref status);

        if (status.LedFrames != _lastLedSerial)
        {
            _lastLedSerial = EngineApi.p2s_poll_leds(_ledBytes);
            UpdatePadGlow();
        }
        if (status.OledFrames != _lastOledSerial)
        {
            _lastOledSerial = EngineApi.p2s_poll_oled(_oledBytes);
            RenderOled();
        }

        int scale = Math.Clamp(status.CurrentScale, 0, ScaleNames.Length - 1);
        int shuffle = Math.Clamp(status.CurrentShuffle, 0, ShuffleNames.Length - 1);
        ViewModel.UpdateFromStatus(status, ScaleNames[scale], ShuffleNames[shuffle]);
    }

    private void UpdatePadGlow()
    {
        for (int pad = 0; pad < 32; ++pad)
        {
            int i = pad * 3; // LED index == pad grid index until proven otherwise
            byte r = _ledBytes[i], g = _ledBytes[i + 1], b = _ledBytes[i + 2];
            // Glow = LED color; the faint blue base keeps pads discoverable
            // when their LED is off.
            var brush = r + g + b == 0
                ? GetOrCreateBrush(16, 16, 28)
                : GetOrCreateBrush(r, g, b);
            _pads[pad].Background = brush;
        }
    }

    private Brush GetOrCreateBrush(byte r, byte g, byte b)
    {
        uint key = (uint)(r << 16 | g << 8 | b);
        if (_brushCache.TryGetValue(key, out var brush))
            return brush;
        brush = new SolidColorBrush(Windows.UI.Color.FromArgb(255, r, g, b));
        _brushCache[key] = brush;
        return brush;
    }

    private readonly Dictionary<uint, SolidColorBrush> _brushCache = new();

    private void RenderOled()
    {
        // 1bpp page buffer (x + (y/8)*128, bit y%8) -> BGRA8 bitmap.
        for (int y = 0; y < 64; ++y)
        {
            int pageRow = (y / 8) * 128;
            byte bit = (byte)(1 << (y & 7));
            int dstRow = y * 128 * 4;
            for (int x = 0; x < 128; ++x)
            {
                bool on = (_oledBytes[pageRow + x] & bit) != 0;
                int dst = dstRow + x * 4;
                if (on)
                {
                    _bgra[dst] = 235;
                    _bgra[dst + 1] = 240;
                    _bgra[dst + 2] = 245;
                }
                else
                {
                    _bgra[dst] = 16;
                    _bgra[dst + 1] = 14;
                    _bgra[dst + 2] = 18;
                }
                _bgra[dst + 3] = 255;
            }
        }
        using var stream = _oledBitmap.PixelBuffer.AsStream();
        stream.Write(_bgra, 0, _bgra.Length);
        _oledBitmap.Invalidate();
    }
}
