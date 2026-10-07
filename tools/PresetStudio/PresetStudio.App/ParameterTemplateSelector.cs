using System.Windows;
using System.Windows.Controls;
using PresetStudio.Editing;

namespace PresetStudio.App;

/// <summary>Picks the row layout for a value: a switch, a drop-down of choices, or a slider with a number box.</summary>
public sealed class ParameterTemplateSelector : DataTemplateSelector
{
    public DataTemplate? Numeric { get; set; }
    public DataTemplate? Toggle { get; set; }
    public DataTemplate? Choice { get; set; }

    public override DataTemplate? SelectTemplate(object item, DependencyObject container) =>
        item is ParameterViewModel p ? (p.IsToggle ? Toggle : p.IsChoice ? Choice : Numeric) : base.SelectTemplate(item, container);
}
