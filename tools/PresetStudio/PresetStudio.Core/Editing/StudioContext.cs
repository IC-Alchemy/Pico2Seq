using PresetStudio.Codec;
using PresetStudio.Help;
using PresetStudio.Model;
using PresetStudio.Rules;
using PresetStudio.Schema;
using PresetStudio.Storage;

namespace PresetStudio.Editing;

/// <summary>Everything the editor needs that does not change while it runs, loaded once.</summary>
public sealed class StudioContext
{
    public PatchSchema Schema { get; }
    public FactoryCatalog Factory { get; }
    public HelpCatalog Help { get; }
    public LimitResolver Limits { get; }
    public PresetValidator Validator { get; }
    public RecordCodec Codec { get; }
    public LibraryFile Files { get; }

    public StudioContext(PatchSchema schema, FactoryCatalog factory, HelpCatalog help)
    {
        Schema = schema;
        Factory = factory;
        Help = help;
        Limits = new LimitResolver(schema, factory);
        Validator = new PresetValidator(schema, factory, Limits);
        Codec = new RecordCodec(schema, factory);
        Files = new LibraryFile(schema, factory);
    }

    /// <summary>Loads the schema, factory presets and help text built into this program.</summary>
    public static StudioContext Create()
    {
        var schema = PatchSchema.Load();
        var factory = FactoryCatalog.Load(schema);
        var help = HelpCatalog.Load();
        var problems = help.CheckAgainst(schema);
        if (problems.Count > 0)
            throw new InvalidOperationException("The built-in help does not match the patch schema: " + string.Join("; ", problems));
        return new StudioContext(schema, factory, help);
    }
}
