using System.Globalization;

namespace PresetStudio.Model;

/// <summary>An LED colour, 8 bits per channel, exactly as the box stores it.</summary>
public readonly record struct Rgb(byte R, byte G, byte B)
{
    public static readonly Rgb White = new(255, 255, 255);

    public uint ToUInt32() => ((uint)R << 16) | ((uint)G << 8) | B;
    public static Rgb FromUInt32(uint value) => new((byte)(value >> 16), (byte)(value >> 8), (byte)value);

    /// <summary>"#RRGGBB".</summary>
    public string ToHex() => $"#{R:X2}{G:X2}{B:X2}";

    public static bool TryParseHex(string? text, out Rgb color)
    {
        color = default;
        if (string.IsNullOrWhiteSpace(text)) return false;
        var s = text.Trim().TrimStart('#');
        if (s.Length != 6 || !uint.TryParse(s, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var v))
            return false;
        color = FromUInt32(v);
        return true;
    }

    public static Rgb ParseHex(string text) =>
        TryParseHex(text, out var c) ? c : throw new FormatException($"'{text}' is not a #RRGGBB colour");

    /// <summary>
    /// Roughly how the LED will look: the box runs idle presets at a quarter of the chosen colour,
    /// the preset in use at full strength.
    /// </summary>
    public Rgb Scaled(double factor) =>
        new((byte)Math.Clamp(Math.Round(R * factor), 0, 255), (byte)Math.Clamp(Math.Round(G * factor), 0, 255),
            (byte)Math.Clamp(Math.Round(B * factor), 0, 255));

    public override string ToString() => ToHex();
}
