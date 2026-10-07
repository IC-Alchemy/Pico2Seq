using System.Text.Json;
using PresetStudio.Editing;
using PresetStudio.Model;
using PresetStudio.Storage;
using Xunit;

namespace PresetStudio.Tests;

public class LibraryTests : IDisposable
{
    private readonly StudioContext _s = TestSupport.Studio;
    private readonly string _dir = Directory.CreateTempSubdirectory("p2lib").FullName;

    public void Dispose() => Directory.Delete(_dir, recursive: true);

    [Fact]
    public void New_presets_take_the_first_free_pad_and_unique_names()
    {
        var lib = new LibraryEditor(_s);
        var bass = _s.Factory.FindByName("Bass")!.Index;
        var a = lib.AddFromFactory(bass)!;
        var b = lib.AddFromFactory(bass)!;
        Assert.Equal((1, 0), (a.Page, a.Pad));
        Assert.Equal((1, 1), (b.Page, b.Pad));
        Assert.Equal("Bass", a.Name);
        Assert.Equal("Bass 2", b.Name);
        Assert.NotEqual(a.Color, _s.Factory.NewFrom(0, _s.Schema).Color);
        Assert.Same(b, lib.Selected);
        Assert.True(lib.IsDirty);
    }

    [Fact]
    public void Unique_names_respect_the_name_length_limit()
    {
        var lib = new LibraryEditor(_s);
        var p = lib.AddFromFactory(0)!;
        p.Name = "123456789012345";
        var next = lib.UniqueName("123456789012345");
        Assert.Equal(15, next.Length);
        Assert.EndsWith(" 2", next);
    }

    [Fact]
    public void The_library_fills_both_user_pages_and_then_refuses()
    {
        var lib = new LibraryEditor(_s);
        for (int i = 0; i < 62; i++) Assert.NotNull(lib.AddFromFactory(i % _s.Factory.Count));
        Assert.True(lib.IsFull);
        Assert.Null(lib.AddFromFactory(0));
        Assert.Null(lib.Duplicate(lib.Presets[0]));
        Assert.Null(lib.FindFreeSlot());
        Assert.Equal(31, lib.Presets.Count(p => p.Page == 1));
        Assert.Equal(31, lib.Presets.Count(p => p.Page == 2));
        Assert.DoesNotContain(lib.Validate(), i => i.Severity == Rules.IssueSeverity.Error);
    }

    [Fact]
    public void Moving_onto_an_occupied_pad_swaps_so_nothing_is_lost()
    {
        var lib = new LibraryEditor(_s);
        var a = lib.AddFromFactory(0)!;
        var b = lib.AddFromFactory(1)!;
        lib.MoveTo(a, b.Page, b.Pad);
        Assert.Equal((1, 1), (a.Page, a.Pad));
        Assert.Equal((1, 0), (b.Page, b.Pad));
        lib.MoveTo(a, 2, 30);
        Assert.Equal((2, 30), (a.Page, a.Pad));
        Assert.Null(lib.PresetAt(1, 1));
        Assert.Throws<ArgumentOutOfRangeException>(() => lib.MoveTo(a, 2, 31)); // the page key
        Assert.Throws<ArgumentOutOfRangeException>(() => lib.MoveTo(a, 0, 3));  // the factory page
        Assert.Throws<ArgumentOutOfRangeException>(() => lib.MoveTo(a, 3, 3));  // no such page
    }

    [Fact]
    public void Duplicating_and_removing_keep_selection_sensible()
    {
        var lib = new LibraryEditor(_s);
        var a = lib.AddFromFactory(0)!;
        var copy = lib.Duplicate(a)!;
        Assert.NotEqual(a.Id, copy.Id);
        Assert.NotSame(a.Values, copy.Values);
        copy.Values["filter.resonance"] = 0.9f;
        Assert.NotEqual(a.Values["filter.resonance"], copy.Values["filter.resonance"]);
        Assert.Same(copy, lib.Selected);
        lib.Remove(copy);
        Assert.Same(a, lib.Selected);
        lib.Remove(a);
        Assert.Null(lib.Selected);
        Assert.Empty(lib.Presets);
    }

    [Fact]
    public void Edits_through_the_editor_mark_the_library_changed()
    {
        var lib = new LibraryEditor(_s);
        var a = lib.AddFromFactory(0)!;
        var path = Path.Combine(_dir, "set.p2lib");
        lib.SaveAs(path);
        Assert.False(lib.IsDirty);
        var seen = new List<EditKind>();
        lib.PresetEdited += (_, k) => seen.Add(k);
        lib.Editor!.Parameter("filter.resonance").Value = 0.6f;
        Assert.True(lib.IsDirty);
        Assert.Equal(new[] { EditKind.Value }, seen);
        Assert.EndsWith("*", lib.Title);
    }

