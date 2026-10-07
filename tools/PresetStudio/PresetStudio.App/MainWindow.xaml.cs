using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Media;
using PresetStudio.Editing;
using PresetStudio.Shell;

namespace PresetStudio.App;

/// <summary>
/// The window only displays <see cref="MainViewModel"/> and forwards a few gestures that XAML cannot express:
/// the factory-preset menus, hover/focus help, Enter in a number box, and dragging presets between pads.
/// </summary>
public partial class MainWindow : Window
{
    private Point _dragStart;
    private ParameterViewModel? _hovered;

    public MainWindow()
    {
        InitializeComponent();
        Loaded += (_, _) => FillFactoryMenu(NewFromFactoryMenu.Items);
    }

    private MainViewModel ViewModel => (MainViewModel)DataContext;

    // ---- menus --------------------------------------------------------------------------

    /// <summary>Appends one menu entry per factory preset, built here so the click handler is trivial and certain to work.</summary>
    private void FillFactoryMenu(ItemCollection items)
    {
        foreach (var preset in ViewModel.FactoryPresets)
        {
            var item = new MenuItem { Header = preset.Name.Replace("_", "__"), Tag = preset.Index };
            item.Click += (_, _) => ViewModel.AddFromFactory(preset.Index);
            items.Add(item);
        }
    }

    private void AddPreset_Click(object sender, RoutedEventArgs e)
    {
        var menu = new ContextMenu { PlacementTarget = (UIElement)sender, Placement = System.Windows.Controls.Primitives.PlacementMode.Top };
        menu.Items.Add(new MenuItem { Header = "Start from which factory sound?", IsEnabled = false });
        menu.Items.Add(new Separator());
        FillFactoryMenu(menu.Items);
        menu.IsOpen = true;
    }

    private void Exit_Click(object sender, RoutedEventArgs e) => Close();

    private void Window_Closing(object? sender, System.ComponentModel.CancelEventArgs e)
    {
        if (!ViewModel.ConfirmCanClose()) e.Cancel = true;
    }

    // ---- help follows the pointer and the keyboard focus -----------------------------------

    private static ParameterViewModel? ParameterAt(DependencyObject? start)
    {
        for (var d = start; d is not null; d = d is Visual or System.Windows.Media.Media3D.Visual3D ? VisualTreeHelper.GetParent(d) : LogicalTreeHelper.GetParent(d))
            if (d is FrameworkElement { DataContext: ParameterViewModel p })
                return p;
        return null;
    }

    private void Params_MouseMove(object sender, MouseEventArgs e)
    {
        var p = ParameterAt(e.OriginalSource as DependencyObject);
        if (p is null || ReferenceEquals(p, _hovered)) return;
        _hovered = p;
        ViewModel.FocusParameter(p);
    }

    private void Params_GotFocus(object sender, KeyboardFocusChangedEventArgs e)
    {
        var p = ParameterAt(e.NewFocus as DependencyObject);
        if (p is null) return;
        _hovered = p;
        ViewModel.FocusParameter(p);
    }

    private void Identity_Focus(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { Tag: string id })
        {
            _hovered = null;
            ViewModel.FocusIdentity(id);
        }
    }

    /// <summary>Enter commits a typed value right away (a text box otherwise waits until it loses focus).</summary>
    private void Params_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.Enter || e.OriginalSource is not TextBox box) return;
        BindingOperations.GetBindingExpression(box, TextBox.TextProperty)?.UpdateSource();
        box.SelectAll();
        e.Handled = true;
    }

    // ---- pad grid -----------------------------------------------------------------------

    private static PadViewModel? PadOf(object sender) => (sender as FrameworkElement)?.DataContext as PadViewModel;

    private void Pad_Click(object sender, RoutedEventArgs e)
    {
        if (PadOf(sender) is { } pad) ViewModel.PadClicked(pad);
    }

    private void Pad_PreviewMouseDown(object sender, MouseButtonEventArgs e) => _dragStart = e.GetPosition(null);

    private void Pad_PreviewMouseMove(object sender, MouseEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed || PadOf(sender) is not { Preset: { } preset }) return;
        var now = e.GetPosition(null);
        if (Math.Abs(now.X - _dragStart.X) < SystemParameters.MinimumHorizontalDragDistance &&
            Math.Abs(now.Y - _dragStart.Y) < SystemParameters.MinimumVerticalDragDistance)
            return;
        DragDrop.DoDragDrop((DependencyObject)sender, new DataObject("PresetStudio.PresetId", preset.Id.ToString()), DragDropEffects.Move);
    }

    private void Pad_Drop(object sender, DragEventArgs e)
    {
        if (PadOf(sender) is not { } pad) return;
        if (e.Data.GetData("PresetStudio.PresetId") is string text && Guid.TryParse(text, out var id))
            ViewModel.DropOnPad(id, pad);
        e.Handled = true;
    }
}
