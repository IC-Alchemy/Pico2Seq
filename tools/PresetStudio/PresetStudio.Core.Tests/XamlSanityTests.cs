using System.Reflection;
using System.Text.RegularExpressions;
using System.Xml.Linq;
using PresetStudio.Editing;
using PresetStudio.Model;
using PresetStudio.Schema;
using PresetStudio.Shell;
using Xunit;

namespace PresetStudio.Tests;

/// <summary>
/// The WPF window cannot be started on a build machine without Windows, so these checks read its XAML as text
/// and catch the mistakes that otherwise only show up as a blank control or a crash at start-up: a resource
/// key that does not exist, a binding to a property that is not there, a click handler that was never written.
/// </summary>
public class XamlSanityTests
{
    private static readonly string AppDir = FindAppDir();

    private static string FindAppDir()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null)
        {
            var candidate = Path.Combine(dir.FullName, "PresetStudio.App");
            if (Directory.Exists(candidate)) return candidate;
            dir = dir.Parent;
        }
        throw new DirectoryNotFoundException("PresetStudio.App folder not found above the test output");
    }

    private static IEnumerable<string> XamlFiles() => Directory.GetFiles(AppDir, "*.xaml", SearchOption.AllDirectories);

    // Every type a binding in the XAML can end up on (the window's view model and everything it exposes).
    private static readonly Type[] BoundTypes =
    {
        typeof(MainViewModel), typeof(PresetEditor), typeof(ParameterViewModel), typeof(PresetItemViewModel),
        typeof(PadViewModel), typeof(HelpPanelViewModel), typeof(TabViewModel), typeof(SectionViewModel),
        typeof(PageChoice), typeof(FactoryPreset), typeof(Choice), typeof(DeviceSession), typeof(UserPreset),
        typeof(Rgb), typeof(LibraryEditor), typeof(Link.DeviceInfo),
    };

    [Fact]
    public void Every_xaml_file_is_well_formed_xml()
    {
        var files = XamlFiles().ToList();
        Assert.True(files.Count >= 8, "expected the app's XAML files");
        foreach (var f in files) XDocument.Load(f);
    }

    [Fact]
    public void Every_resource_a_xaml_file_refers_to_is_defined_somewhere()
    {
        var defined = new HashSet<string>(StringComparer.Ordinal);
        foreach (var f in XamlFiles())
            foreach (Match m in Regex.Matches(File.ReadAllText(f), "x:Key=\"([^\"]+)\"")) defined.Add(m.Groups[1].Value);
        Assert.Contains("PrimaryButton", defined);

        foreach (var f in XamlFiles())
            foreach (Match m in Regex.Matches(File.ReadAllText(f), @"\{(?:StaticResource|DynamicResource)\s+([\w\.]+)\}"))
                Assert.True(defined.Contains(m.Groups[1].Value), $"{Path.GetFileName(f)} uses resource '{m.Groups[1].Value}', which nothing defines");
        // Code-behind lookups (FindResource("...")).
        foreach (var f in Directory.GetFiles(AppDir, "*.cs", SearchOption.AllDirectories))
            foreach (Match m in Regex.Matches(File.ReadAllText(f), "FindResource\\(\"([^\"]+)\"\\)"))
                Assert.True(defined.Contains(m.Groups[1].Value), $"{Path.GetFileName(f)} looks up resource '{m.Groups[1].Value}', which nothing defines");
    }

    [Fact]
    public void Merged_dictionaries_can_see_what_they_use()
    {
        // Sibling merged dictionaries cannot see each other's keys: a file that uses keys from Styles.xaml
        // must merge Styles.xaml itself (or define them).
        var styles = File.ReadAllText(Path.Combine(AppDir, "Themes", "Styles.xaml"));
        var styleKeys = Regex.Matches(styles, "x:Key=\"([^\"]+)\"").Select(m => m.Groups[1].Value).ToHashSet();
        var templates = File.ReadAllText(Path.Combine(AppDir, "Themes", "ParameterTemplates.xaml"));
        var own = Regex.Matches(templates, "x:Key=\"([^\"]+)\"").Select(m => m.Groups[1].Value).ToHashSet();
        var used = Regex.Matches(templates, @"\{StaticResource\s+(\w+)\}").Select(m => m.Groups[1].Value).ToHashSet();
        Assert.All(used, key => Assert.True(own.Contains(key) || styleKeys.Contains(key), $"ParameterTemplates uses '{key}'"));
        if (used.Any(styleKeys.Contains))
            Assert.Contains("Styles.xaml", templates);
    }

    [Fact]
    public void Every_binding_starts_with_a_property_that_exists_on_some_view_model()
    {
        var known = new HashSet<string>(StringComparer.Ordinal);
        foreach (var t in BoundTypes)
            foreach (var p in t.GetProperties(BindingFlags.Public | BindingFlags.Instance)) known.Add(p.Name);
        // Members of framework types the XAML also binds to.
        foreach (var name in new[] { "IsChecked", "Count", "Text", "Items" }) known.Add(name);
        // The send-preview window binds to a small row record declared in its code-behind.
        foreach (var name in new[] { "Symbol", "Brush", "Detail" }) known.Add(name);

        foreach (var f in XamlFiles())
            foreach (Match m in Regex.Matches(File.ReadAllText(f), @"\{Binding\s*([^,}]*)"))
            {
                var first = m.Groups[1].Value.Trim();
                if (first.Length == 0 || first.Contains('=')) continue;             // {Binding}, or Path=/ElementName= forms
                var head = first.Split('.', '[')[0];
                Assert.True(known.Contains(head), $"{Path.GetFileName(f)}: binding '{first}' - no view model has a property '{head}'");
            }
    }

    [Fact]
    public void Two_way_bindings_target_writable_properties()
    {
        var writable = new HashSet<string>(StringComparer.Ordinal);
        foreach (var t in BoundTypes)
            foreach (var p in t.GetProperties(BindingFlags.Public | BindingFlags.Instance))
                if (p.SetMethod is { IsPublic: true }) writable.Add(p.Name);
        foreach (var f in XamlFiles())
            foreach (Match m in Regex.Matches(File.ReadAllText(f), @"\{Binding\s+([\w\.]+)[^}]*Mode=TwoWay"))
            {
                var last = m.Groups[1].Value.Split('.')[^1];
                Assert.True(writable.Contains(last) || last is "IsChecked", $"{Path.GetFileName(f)}: two-way binding to '{m.Groups[1].Value}', which has no public setter");
            }
    }

    [Fact]
    public void Every_event_handler_named_in_xaml_exists_in_the_code_behind()
    {
        var events = "Click|Drop|Closing|MouseMove|MouseEnter|GotKeyboardFocus|PreviewKeyDown|PreviewMouseLeftButtonDown|PreviewMouseMove|Loaded|Startup|DispatcherUnhandledException";
        foreach (var f in XamlFiles())
        {
            var text = File.ReadAllText(f);
            var handlers = Regex.Matches(text, $"\\s(?:{events})=\"(\\w+)\"").Select(m => m.Groups[1].Value).Distinct().ToList();
            if (handlers.Count == 0) continue;
            var codeBehind = f + ".cs";
            Assert.True(File.Exists(codeBehind), $"{Path.GetFileName(f)} names handlers but has no code-behind");
            var code = File.ReadAllText(codeBehind);
            foreach (var h in handlers)
                Assert.True(Regex.IsMatch(code, $@"\bvoid\s+{h}\s*\("), $"{Path.GetFileName(f)}: handler '{h}' is not defined in {Path.GetFileName(codeBehind)}");
        }
    }

    [Fact]
    public void Commands_named_in_xaml_exist_on_the_view_model()
    {
        var names = typeof(MainViewModel).GetProperties().Select(p => p.Name).Concat(typeof(ParameterViewModel).GetProperties().Select(p => p.Name)).ToHashSet();
        foreach (var f in XamlFiles())
            foreach (Match m in Regex.Matches(File.ReadAllText(f), @"Command=""\{Binding\s+(\w+)\}"""))
                Assert.True(names.Contains(m.Groups[1].Value), $"{Path.GetFileName(f)}: command '{m.Groups[1].Value}' does not exist");
    }

    [Fact]
    public void Dynamic_menus_are_not_bound_through_fragile_paths()
    {
        // The factory menus are built in code; a RelativeSource lookup from inside a menu popup is a classic silent failure.
        var main = File.ReadAllText(Path.Combine(AppDir, "MainWindow.xaml"));
        Assert.DoesNotContain("RelativeSource", main);
    }

    [Fact]
    public void Text_in_xaml_has_no_stray_csharp_escapes()
    {
        foreach (var f in XamlFiles())
            Assert.DoesNotMatch(@"\\u[0-9A-Fa-f]{4}", File.ReadAllText(f));
    }
}
