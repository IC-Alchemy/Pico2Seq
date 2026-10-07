using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using PresetStudio.Editing;
using PresetStudio.Model;

namespace PresetStudio.App.Views;

/// <summary>A palette of colours that look good on the box's LEDs, plus red/green/blue sliders.</summary>
public partial class ColorPickerControl : UserControl
{
    // Fully saturated hues first (they read best on WS2812 LEDs), then a few softer ones, then whites.
    private static readonly string[] PaletteColors =
    {
        "#FF1E1E", "#FF5A00", "#FF9A00", "#FFD400", "#B6FF00", "#2BE04A", "#00E0A0", "#00D8FF",
        "#0096FF", "#2F4BFF", "#6A3DFF", "#B03BFF", "#FF3BE0", "#FF3B8A", "#FF6F6F", "#FFB27A",
        "#FFE27A", "#B6F27A", "#7AF2C8", "#7AD8FF", "#7A9BFF", "#C07AFF", "#FFFFFF", "#FFC890",
    };

    public ColorPickerControl()
    {
        InitializeComponent();
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        if (Palette.Children.Count > 0) return;
        foreach (var hex in PaletteColors)
        {
            var rgb = Rgb.ParseHex(hex);
            var button = new Button
            {
                Width = 24,
                Height = 24,
                Margin = new Thickness(2),
                Background = new SolidColorBrush(Color.FromRgb(rgb.R, rgb.G, rgb.B)),
                BorderBrush = new SolidColorBrush(Color.FromArgb(0x55, 0, 0, 0)),
                ToolTip = hex,
                Tag = hex,
            };
            button.Click += (_, _) =>
            {
                if (DataContext is PresetEditor editor) editor.ColorHex = hex;
            };
            Palette.Children.Add(button);
        }
    }
}
