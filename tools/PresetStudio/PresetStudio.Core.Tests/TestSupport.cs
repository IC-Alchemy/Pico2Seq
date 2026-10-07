using System.Text.Json;
using PresetStudio.Editing;
using PresetStudio.Model;

namespace PresetStudio.Tests;

internal static class TestSupport
{
    private static readonly Lazy<StudioContext> Shared = new(StudioContext.Create);
    public static StudioContext Studio => Shared.Value;

    public static byte[] FromHex(string hex) => Convert.FromHexString(hex);
    public static string ToHex(byte[] bytes) => Convert.ToHexString(bytes).ToLowerInvariant();

    public static JsonDocument Golden() => JsonDocument.Parse(PresetStudio.Schema.EmbeddedResources.ReadText("golden-vectors.json"));

    /// <summary>A fresh preset built on a factory preset, placed on page 1 (the first user page).</summary>
    public static UserPreset NewPreset(string factoryName = "Bass", int pad = 0)
    {
        var f = Studio.Factory.FindByName(factoryName) ?? throw new InvalidOperationException(factoryName);
        var p = Studio.Factory.NewFrom(f.Index, Studio.Schema);
        p.Page = 1;
        p.Pad = pad;
        p.Color = new Rgb(10, 200, 90);
        return p;
    }

    /// <summary>
    /// Path of the firmware simulator (tests/tools/preset_link_sim.cpp) if it has been built, else null.
    /// Set PICO2SEQ_LINK_SIM to override; by default it looks in the repository's host test build folders.
    /// </summary>
    public static string? SimulatorPath()
    {
        var env = Environment.GetEnvironmentVariable("PICO2SEQ_LINK_SIM");
        if (!string.IsNullOrEmpty(env)) return File.Exists(env) ? env : null;
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null)
        {
            foreach (var build in new[] { "build_test_ninja", "build_test", "build_host" })
            {
                foreach (var name in new[] { "preset_link_sim", "preset_link_sim.exe" })
                {
                    var candidate = Path.Combine(dir.FullName, build, "tests", name);
                    if (File.Exists(candidate)) return candidate;
                }
            }
            dir = dir.Parent;
        }
        return null;
    }
}
