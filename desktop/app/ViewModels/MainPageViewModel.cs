using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Pico2Seq_App.Native;
using System.Collections.ObjectModel;

namespace Pico2Seq_App.ViewModels;

/// <summary>
/// Status surface of the virtual Pico2Seq. Values are refreshed from the
/// native engine by MainPage's poll timer; commands drive the transport.
/// </summary>
public partial class MainPageViewModel : ObservableObject
{
    /// <summary>Row-major pads of the 4x8 touch matrix; MainPage fills this
    /// once before the page's XAML binds it.</summary>
    public ObservableCollection<PadCellViewModel> Pads { get; } = new();

    [ObservableProperty]
    public partial bool TransportRunning { get; set; }

    [ObservableProperty]
    public partial string TempoText { get; set; } = "— BPM";

    [ObservableProperty]
    public partial string StatusLine { get; set; } = "engine starting";

    [ObservableProperty]
    public partial string VoiceText { get; set; } = "Voice 1";

    [ObservableProperty]
    public partial string ScaleText { get; set; } = "";

    [ObservableProperty]
    public partial string ShuffleText { get; set; } = "";

    public void UpdateFromStatus(in EngineApi.Status status, string scaleName, string shuffleName)
    {
        TransportRunning = status.TransportRunning != 0;
        TempoText = $"{status.TempoBpm:0} BPM";
        VoiceText = $"Voice {status.SelectedVoice + 1}";
        ScaleText = scaleName;
        ShuffleText = shuffleName;
    }

    [RelayCommand]
    private void ToggleTransport()
    {
        TransportRunning = !TransportRunning;   // immediate feedback; poll reconciles
        if (TransportRunning)
            EngineApi.p2s_transport_start();
        else
            EngineApi.p2s_transport_stop();
    }
}