    [Fact]
    public void A_library_saves_and_loads_exactly()
    {
        var lib = new LibraryEditor(_s);
        var a = lib.AddFromFactory(_s.Factory.FindByName("Bass")!.Index)!;
        a.Values["filter.resonance"] = 0.123456789f;
        a.Notes = "Warm, for the intro é";
        a.Name = "Warm Bass";
        a.Color = new Rgb(1, 2, 3);
        var b = lib.AddFromFactory(_s.Factory.FindByName("PhaseMorph")!.Index)!;
        b.Page = 2;
        b.Pad = 30;
        var path = Path.Combine(_dir, "mine.p2lib");
        lib.SaveAs(path);

        var back = new LibraryEditor(_s);
        back.Open(path);
        Assert.False(back.IsDirty);
        Assert.Equal(2, back.Presets.Count);
        var a2 = back.Presets[0];
        Assert.Equal(a.Id, a2.Id);
        Assert.Equal("Warm Bass", a2.Name);
        Assert.Equal(new Rgb(1, 2, 3), a2.Color);
        Assert.Equal(a.Notes, a2.Notes);
        Assert.Equal((a.Page, a.Pad, a.BaseIndex), (a2.Page, a2.Pad, a2.BaseIndex));
        Assert.True(a.Values.SameAs(a2.Values), "values must survive a file round trip bit for bit");
        Assert.Equal(_s.Codec.Encode(b), _s.Codec.Encode(back.Presets[1]));
    }

    [Fact]
    public void Saved_files_use_the_page_number_the_box_shows_and_name_the_base_preset()
    {
        var lib = new LibraryEditor(_s);
        lib.AddFromFactory(_s.Factory.FindByName("Bass")!.Index);
        var text = _s.Files.WriteLibrary(lib.Library);
        using var doc = JsonDocument.Parse(text);
        var p = doc.RootElement.GetProperty("presets")[0];
        Assert.Equal(2, p.GetProperty("page").GetInt32());   // the first user page is "page 2" on the box
        Assert.Equal("Bass", p.GetProperty("base").GetString());
        Assert.True(p.GetProperty("values").TryGetProperty("filter.resonance", out _));
    }

    [Fact]
    public void Files_from_other_versions_are_handled_kindly()
    {
        var lib = new LibraryEditor(_s);
        lib.AddFromFactory(0);
        var text = _s.Files.WriteLibrary(lib.Library);

        // A value this version does not know is ignored, a value it expects but the file lacks comes from the base.
        var edited = text.Replace("\"filter.resonance\"", "\"filter.futureThing\"");
        var loaded = _s.Files.ReadLibrary(edited);
        Assert.Equal(_s.Factory[0].Values["filter.resonance"], loaded.Presets[0].Values["filter.resonance"]);

        // A newer layout is refused with advice.
        var newer = text.Replace("\"layoutVersion\": 1", "\"layoutVersion\": 7");
        var ex = Assert.Throws<FormatException>(() => _s.Files.ReadLibrary(newer));
        Assert.Contains("Update Preset Studio", ex.Message);

        // Not our file at all.
        Assert.Throws<FormatException>(() => _s.Files.ReadLibrary("{\"format\":\"something-else\"}"));
        Assert.ThrowsAny<JsonException>(() => _s.Files.ReadLibrary("not json"));

        // A base preset that this firmware does not have.
        var alien = text.Replace("\"base\": \"Analog\"", "\"base\": \"Nonexistent\"");
        Assert.Contains("Nonexistent", Assert.Throws<FormatException>(() => _s.Files.ReadLibrary(alien)).Message);
    }

    [Fact]
    public void Single_presets_export_and_import_onto_a_free_pad()
    {
        var lib = new LibraryEditor(_s);
        var a = lib.AddFromFactory(_s.Factory.FindByName("Lead")!.Index)!;
        a.Name = "Shared Lead";
        var path = Path.Combine(_dir, "lead" + LibraryFile.PresetExtension);
        lib.ExportPreset(a, path);

        var other = new LibraryEditor(_s);
        var taken = other.AddFromFactory(0)!;     // occupies page 1 pad 0, where the import wants to go
        var imported = other.ImportPreset(path)!;
        Assert.Equal("Shared Lead", imported.Name);
        Assert.NotEqual((taken.Page, taken.Pad), (imported.Page, imported.Pad));
        Assert.Equal(2, other.Presets.Count);
        Assert.True(a.Values.SameAs(imported.Values));
    }

    [Fact]
    public void Saving_never_leaves_a_half_written_file()
    {
        var lib = new LibraryEditor(_s);
        lib.AddFromFactory(0);
        var path = Path.Combine(_dir, "keep.p2lib");
        lib.SaveAs(path);
        lib.AddFromFactory(1);
        lib.Save();
        Assert.False(File.Exists(path + ".tmp"));
        Assert.Equal(2, new LibraryEditor(_s).Also(l => l.Open(path)).Presets.Count);
        var fresh = new LibraryEditor(_s);
        Assert.Throws<InvalidOperationException>(() => fresh.Save());
    }

    [Fact]
    public void SaveAs_adds_the_extension_and_names_the_library()
    {
        var lib = new LibraryEditor(_s);
        lib.AddFromFactory(0);
        lib.SaveAs(Path.Combine(_dir, "stage"));
        Assert.True(File.Exists(Path.Combine(_dir, "stage" + LibraryFile.LibraryExtension)));
    }
}

internal static class Extensions
{
    public static T Also<T>(this T value, Action<T> action)
    {
        action(value);
        return value;
    }
}
