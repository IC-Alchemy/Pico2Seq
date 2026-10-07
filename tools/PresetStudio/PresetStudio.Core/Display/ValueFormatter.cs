using System.Globalization;
using System.Text.RegularExpressions;
using PresetStudio.Schema;

namespace PresetStudio.Display;

/// <summary>
/// Shows values the way the box shows them (percent, seconds, Hz, semitones, cents) and reads what
/// the player types back. Everything is culture-invariant so a decimal comma never changes a sound.
/// </summary>
public static class ValueFormatter
{
    private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;
    private static readonly Regex NumberWithUnit = new(@"^([+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?)\s*([a-z%]*)$", RegexOptions.Compiled);

    public static string Format(FieldDef field, string unit, float value)
    {
        if (field.IsChoice)
            return field.Choices!.FirstOrDefault(c => c.Value == (int)value)?.Label ?? ((int)value).ToString(Inv);
        if (field.IsToggle) return value >= 0.5f ? "On" : "Off";
        switch (unit)
        {
            case "Percent": return (value * 100f).ToString("0.#", Inv) + " %";
            case "Seconds": return FormatSeconds(value);
            case "Hertz": return value <= 20.0001f && field.Key == "hp.cutoff" ? "Off" : FormatHertz(value);
            case "Semitones": return (value >= 0 ? "+" : "") + value.ToString("0.##", Inv) + " st";
            case "Cents": return value.ToString("0.#", Inv) + " ct";
            default: return field.Integer ? ((int)MathF.Round(value)).ToString(Inv) : value.ToString("0.###", Inv);
        }
    }

    public static string FormatSeconds(float s)
    {
        if (s < 1f) return (s * 1000f).ToString("0.#", Inv) + " ms";
        if (s < 10f) return s.ToString("0.###", Inv) + " s";
        return s.ToString("0.#", Inv) + " s";
    }

    public static string FormatHertz(float hz) =>
        hz >= 1000f ? (hz / 1000f).ToString("0.##", Inv) + " kHz" : hz.ToString("0.#", Inv) + " Hz";

    /// <summary>
    /// Reads a typed value: "37", "37%", "250ms", "1.5 s", "440hz", "2k", "+3", "on", a choice's label.
    /// Plain numbers mean the unit the field is shown in (percent for percent fields, seconds for time).
    /// </summary>
    public static bool TryParse(FieldDef field, string unit, string? text, out float value)
    {
        value = 0;
        if (string.IsNullOrWhiteSpace(text)) return false;
        var t = text.Trim().ToLowerInvariant().Replace(',', '.');

        if (field.IsChoice)
        {
            var match = field.Choices!.FirstOrDefault(c => string.Equals(c.Label, text.Trim(), StringComparison.OrdinalIgnoreCase));
            if (match is not null) { value = match.Value; return true; }
            if (int.TryParse(t, NumberStyles.Integer, Inv, out var n) && field.Choices!.Any(c => c.Value == n))
            {
                value = n;
                return true;
            }
            return false;
        }
        if (field.IsToggle)
        {
            if (t is "on" or "yes" or "true" or "1") { value = 1; return true; }
            if (t is "off" or "no" or "false" or "0") { value = 0; return true; }
            return false;
        }

        if (field.Key == "hp.cutoff" && t == "off") { value = 0; return true; }

        // Split the number from a trailing unit.
        var m = NumberWithUnit.Match(t);
        if (!m.Success || !float.TryParse(m.Groups[1].Value, NumberStyles.Float, Inv, out var v)) return false;
        var suffix = m.Groups[2].Value.Trim();

        switch (unit)
        {
            case "Percent":
                value = suffix is "" or "%" ? v / 100f : float.NaN;
                break;
            case "Seconds":
                value = suffix switch { "" or "s" or "sec" => v, "ms" => v / 1000f, _ => float.NaN };
                break;
            case "Hertz":
                value = suffix switch { "" or "hz" => v, "k" or "khz" => v * 1000f, _ => float.NaN };
                break;
            case "Semitones":
                value = suffix is "" or "st" or "semitones" ? v : float.NaN;
                break;
            case "Cents":
                value = suffix is "" or "ct" or "cents" ? v : float.NaN;
                break;
            default:
                value = suffix == "" ? v : float.NaN;
                break;
        }
        return !float.IsNaN(value) && !float.IsInfinity(value);
    }
}
