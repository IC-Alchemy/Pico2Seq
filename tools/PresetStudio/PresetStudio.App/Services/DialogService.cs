using System.Windows;
using Microsoft.Win32;
using PresetStudio.App.Views;
using PresetStudio.Shell;

namespace PresetStudio.App.Services;

/// <summary>The Windows dialogs behind <see cref="IDialogService"/>.</summary>
public sealed class DialogService : IDialogService
{
    private static Window? Owner => Application.Current?.MainWindow;

    public string? ChooseFileToOpen(string title, string filter)
    {
        var dialog = new OpenFileDialog { Title = title, Filter = filter, CheckFileExists = true };
        return dialog.ShowDialog(Owner) == true ? dialog.FileName : null;
    }

    public string? ChooseFileToSave(string title, string filter, string suggestedName)
    {
        var dialog = new SaveFileDialog
        {
            Title = title,
            Filter = filter,
            FileName = suggestedName,
            AddExtension = true,
            OverwritePrompt = true,
        };
        return dialog.ShowDialog(Owner) == true ? dialog.FileName : null;
    }

    public bool Confirm(string title, string message, string acceptText = "OK", string cancelText = "Cancel")
    {
        var window = new MessageWindow(title, message, acceptText, cancelText, isError: false) { Owner = Owner };
        return window.ShowDialog() == true;
    }

    public SavePromptResult AskSave(string title, string message)
    {
        var result = MessageBox.Show(Owner!, message, title, MessageBoxButton.YesNoCancel, MessageBoxImage.Question);
        return result switch
        {
            MessageBoxResult.Yes => SavePromptResult.Save,
            MessageBoxResult.No => SavePromptResult.Discard,
            _ => SavePromptResult.Cancel,
        };
    }

    public void ShowInfo(string title, string message) =>
        new MessageWindow(title, message, "OK", null, isError: false) { Owner = Owner }.ShowDialog();

    public void ShowError(string title, string message) =>
        new MessageWindow(title, message, "OK", null, isError: true) { Owner = Owner }.ShowDialog();

    public bool ConfirmSync(SyncPreview preview) =>
        new SyncPreviewWindow(preview) { Owner = Owner }.ShowDialog() == true;

    public void ShowQuickStart() => new QuickStartWindow { Owner = Owner }.ShowDialog();
}
