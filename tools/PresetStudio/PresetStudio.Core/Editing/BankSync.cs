using PresetStudio.Codec;
using PresetStudio.Display;
using PresetStudio.Link;
using PresetStudio.Model;

namespace PresetStudio.Editing;

/// <summary>One difference between the library and what is on the box.</summary>
public sealed record BankChange(BankChangeKind Kind, UserPreset Preset, string Summary);

public enum BankChangeKind { Added, Changed, Removed }

/// <summary>What sending the library would do to the box, preset by preset.</summary>
public sealed class BankDiff
{
    public IReadOnlyList<BankChange> Changes { get; }
    public int Unchanged { get; }
    public int Added => Changes.Count(c => c.Kind == BankChangeKind.Added);
    public int Changed => Changes.Count(c => c.Kind == BankChangeKind.Changed);
    public int Removed => Changes.Count(c => c.Kind == BankChangeKind.Removed);
    public bool IsEmpty => Changes.Count == 0;

    public BankDiff(IReadOnlyList<BankChange> changes, int unchanged)
    {
        Changes = changes;
        Unchanged = unchanged;
    }
}

/// <summary>Reading the box's bank, comparing it with the library, and sending the library across.</summary>
public sealed class BankSync
{
    private readonly StudioContext _studio;

    public BankSync(StudioContext studio) => _studio = studio;

    /// <summary>Bytes the box needs on flash for a bank of this many presets.</summary>
    public static int BankBytes(int count) => 12 + 256 * count + 4;

    public List<UserPreset> Decode(IEnumerable<byte[]> records) => records.Select(r => _studio.Codec.Decode(r)).ToList();

    /// <summary>
    /// Compares by place (page and pad): a preset is Added if the box has nothing there, Removed if the library has
    /// nothing there, Changed if the stored bytes differ. The comparison is on the box's canonical form, so a
    /// preset that only differs in the editor's own notes is not reported.
    /// </summary>
    public BankDiff Compare(IReadOnlyList<UserPreset> library, IReadOnlyList<UserPreset> device)
    {
        var changes = new List<BankChange>();
        var unchanged = 0;
        var onDevice = device.ToDictionary(p => (p.Page, p.Pad));
        var inLibrary = library.ToDictionary(p => (p.Page, p.Pad));

        foreach (var local in library.OrderBy(p => p.Page).ThenBy(p => p.Pad))
        {
            if (!onDevice.TryGetValue((local.Page, local.Pad), out var remote))
            {
                changes.Add(new BankChange(BankChangeKind.Added, local, "new preset"));
                continue;
            }
            var a = _studio.Codec.Encode(local);
            var b = _studio.Codec.Encode(remote);
            if (a.AsSpan().SequenceEqual(b)) { unchanged++; continue; }
            changes.Add(new BankChange(BankChangeKind.Changed, local, Describe(local, remote)));
        }
        foreach (var remote in device.OrderBy(p => p.Page).ThenBy(p => p.Pad))
            if (!inLibrary.ContainsKey((remote.Page, remote.Pad)))
                changes.Add(new BankChange(BankChangeKind.Removed, remote, "will be removed from the Pico"));
        return new BankDiff(changes, unchanged);
    }

    /// <summary>Plain-language list of what differs between two versions of a preset at the same pad.</summary>
    public string Describe(UserPreset local, UserPreset remote)
    {
        var parts = new List<string>();
        if (local.Name != remote.Name) parts.Add($"renamed from '{remote.Name}'");
        if (local.Color != remote.Color) parts.Add("new colour");
        if (local.BaseIndex != remote.BaseIndex)
            parts.Add($"built on {_studio.Factory[local.BaseIndex].Name} instead of {_studio.Factory[Math.Min(remote.BaseIndex, _studio.Factory.Count - 1)].Name}");
        var changedValues = new List<string>();
        foreach (var field in _studio.Schema.Fields)
        {
            var before = remote.Values[field];
            var after = local.Values[field];
            if (Math.Abs(before - after) <= 1e-6f * Math.Max(1f, Math.Abs(before))) continue;
            var unit = _studio.Limits.Resolve(field, local).Unit;
            changedValues.Add($"{_studio.Help.For(field.Key).Title}: {ValueFormatter.Format(field, unit, before)} → {ValueFormatter.Format(field, unit, after)}");
        }
        if (changedValues.Count > 0)
            parts.Add(changedValues.Count <= 4
                ? string.Join("; ", changedValues)
                : string.Join("; ", changedValues.Take(3)) + $"; and {changedValues.Count - 3} more values");
        return parts.Count == 0 ? "changed" : string.Join(". ", parts);
    }

    /// <summary>Sends the library to the box as its whole user bank, then reads it back and checks it.</summary>
    public void Push(DeviceLink link, IReadOnlyList<UserPreset> library, IProgress<LinkProgress>? progress = null,
        CancellationToken cancel = default)
    {
        var ordered = library.OrderBy(p => p.Page).ThenBy(p => p.Pad).ToList();
        var records = ordered.Select(p => _studio.Codec.Encode(p)).ToList();
        link.UploadBank(records, ordered.Select(p => p.Name).ToList(), progress, cancel);

        // Prove it landed: read everything back and compare with what was sent.
        var info = link.Hello();
        if (info.PresetCount != records.Count)
            throw new LinkException($"The Pico reports {info.PresetCount} presets after the transfer, but {records.Count} were sent.");
        var back = link.DownloadBank(info, progress, cancel);
        var sent = records.ToDictionary(r => (r[_studio.Schema.Record.PageOffset], r[_studio.Schema.Record.PadOffset]));
        foreach (var r in back)
        {
            var key = (r[_studio.Schema.Record.PageOffset], r[_studio.Schema.Record.PadOffset]);
            if (!sent.TryGetValue(key, out var original) || !original.AsSpan().SequenceEqual(r))
                throw new LinkException("The presets the Pico stored do not match what was sent. Try sending again.");
        }
    }

    /// <summary>Reads the box's bank. Throws <see cref="IncompatibleDeviceException"/> for firmware that does not fit.</summary>
    public (DeviceInfo Info, List<UserPreset> Presets) Pull(DeviceLink link, IProgress<LinkProgress>? progress = null,
        CancellationToken cancel = default)
    {
        var info = link.Hello();
        var records = link.DownloadBank(info, progress, cancel);
        return (info, Decode(records));
    }
}
