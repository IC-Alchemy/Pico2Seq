using System.Windows;
using System.Windows.Media;

namespace PresetStudio.App.Views;

/// <summary>A readable message or question: wraps long text, shows a list of problems, never loses the end of a sentence.</summary>
public partial class MessageWindow : Window
{
    public MessageWindow(string title, string message, string acceptText, string? cancelText, bool isError)
    {
        InitializeComponent();
        Title = "Pico2Seq Preset Studio";
        TitleText.Text = title;
        BodyText.Text = message;
        AcceptButton.Content = acceptText;
        if (cancelText is null) CancelButton.Visibility = Visibility.Collapsed;
        else CancelButton.Content = cancelText;
        if (isError)
        {
            Badge.Background = (Brush)FindResource("ErrorBrush");
            BadgeText.Text = "!";
        }
        else
        {
            BadgeText.Text = cancelText is null ? "i" : "?";
        }
    }

    private void Accept_Click(object sender, RoutedEventArgs e) => DialogResult = true;
}
