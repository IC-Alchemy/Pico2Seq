using System.Buffers.Binary;
using PresetStudio.Schema;

namespace PresetStudio.Link;

/// <summary>What the box says about itself in reply to Hello.</summary>
public sealed record DeviceInfo(
    int Protocol, bool TransportRunning, bool UploadInProgress, int LayoutVersion, int RecordSize, int PatchSize,
    int FactoryCount, int UserPages, int PadsPerPage, int MaxPresets, int PresetCount,
    uint FreeBytes, uint BankBytes, uint TableHash)
{
    public const int WireSize = 28;

    public static DeviceInfo Parse(ReadOnlySpan<byte> p)
    {
        if (p.Length != WireSize) throw new LinkException($"The Pico's hello reply had {p.Length} bytes instead of {WireSize}; its firmware does not match this editor.");
        return new DeviceInfo(
            Protocol: p[0],
            TransportRunning: (p[1] & 1) != 0,
            UploadInProgress: (p[1] & 2) != 0,
            LayoutVersion: BinaryPrimitives.ReadUInt16LittleEndian(p[2..]),
            RecordSize: BinaryPrimitives.ReadUInt16LittleEndian(p[4..]),
            PatchSize: BinaryPrimitives.ReadUInt16LittleEndian(p[6..]),
            FactoryCount: p[8],
            UserPages: p[9],
            PadsPerPage: p[10],
            MaxPresets: BinaryPrimitives.ReadUInt16LittleEndian(p[12..]),
            PresetCount: BinaryPrimitives.ReadUInt16LittleEndian(p[14..]),
            FreeBytes: BinaryPrimitives.ReadUInt32LittleEndian(p[16..]),
            BankBytes: BinaryPrimitives.ReadUInt32LittleEndian(p[20..]),
            TableHash: BinaryPrimitives.ReadUInt32LittleEndian(p[24..]));
    }

    /// <summary>
    /// Null when this editor can safely talk to the box; otherwise why not, in words for the player.
    /// A mismatch means the firmware and Preset Studio were built from different layouts of the patch, and
    /// sending a preset could silently put values in the wrong place.
    /// </summary>
    public string? Incompatibility(PatchSchema schema)
    {
        if (Protocol != LinkProtocol.ProtocolVersion)
            return Protocol > LinkProtocol.ProtocolVersion
                ? "The Pico's firmware speaks a newer version of the Preset Studio link. Update Preset Studio."
                : "The Pico's firmware speaks an older version of the Preset Studio link. Update the firmware (flash the latest Pico2Seq.ino.uf2).";
        if (LayoutVersion != schema.LayoutVersion || RecordSize != schema.RecordSize || PatchSize != schema.PatchSize || TableHash != schema.TableHash)
            return LayoutVersion > schema.LayoutVersion
                ? "The Pico's firmware stores presets in a newer layout than this Preset Studio knows. Update Preset Studio."
                : "The Pico's firmware stores presets in an older layout than this Preset Studio. Update the firmware (flash the latest Pico2Seq.ino.uf2).";
        if (UserPages != schema.Browser.UserPages || PadsPerPage != schema.Browser.PadsPerPage || MaxPresets != schema.Browser.MaxPresets)
            return "The Pico's preset pages are laid out differently from this Preset Studio. Update the firmware or Preset Studio so they match.";
        return null;
    }
}
