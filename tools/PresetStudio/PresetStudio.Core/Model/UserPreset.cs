namespace PresetStudio.Model;

/// <summary>
/// One preset of the player's own: a name, an LED colour, a place on the browser's pages and the sound itself.
/// Everything except <see cref="Id"/> and <see cref="Notes"/> is stored on the box.
/// </summary>
public sealed class UserPreset
{
    /// <summary>Local identity (never sent to the box), so renames and moves keep selection stable.</summary>
    public Guid Id { get; set; } = Guid.NewGuid();
    /// <summary>At most 15 printable ASCII characters.</summary>
    public string Name { get; set; } = "New Preset";
    public Rgb Color { get; set; } = Rgb.White;
    /// <summary>Browser page, 1.. (page 0 is the factory bank and cannot be used).</summary>
    public int Page { get; set; } = 1;
    /// <summary>Pad on the page, 0..30 (pad 31 flips pages).</summary>
    public int Pad { get; set; }
    /// <summary>Index of the factory preset this one is built on; supplies the lane layout and recipe.</summary>
    public int BaseIndex { get; set; }
    public PatchValues Values { get; set; } = null!;
    /// <summary>Your own remarks. Kept in the library file only.</summary>
    public string Notes { get; set; } = "";

    public UserPreset Clone()
    {
        var copy = (UserPreset)MemberwiseClone();
        copy.Values = Values.Clone();
        return copy;
    }

    /// <summary>A new Id for a copy that should live next to the original.</summary>
    public UserPreset Duplicate()
    {
        var copy = Clone();
        copy.Id = Guid.NewGuid();
        return copy;
    }

    public override string ToString() => $"{Name} (page {Page + 1}, pad {Pad})";
}
