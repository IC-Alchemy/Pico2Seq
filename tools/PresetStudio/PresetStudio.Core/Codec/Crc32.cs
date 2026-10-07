namespace PresetStudio.Codec;

/// <summary>IEEE CRC-32, the same one the box uses for its song file, bank file and link frames.</summary>
public static class Crc32
{
    private static readonly uint[] Table = BuildTable();

    private static uint[] BuildTable()
    {
        var table = new uint[256];
        for (uint i = 0; i < 256; i++)
        {
            var c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        return table;
    }

    public static uint Compute(ReadOnlySpan<byte> data) => Update(0, data);

    /// <summary>Continues a CRC: <c>Update(Update(0, a), b) == Compute(a ++ b)</c>.</summary>
    public static uint Update(uint crc, ReadOnlySpan<byte> data)
    {
        var c = crc ^ 0xFFFFFFFFu;
        foreach (var b in data)
            c = Table[(c ^ b) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    }
}
