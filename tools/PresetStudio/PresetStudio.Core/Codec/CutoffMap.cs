using PresetStudio.Model;

namespace PresetStudio.Codec;

/// <summary>
/// The filter's cutoff is stored as 0..1 and mapped to Hz by the base preset's layout. This is the same
/// curve the firmware uses (<c>dspmap::fmap</c> / <c>fmapCentered</c>), so the editor can show real Hz.
/// </summary>
public static class CutoffMap
{
    public static float ToHertz(CutoffInfo c, float normalized)
    {
        var x = Math.Clamp(normalized, 0f, 1f);
        if (c.Center is { } center)
            return x < 0.5f
                ? Half(x * 2f, c.Min, center, c.Curve)
                : Half((x - 0.5f) * 2f, center, c.Max, c.Curve);
        return Math.Clamp(Plain(x, c.Min, c.Max, c.Curve), c.Min, c.Max);
    }

    /// <summary>Inverse of <see cref="ToHertz"/>; values outside the span clamp to its ends.</summary>
    public static float FromHertz(CutoffInfo c, float hertz)
    {
        var hz = Math.Clamp(hertz, c.Min, c.Max);
        if (c.Center is { } center)
            return hz < center
                ? 0.5f * InverseHalf(hz, c.Min, center, c.Curve)
                : 0.5f + 0.5f * InverseHalf(hz, center, c.Max, c.Curve);
        return Math.Clamp(InversePlain(hz, c.Min, c.Max, c.Curve), 0f, 1f);
    }

    private static float Plain(float x, float min, float max, string curve) => curve switch
    {
        "exp" => min + x * x * (max - min),
        "log" => min * MathF.Pow(10f, x * MathF.Log10(max / min)),
        "octave" => min * MathF.Pow(max / min, x),
        _ => min + x * (max - min),
    };

    private static float InversePlain(float v, float min, float max, string curve) => curve switch
    {
        "exp" => max > min ? MathF.Sqrt(Math.Max(0f, (v - min) / (max - min))) : 0f,
        "log" or "octave" => min > 0 && max > min ? MathF.Log(v / min) / MathF.Log(max / min) : 0f,
        _ => max > min ? (v - min) / (max - min) : 0f,
    };

    private static float Half(float x, float a, float b, string curve) => curve switch
    {
        "log" or "octave" => a * MathF.Pow(b / a, x),
        "exp" => a + x * x * (b - a),
        _ => a + x * (b - a),
    };

    private static float InverseHalf(float v, float a, float b, string curve)
    {
        if (b <= a) return 0f;
        return curve switch
        {
            "log" or "octave" => MathF.Log(v / a) / MathF.Log(b / a),
            "exp" => MathF.Sqrt(Math.Max(0f, (v - a) / (b - a))),
            _ => (v - a) / (b - a),
        };
    }
}
