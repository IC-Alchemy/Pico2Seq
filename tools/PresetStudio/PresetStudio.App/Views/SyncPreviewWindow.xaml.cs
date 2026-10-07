using System.Windows;
using System.Windows.Media;
using PresetStudio.Editing;
using PresetStudio.Shell;

namespace PresetStudio.App.Views;

/// <summary>"Here is exactly what sending will do" - shown before anything is written to the box.</summary>
public partial class SyncPreviewWindow : Window
{
    private sealed record ChangeRow(string Symbol, Brush Brush, string Name, string Place, string Detail);

    public SyncPreviewWindow(SyncPreview preview)
    {
        InitializeComponent();
        var diff = preview.Diff;

        HeadlineText.Text = preview.PresetCount == 0
            ? $"Remove all user presets from the Pico on {preview.PortName}?"
            : $"Send {preview.PresetCount} preset{(preview.PresetCount == 1 ? "" : "s")} to the Pico on {preview.PortName}?";
        CountsText.Text = $"{diff.Added} new  ·  {diff.Changed} changed  ·  {diff.Removed} removed  ·  {diff.Unchanged} unchanged";

        var needed = preview.BytesNeeded / 1024.0;
        var free = preview.BytesFree / 1024.0;
        StorageText.Text = $"Storage: needs {needed:0.#} KB of the {free:0.#} KB free on the Pico.";
        StorageText.Foreground = (Brush)FindResource(preview.FitsInStorage ? "MutedTextBrush" : "ErrorBrush");
        if (!preview.FitsInStorage)
            StorageText.Text += " This probably will not fit; remove some presets or free space on the Pico.";

        if (preview.Warnings.Count > 0)
        {
            WarningBox.Visibility = Visibility.Visible;
            WarningText.Text = string.Join("\n", preview.Warnings.Take(6).Select(w => "• " + w));
        }

        var green = (Brush)FindResource("GoodBrush");
        var amber = (Brush)FindResource("ChangedBrush");
        var red = (Brush)FindResource("ErrorBrush");
        ChangeList.ItemsSource = diff.Changes.Select(c => new ChangeRow(
            c.Kind switch { BankChangeKind.Added => "+", BankChangeKind.Changed => "~", _ => "−" },
            c.Kind switch { BankChangeKind.Added => green, BankChangeKind.Changed => amber, _ => red },
            c.Preset.Name,
            $"  page {c.Preset.Page + 1}, pad {c.Preset.Pad}",
            c.Summary)).ToList();
        if (diff.Changes.Count == 0)
            ChangeList.ItemsSource = new[] { new ChangeRow("=", (Brush)FindResource("MutedTextBrush"), "No differences", "", "The Pico already has these presets.") };
    }

    private void Send_Click(object sender, RoutedEventArgs e) => DialogResult = true;
}
