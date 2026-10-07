using System.Text.Json;

namespace PresetStudio.Storage;

/// <summary>Small things worth remembering between runs. Losing the file costs nothing.</summary>
public sealed class AppSettings
{
    public string? LastPort { get; set; }
    public string? LastLibrary { get; set; }
    public int AuditionVoice { get; set; } = 1;
    public bool LiveAudition { get; set; }
}

public sealed class SettingsStore
{
    private readonly string _path;

    public SettingsStore(string path)
    {
        _path = path;
        Current = Load();
    }

    public AppSettings Current { get; }

    /// <summary>The usual place: <c>%APPDATA%\Pico2SeqPresetStudio\settings.json</c>.</summary>
    public static SettingsStore AtDefaultLocation() => new(Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Pico2SeqPresetStudio", "settings.json"));

    private AppSettings Load()
    {
        try
        {
            return File.Exists(_path)
                ? JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(_path)) ?? new AppSettings()
                : new AppSettings();
        }
        catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException)
        {
            return new AppSettings();
        }
    }

    public void Update(Action<AppSettings> change)
    {
        change(Current);
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_path)!);
            File.WriteAllText(_path, JsonSerializer.Serialize(Current, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            // Not being able to remember a port is never worth interrupting the player.
        }
    }
}
