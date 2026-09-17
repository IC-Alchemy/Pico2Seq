using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using CommunityToolkit.Mvvm.ComponentModel;

namespace Pico2Seq_App.ViewModels;

/// <summary>One pad of the 4x8 touch matrix. Touch callbacks are injected so
/// the VM stays engine-agnostic; MainPage owns the electrode-bit encoding.</summary>
public sealed partial class PadCellViewModel : ObservableObject
{
    private readonly Action<int, bool> _touch;

    public PadCellViewModel(int index, Action<int, bool> touch)
    {
        Index = index;
        _touch = touch;
    }

    public int Index { get; }
    public int Number => Index % 16 + 1;
    public string Name => $"Pad {Number}, bank {(Index < 16 ? "A" : "B")}";
    public string AutomationId => $"Pad{Index + 1}";

    /// <summary>LED glow from the poll loop; null leaves the theme idle fill visible.</summary>
    [ObservableProperty]
    public partial Brush? LedBrush { get; set; }

    public void OnPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        ((FrameworkElement)sender).CapturePointer(e.Pointer);
        _touch(Index, true);
    }

    public void OnPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        ((FrameworkElement)sender).ReleasePointerCapture(e.Pointer);
        _touch(Index, false);
    }

    // Keyboard activation = one full matrix close/open cycle, identical to a tap.
    public void OnKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key is Windows.System.VirtualKey.Space or Windows.System.VirtualKey.Enter)
        {
            _touch(Index, true);
            _touch(Index, false);
            e.Handled = true;
        }
    }
}
