using PresetStudio.Codec;
using PresetStudio.Display;
using PresetStudio.Help;
using PresetStudio.Model;
using PresetStudio.Rules;
using PresetStudio.Schema;

namespace PresetStudio.Editing;

/// <summary>
/// One value of the preset being edited, ready to bind to a slider, a text box or a drop-down: its current
/// value, the range it may take for this very preset, whether it applies right now (and if not, why not),
/// and how far it has strayed from the factory preset it is built on.
/// </summary>
public sealed class ParameterViewModel : ObservableObject
{
    private readonly PresetEditor _owner;
    private ResolvedField _resolved;
    private bool _enabled = true;
    private string _disabledReason = "";

    public FieldDef Field { get; }
    public HelpEntry Help { get; }

    internal ParameterViewModel(PresetEditor owner, FieldDef field, HelpEntry help)
    {
        _owner = owner;
        Field = field;
        Help = help;
        Refresh();
    }

    public string Key => Field.Key;

    /// <summary>The name shown beside the control. Recipe controls use the base preset's own names (Index, Ratio, ...).</summary>
    public string Label => Field.Key.StartsWith("recipe.macro", StringComparison.Ordinal) && _owner.UsesRecipeLabels
        ? _resolved.Label
        : Help.Title;

    public string Unit => _resolved.Unit;
    public float Min => _resolved.Min;
    public float Max => _resolved.Max;
    public bool IsToggle => Field.IsToggle;
    public bool IsChoice => Field.IsChoice;
    public bool IsNumeric => !IsToggle && !IsChoice;

    /// <summary>True when the value applies to the preset as it is now.</summary>
    public bool IsEnabled
    {
        get => _enabled;
        private set => Set(ref _enabled, value);
    }

    public string DisabledReason
    {
        get => _disabledReason;
        private set => Set(ref _disabledReason, value);
    }

    public float Value
    {
        get => _owner.Preset.Values[Field];
        set
        {
            var clamped = _owner.Limits.Clamp(Field, _owner.Preset, value);
            if (clamped == _owner.Preset.Values[Field]) { Raise(); return; }
            _owner.SetValue(this, clamped);
        }
    }

    public bool BoolValue
    {
        get => Value >= 0.5f;
        set => Value = value ? 1f : 0f;
    }

    /// <summary>The value as text with its unit, as the box would show it ("250 ms", "37 %", "Saw").</summary>
    public string Text => ValueFormatter.Format(Field, Unit, Value);

    /// <summary>Editable text; unparseable input is ignored (the binding refreshes back to the real value).</summary>
    public string EditText
    {
        get => Text;
        set
        {
            if (ValueFormatter.TryParse(Field, Unit, value, out var v)) Value = v;
            else Raise();
        }
    }

    /// <summary>Position 0..1 for a slider. Time and frequency use a logarithmic scale, as on the box.</summary>
    public double SliderPosition
    {
        get => ToPosition(Value);
        set => Value = FromPosition(value);
    }

    public int SelectedChoice
    {
        get => (int)Value;
        set => Value = value;
    }

    /// <summary>Choices the player may pick now (the Recipe engine is withheld when the base preset has no recipe).</summary>
    public IReadOnlyList<Choice> Choices => Field.Choices is null ? Array.Empty<Choice>() : _owner.AvailableChoices(Field);

    // ---- relation to the base preset --------------------------------------------------------

    public float BaseValue => _owner.BaseFactory.Values[Field];

    public string BaseText => ValueFormatter.Format(Field, Unit, BaseValue);

    /// <summary>True when this value differs from the factory preset the preset is built on.</summary>
    public bool IsChanged => Math.Abs(Value - BaseValue) > 1e-5f * Math.Max(1f, Math.Abs(BaseValue));

    /// <summary>Extra detail for this preset: the filter's cutoff in Hz, or which sequencer lane drives the value.</summary>
    public string Hint
    {
        get
        {
            var parts = new List<string>();
            if (Field.Key == "filter.cutoff")
                parts.Add($"about {ValueFormatter.FormatHertz(CutoffMap.ToHertz(_owner.BaseFactory.Cutoff, Value))} before the envelope opens it");
            var lane = _owner.BaseFactory.Lanes.FirstOrDefault(l => l.Field == Field.Key);
            if (lane is not null && lane.Lane is not ("Note" or "Octave" or "GateLength" or "Gate" or "Slide"))
                parts.Add($"On this preset the sequencer's {lane.Lane} lane ('{lane.Name}') moves this value per step");
            else if (lane is not null)
                parts.Add($"The sequencer's {lane.Lane} lane works around this value");
            return string.Join(". ", parts);
        }
    }

    /// <summary>Puts the value back to what the base preset has.</summary>
    public void ResetToBase() => Value = BaseValue;

    private System.Windows.Input.ICommand? _reset;
    /// <summary>For the small "back to base" button beside a changed value.</summary>
    public System.Windows.Input.ICommand ResetCommand => _reset ??= new Shell.RelayCommand(ResetToBase);

    internal void Refresh()
    {
        _resolved = _owner.Limits.Resolve(Field, _owner.Preset);
        IsEnabled = Field.Show.Evaluate(_owner.Context, _owner.Schema);
        DisabledReason = IsEnabled ? "" : Field.Show.WhyUnavailable(_owner.Schema);
        Raise(nameof(Label));
        Raise(nameof(Unit));
        Raise(nameof(Min));
        Raise(nameof(Max));
        Raise(nameof(Value));
        Raise(nameof(BoolValue));
        Raise(nameof(Text));
        Raise(nameof(EditText));
        Raise(nameof(SliderPosition));
        Raise(nameof(SelectedChoice));
        Raise(nameof(Choices));
        Raise(nameof(BaseValue));
        Raise(nameof(BaseText));
        Raise(nameof(IsChanged));
        Raise(nameof(Hint));
    }

    // ---- slider scale -----------------------------------------------------------------------

    private bool LogScale => Field.Log && Max > 0 && !IsToggle && !IsChoice;
    // A log scale needs a positive floor; the high-pass's 0 ("off") sits at the far left, below 20 Hz.
    private float LogFloor => Min > 0 ? Min : 20f;
    private bool HasOffStop => LogScale && Min <= 0;

    private double ToPosition(float v)
    {
        if (Max <= Min) return 0;
        if (LogScale)
        {
            if (HasOffStop && v <= 0) return 0;
            var floor = LogFloor;
            var t = Math.Log(Math.Clamp(v, floor, Max) / floor) / Math.Log(Max / floor);
            return HasOffStop ? 0.02 + 0.98 * t : t;
        }
        return Math.Clamp((v - Min) / (Max - Min), 0, 1);
    }

    private float FromPosition(double position)
    {
        var p = Math.Clamp(position, 0, 1);
        if (Max <= Min) return Min;
        if (LogScale)
        {
            if (HasOffStop)
            {
                if (p < 0.01) return Min;
                p = Math.Clamp((p - 0.02) / 0.98, 0, 1);
            }
            var floor = LogFloor;
            return (float)(floor * Math.Pow(Max / floor, p));
        }
        return (float)(Min + p * (Max - Min));
    }
}
