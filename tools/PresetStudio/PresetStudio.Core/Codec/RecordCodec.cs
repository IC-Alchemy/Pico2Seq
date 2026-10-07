using System.Buffers.Binary;
using System.Text;
using PresetStudio.Model;
using PresetStudio.Schema;

namespace PresetStudio.Codec;

/// <summary>
/// Turns a <see cref="UserPreset"/> into the 256-byte record the box stores, and back. The layout
/// comes entirely from the <see cref="PatchSchema"/>; the canonical form (derived bytes the editor
/// does not own) mirrors <c>usercodec::canonicalize</c> in the firmware, which is authoritative.
/// </summary>
public sealed class RecordCodec
{
    private readonly PatchSchema _schema;
    private readonly FactoryCatalog _factory;

    public RecordCodec(PatchSchema schema, FactoryCatalog factory)
    {
        _schema = schema;
        _factory = factory;
    }

    public int RecordSize => _schema.RecordSize;

    public byte[] Encode(UserPreset preset)
    {
        var r = new byte[_schema.RecordSize];
        var layout = _schema.Record;

        var name = SanitizeName(preset.Name);
        var nameBytes = Encoding.ASCII.GetBytes(name);
        Array.Copy(nameBytes, 0, r, layout.NameOffset, Math.Min(nameBytes.Length, _schema.NameMaxLength));
        r[layout.PageOffset] = (byte)preset.Page;
        r[layout.PadOffset] = (byte)preset.Pad;
        r[layout.BaseIndexOffset] = (byte)preset.BaseIndex;
        r[layout.ColorOffset] = preset.Color.R;
        r[layout.ColorOffset + 1] = preset.Color.G;
        r[layout.ColorOffset + 2] = preset.Color.B;

        var patch = r.AsSpan(layout.PatchOffset, _schema.PatchSize);
        foreach (var (field, value) in preset.Values.Enumerate())
            WriteField(patch, field, value);
        Canonicalize(r, preset);
        return r;
    }

    private void Canonicalize(byte[] r, UserPreset preset)
    {
        var layout = _schema.Record;
        // Bases are stored quantised: whole scale steps, whole octaves (round half away from zero, like C++).
        var note = _schema["base.note"];
        var octave = _schema["base.octave"];
        WriteFloat(r, layout.PatchOffset + note.Offset,
            MathF.Round(preset.Values[note], MidpointRounding.AwayFromZero));
        WriteFloat(r, layout.PatchOffset + octave.Offset,
            MathF.Round(preset.Values[octave] / 12f, MidpointRounding.AwayFromZero) * 12f);

        r[layout.PresetIndexOffset] = (byte)preset.BaseIndex;
        r[layout.FlagsOffset] |= (byte)layout.UsePatchBasesMask;
        r[layout.ParamSetOffset] = (byte)DeriveParamSet(preset);
    }

    /// <summary>The lane set the box derives from the engine (and hard-sync waveforms), like its own editor.</summary>
    public int DeriveParamSet(UserPreset preset)
    {
        var engine = (int)preset.Values["source.engine"];
        var e = _schema.Engines;
        var p = _schema.ParamSets;
        if (engine == e.Waveguide) return p.Waveguide;
        if (engine == e.Hypersaw) return p.Hypersaw;
        if (engine == e.NoiseFx) return p.NoiseStorm;
        if (engine == e.Recipe)
            return preset.BaseIndex >= 0 && preset.BaseIndex < _factory.Count ? _factory[preset.BaseIndex].ParamSet : p.Standard;
        var active = Math.Min((int)preset.Values["source.oscCount"], 3);
        for (int i = 0; i < active; i++)
            if ((int)preset.Values[$"osc{i + 1}.wave"] == _schema.Waveforms.HardSyncSaw)
                return p.HardSync;
        return p.Standard;
    }

    public UserPreset Decode(ReadOnlySpan<byte> record)
    {
        if (record.Length != _schema.RecordSize)
            throw new ArgumentException($"a record is {_schema.RecordSize} bytes, got {record.Length}");
        var layout = _schema.Record;
        var values = new PatchValues(_schema);
        var patch = record.Slice(layout.PatchOffset, _schema.PatchSize);
        foreach (var field in _schema.Fields)
            values[field] = ReadField(patch, field);

        var nameEnd = record.Slice(layout.NameOffset, _schema.NameSize).IndexOf((byte)0);
        if (nameEnd < 0) nameEnd = _schema.NameSize;
        return new UserPreset
        {
            Name = Encoding.ASCII.GetString(record.Slice(layout.NameOffset, nameEnd)),
            Page = record[layout.PageOffset],
            Pad = record[layout.PadOffset],
            BaseIndex = record[layout.BaseIndexOffset],
            Color = new Rgb(record[layout.ColorOffset], record[layout.ColorOffset + 1], record[layout.ColorOffset + 2]),
            Values = values,
        };
    }

    /// <summary>Printable ASCII only, trimmed to what the box can hold.</summary>
    public string SanitizeName(string? name)
    {
        var sb = new StringBuilder();
        foreach (var ch in name ?? "")
        {
            if (sb.Length >= _schema.NameMaxLength) break;
            sb.Append(ch is >= ' ' and <= '~' ? ch : '?');
        }
        return sb.ToString();
    }

    private static void WriteFloat(byte[] buffer, int offset, float value) =>
        BinaryPrimitives.WriteSingleLittleEndian(buffer.AsSpan(offset, 4), value);

    private static void WriteField(Span<byte> patch, FieldDef f, float value)
    {
        switch (f.Type)
        {
            case FieldType.Float:
                BinaryPrimitives.WriteSingleLittleEndian(patch.Slice(f.Offset, 4), value);
                break;
            case FieldType.Int32:
                BinaryPrimitives.WriteInt32LittleEndian(patch.Slice(f.Offset, 4), (int)MathF.Round(value, MidpointRounding.AwayFromZero));
                break;
            case FieldType.Byte:
                patch[f.Offset] = (byte)MathF.Round(value, MidpointRounding.AwayFromZero);
                break;
            case FieldType.Flag:
                if (value >= 0.5f) patch[f.Offset] |= (byte)f.Mask;
                else patch[f.Offset] &= (byte)~f.Mask;
                break;
        }
    }

    private static float ReadField(ReadOnlySpan<byte> patch, FieldDef f) => f.Type switch
    {
        FieldType.Float => BinaryPrimitives.ReadSingleLittleEndian(patch.Slice(f.Offset, 4)),
        FieldType.Int32 => BinaryPrimitives.ReadInt32LittleEndian(patch.Slice(f.Offset, 4)),
        FieldType.Byte => patch[f.Offset],
        _ => (patch[f.Offset] & f.Mask) != 0 ? 1f : 0f,
    };
}
