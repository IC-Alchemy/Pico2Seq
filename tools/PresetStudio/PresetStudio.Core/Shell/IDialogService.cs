using PresetStudio.Editing;
using PresetStudio.Link;

namespace PresetStudio.Shell;

public enum SavePromptResult { Save, Discard, Cancel }

/// <summary>Everything the player is shown before a transfer: what will change, and whether it fits.</summary>
public sealed record SyncPreview(
    BankDiff Diff,
    int PresetCount,
    int BytesNeeded,
    long BytesFree,
    IReadOnlyList<string> Warnings,
    string PortName)
{
    public bool FitsInStorage => BytesNeeded <= BytesFree;
}

/// <summary>The few questions and messages the editor needs a window for. The app supplies a WPF implementation.</summary>
public interface IDialogService
{
    string? ChooseFileToOpen(string title, string filter);
    string? ChooseFileToSave(string title, string filter, string suggestedName);
    bool Confirm(string title, string message, string acceptText = "OK", string cancelText = "Cancel");
    SavePromptResult AskSave(string title, string message);
    void ShowInfo(string title, string message);
    void ShowError(string title, string message);
    /// <summary>Shows what a send will change; true to go ahead.</summary>
    bool ConfirmSync(SyncPreview preview);
    void ShowQuickStart();
}
