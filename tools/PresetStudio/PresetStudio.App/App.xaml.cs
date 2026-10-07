using System.Windows;
using PresetStudio.App.Services;
using PresetStudio.Editing;
using PresetStudio.Shell;
using PresetStudio.Storage;

namespace PresetStudio.App;

public partial class App : Application
{
    private void OnStartup(object sender, StartupEventArgs e)
    {
        StudioContext studio;
        try
        {
            studio = StudioContext.Create();
        }
        catch (Exception ex)
        {
            // Only possible if the program was built with mismatched resource files.
            MessageBox.Show("Preset Studio could not load its built-in data:\n\n" + ex.Message, "Pico2Seq Preset Studio",
                MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(1);
            return;
        }

        var viewModel = new MainViewModel(studio, DeviceSession.ForSerialPorts(studio), new DialogService(),
            SettingsStore.AtDefaultLocation());
        var window = new MainWindow { DataContext = viewModel };
        MainWindow = window;
        window.Show();
        viewModel.RestoreSession();
        window.Closed += (_, _) => viewModel.Dispose();
    }

    private void OnUnhandledException(object sender, System.Windows.Threading.DispatcherUnhandledExceptionEventArgs e)
    {
        // A bug must never take the player's unsaved presets down with it: say so, and keep running.
        MessageBox.Show("Something unexpected went wrong:\n\n" + e.Exception.Message +
                        "\n\nYour presets are still open; save them before closing, then restart Preset Studio.",
            "Pico2Seq Preset Studio", MessageBoxButton.OK, MessageBoxImage.Warning);
        e.Handled = true;
    }
}
