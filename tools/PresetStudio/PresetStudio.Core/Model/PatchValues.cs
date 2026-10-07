using PresetStudio.Schema;

namespace PresetStudio.Model;

/// <summary>Every editable value of one preset, indexed by the schema's field order.</summary>
public sealed class PatchValues
{
    private readonly float[] _values;
    public PatchSchema Schema { get; }

    public PatchValues(PatchSchema schema)
    {
        Schema = schema;
        _values = new float[schema.Fields.Count];
    }

    private PatchValues(PatchValues other)
    {
        Schema = other.Schema;
        _values = (float[])other._values.Clone();
    }

    public float this[FieldDef field]
    {
        get => _values[field.Index];
        set => _values[field.Index] = value;
    }

    public float this[string key]
    {
        get => _values[Schema[key].Index];
        set => _values[Schema[key].Index] = value;
    }

    public PatchValues Clone() => new(this);

    public void CopyFrom(PatchValues other)
    {
        if (!ReferenceEquals(other.Schema, Schema) && other._values.Length != _values.Length)
            throw new ArgumentException("values belong to a different schema");
        Array.Copy(other._values, _values, _values.Length);
    }

    public bool SameAs(PatchValues other) => _values.AsSpan().SequenceEqual(other._values);

    public IEnumerable<(FieldDef Field, float Value)> Enumerate()
    {
        foreach (var f in Schema.Fields) yield return (f, _values[f.Index]);
    }
}
