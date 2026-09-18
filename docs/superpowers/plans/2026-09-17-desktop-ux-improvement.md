# Desktop UX Improvement Plan (branch `x86`)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Date:** 2026-09-17 · **Status:** drafted, not started · **Companion to:** `2026-09-17-windows-desktop-port.md` (the governing replica plan — that plan owns hardware-parity tasks 8–12; this plan does **not** duplicate them)

**Goal:** Make the WinUI desktop port feel like a finished instrument, not a Phase-1 demo: a quality shell (sizing, feedback, theming, accessibility), small read/action extensions to the engine ABI, and three desktop-native pages (Patterns, Sounds, Settings) that remove the hardware's one-encoder/one-lane-at-a-time pain.

**Architecture:** The WinUI app polls the native engine and pushes input events through the existing flat C ABI. This plan adds (a) GUI-only shell improvements, (b) ~12 new poll/inject functions in `desktop/native/src/api.{h,cpp}` — read-mostly plus mutators that are **queued onto the 1 kHz control thread** so the GUI never mutates engine state racing the control loop, and (c) new pages on a NavigationView shell with engine lifetime moved out of `MainPage` so navigation can't kill the engine.

**Tech Stack:** WinUI 3 (WindowsAppSDK 2.5.1, net10.0-windows10.0.26100.0, CommunityToolkit.Mvvm 8.4.2), C++17 engine DLL (`p2s_desktop`), Catch2 (v3.5.2, via `p2s_desktop_tests`), CMake + Ninja.

**Spec:** `docs/superpowers/plans/2026-09-17-windows-desktop-port.md` (replica direction, phases, out-of-scope), `docs/manual.md` (hardware behavior truth). Key survey facts this plan builds on are listed in "Context" below.

## Global Constraints

- All engine changes stay inside `desktop/` (`api.h`, `api.cpp`, `DesktopApp.*`, `shim_runtime.cpp`). No `src/` firmware edits are required by this plan — every engine entry point it calls already exists (verified: `Sequencer` L91/136-139/149/196, `ParameterManager` L42-51, `VoicePresets` L42/58, `VoiceSetup.h` L7, `Session.h` L20-23, `AppState.h` L28, `UIState.h` L37-74).
- The Arduino firmware build and the host test suite must stay green. Verify with the existing Ninja+clang test build (configure from repo **root**, not a stale MSVC cache): `cmake -S . -B build_test_ninja -G Ninja && cmake --build build_test_ninja && ctest --test-dir build_test_ninja --output-on-failure`.
- Desktop native tests: `cmake -S . -B build_desktop -DPICO2SEQ_BUILD_DESKTOP=ON` (once), then `cmake --build build_desktop --target p2s_desktop_tests` and `ctest --test-dir build_desktop -R desktop_tests --output-on-failure`.
- App build/run: `dotnet build desktop/app/Pico2Seq.App.csproj`, `dotnet run --project desktop/app`. If the DLL isn't found, the app expects it at `build_desktop\desktop\native` (override `-p:P2SNativeDir=`).
- Threading rule: the GUI thread calls only `p2s_*` functions. **Mutators must be posted to a queue drained by `DesktopApp::updateOnce`** (Task 5) — never mutate engine objects from the GUI thread. Getters follow the existing lock-free status-snapshot pattern.
- XAML discipline: explicit `Mode=` on every `x:Bind`; `UpdateSourceTrigger=PropertyChanged` on any two-way `TextBox.Text`; `{ThemeResource}` at usage sites; no color literals except the OLED *pixel* colors (`MainPage.xaml.cs:219-228`) and LED *on-glow* colors, which are intentional device-replica literals.
- Every interactive element gets `AutomationProperties.Name` and `AutomationProperties.AutomationId` — Task 13's UI batch tests depend on them.
- The user edits this repo concurrently: re-run `git status` before every commit; never commit unrelated in-flight changes.
- Commit style: conventional commits (`feat:`, `test:`, `docs:`), one commit per task.

## Context (verified survey facts the executor needs)

- **Current GUI** (`desktop/app/`): one page — transport `ToggleButton` + four read-only text blocks, 32 programmatic `Button` pads (pointer-only, LED glow overwrites `Background`, no automation names), OLED `WriteableBitmap` mirror, 30 Hz `DispatcherQueueTimer` poll. Engine init/shutdown happen in `MainPage.OnLoaded/OnUnloaded` (`MainPage.xaml.cs:125-155`) — navigating away would kill the engine. Window has no explicit size; title is "Pico2Seq.App"; zero `InfoBar`/`ToolTip`/`KeyboardAccelerator`/`AutomationProperties` anywhere.
- **Engine ABI** (`desktop/native/src/api.h`, 13 functions): init/shutdown/is_running, transport start/stop/running, `p2s_push_touch(uint16_t)`, `p2s_push_lidar(int)`, `p2s_poll_leds`, `p2s_poll_oled`, `p2s_get_status(P2sStatus*)`, `p2s_processed_step_count`. Pure polling; `P2sStatus` has 12 fields incl. `PadBankB`, `StepEditActive`, `VoiceEditorActive` (marshaled in C# but unused).
- **Unreachable engine capability** (compiled into the DLL, no C ABI): tempo set, step/lane editing (`Sequencer::get/setStepParameterValue`, `get/setParameterStepCount`), playhead (`Sequencer::getCurrentStep()`), presets (`VoicePresets::getPresetName/getPresetCount/getSequencerParamName`, `VoiceSetup::applyVoicePreset` — the staged click-safe path Settings pads use), randomize with depth/seed (`Sequencer::randomizeParameters(depthPercent, seed)`), reset (`Sequencer::resetAllSteps()`), save (`Session::requestSave()`), UIState notices (`oledNoticeKind/oledNoticeVoice/voicePresetIndices[4]`).
- **Engine access pattern for api.cpp**: `#include "app/AppState.h"` gives `extern Sequencer *const sequencers[VoiceSystem::MAX_VOICES];` (AppState.h:28) and `uiState` is already read by `p2s_get_status` (api.cpp:94-110). `ParamId` order: Note, Velocity, Filter, Attack, Decay, Octave, GateLength, Gate, Slide (`SequencerDefs.h:70-82`).
- **Product pain points this plan addresses** (from `docs/manual.md` + firmware survey): one encoder edits one parameter of one voice at a time; step editing = long-press + hold-param + encoder, one step at a time; preset browsing needs Settings mode + unlabeled pads; randomize depth is fixed at 35% with no per-lane control; no lane/polymeter overview; notices only on a 128×64 OLED.

---

## Phase A — Shell quality (GUI-only, no engine changes)

### Task 1: Window sizing + app identity

**Files:**
- Modify: `desktop/app/MainWindow.xaml.cs`
- Modify: `desktop/app/MainWindow.xaml:9,21`
- Modify: `desktop/app/Package.appxmanifest` (DisplayName, Description)

**Interfaces:**
- Produces: `MainWindow` sized 1240×820 DIPs at DPI on first show; display name "Pico2Seq" everywhere.

- [ ] **Step 1: Size the window in the constructor.** WinUI 3 has no `SizeToContent`; `AppWindow.Resize` takes physical pixels, so scale by the window's DPI (`XamlRoot.RasterizationScale` is null in the constructor):

```csharp
using Microsoft.UI;
using Microsoft.UI.Windowing;
using System.Runtime.InteropServices;
using Windows.Graphics;

public sealed partial class MainWindow : Window
{
    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr hWnd);

    public MainWindow()
    {
        InitializeComponent();
        // ... existing TitleBar/icon setup ...
        // Multi-pane rubric (winui-design): 1100-1300 wide; 1240x820 leaves room
        // for pad grid + OLED today and the nav shell of Task 8 tomorrow.
        var hwnd = Win32Interop.GetWindowFromWindowId(AppWindow.Id);
        double scale = GetDpiForWindow(hwnd) / 96.0;
        AppWindow.Resize(new SizeInt32((int)(1240 * scale), (int)(820 * scale)));
    }
}
```

- [ ] **Step 2: Fix the identity strings.** `Title="Pico2Seq"` in `MainWindow.xaml:9`, `Title="Pico2Seq"` on the `TitleBar` (`MainWindow.xaml:21`); in `Package.appxmanifest` set DisplayName to `Pico2Seq`, Description to `Desktop companion for the Pico2Seq sequencer` (leave PublisherIdentity alone — packaging is the port plan's Task 17).
- [ ] **Step 3: Verify.** `dotnet build desktop/app/Pico2Seq.App.csproj` then `dotnet run --project desktop/app`: window opens 1240×820 (check at 150% DPI too), title bar reads "Pico2Seq".
- [ ] **Step 4: Commit** `feat(desktop): size main window and fix app identity strings`.

### Task 2: Data-driven pad grid (accessibility, keyboard, non-destructive LED glow)

**Files:**
- Create: `desktop/app/ViewModels/PadCellViewModel.cs`
- Create: `desktop/app/Themes/P2SThemeResources.xaml`
- Modify: `desktop/app/App.xaml` (merge the dictionary)
- Modify: `desktop/app/MainPage.xaml` (pad area → `ItemsRepeater`)
- Modify: `desktop/app/MainPage.xaml.cs` (build VMs, poll updates VMs)

**Interfaces:**
- Produces: `PadCellViewModel(int index, Action<int,bool> touch)` with `Index`, `Number`, `Name`, `AutomationId`, `LedBrush` (nullable Brush, null = idle), and pointer/key handlers; `MainPage._padCells[32]` used by the poll loop instead of `_pads`.
- Consumes: nothing new from the engine (same `p2s_push_touch` / `p2s_poll_leds`).

**Why:** the 32 programmatic buttons are pointer-only (no `Click` fires on keyboard, and pressing Space does nothing because there is no handler), have no accessible names, and `UpdatePadGlow` overwrites `Button.Background`, which clobbers Fluent hover/pressed visual states and ignores theme. Idle-base color must be theme-aware; LED *on* colors stay literal (device replica).

- [ ] **Step 1: Create the theme dictionary** `desktop/app/Themes/P2SThemeResources.xaml` with Light/Dark/**HighContrast** entries (never "Default"):

```xml
<?xml version="1.0" encoding="utf-8" ?>
<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <ResourceDictionary.ThemeDictionaries>
        <ResourceDictionary x:Key="Light">
            <SolidColorBrush x:Key="P2SPadIdleBrush" Color="#14000000" />
            <SolidColorBrush x:Key="P2SOledPanelBrush" Color="#FFE4E4E8" />
        </ResourceDictionary>
        <ResourceDictionary x:Key="Dark">
            <SolidColorBrush x:Key="P2SPadIdleBrush" Color="#FF10101C" />
            <SolidColorBrush x:Key="P2SOledPanelBrush" Color="#FF101014" />
        </ResourceDictionary>
        <ResourceDictionary x:Key="HighContrast">
            <SolidColorBrush x:Key="P2SPadIdleBrush" Color="{ThemeResource SystemColorGrayTextColor}" />
            <SolidColorBrush x:Key="P2SOledPanelBrush" Color="{ThemeResource SystemColorWindowColor}" />
        </ResourceDictionary>
    </ResourceDictionary.ThemeDictionaries>
</ResourceDictionary>
```

Merge it in `App.xaml` after `XamlControlsResources`:

```xml
<ResourceDictionary.MergedDictionaries>
    <XamlControlsResources xmlns="using:Microsoft.UI.Xaml.Controls" />
    <ResourceDictionary Source="ms-appx:///Themes/P2SThemeResources.xaml" />
</ResourceDictionary.MergedDictionaries>
```

- [ ] **Step 2: Create `PadCellViewModel`** (`desktop/app/ViewModels/PadCellViewModel.cs`). Pads map row-major to the firmware matrix (`RowInputs`/`ColInputs` in `MainPage.xaml.cs:22-23`); banks: pads 0-15 = A, 16-31 = B (bank B labeling becomes voice-aware when the port plan's Phase 2 lands):

```csharp
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using CommunityToolkit.Mvvm.ComponentModel;

namespace Pico2Seq_App.ViewModels;

/// <summary>One pad of the 4x8 touch matrix. Touch callbacks are injected so
/// the VM stays engine-agnostic; MainPage owns the electrode-bit encoding.</summary>
public sealed partial class PadCellViewModel : ObservableObject
{
    private readonly Action<int, bool> _touch;

    public PadCellViewModel(int index, Action<int, bool> touch)
    {
        Index = index;
        _touch = touch;
    }

    public int Index { get; }
    public int Number => Index % 16 + 1;
    public string Name => $"Pad {Number}, bank {(Index < 16 ? "A" : "B")}";
    public string AutomationId => $"Pad{Index + 1}";

    /// <summary>LED glow from the poll loop; null leaves the theme idle fill visible.</summary>
    [ObservableProperty]
    public partial Brush? LedBrush { get; set; }

    public void OnPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        ((FrameworkElement)sender).CapturePointer(e.Pointer);
        _touch(Index, true);
    }

    public void OnPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        ((FrameworkElement)sender).ReleasePointerCapture(e.Pointer);
        _touch(Index, false);
    }

    // Keyboard activation = one full matrix close/open cycle, identical to a tap.
    public void OnKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key is Windows.System.VirtualKey.Space or Windows.System.VirtualKey.Enter)
        {
            _touch(Index, true);
            _touch(Index, false);
            e.Handled = true;
        }
    }
}
```

- [ ] **Step 3: Replace the pad area in `MainPage.xaml`** — keep the 4×8 footprint by giving the `ItemsRepeater` an exact width (8 × 52 + 7 × 6 = 458):

```xml
<ItemsRepeater x:Name="PadRepeater" Grid.Column="0"
               Width="458" HorizontalAlignment="Left" VerticalAlignment="Top"
                AutomationProperties.AutomationId="PadGrid"
                ItemsSource="{x:Bind ViewModel.Pads, Mode=OneWay}">
    <ItemsRepeater.Layout>
        <UniformGridLayout MinItemWidth="52" MinItemHeight="52"
                           MinColumnSpacing="6" MinRowSpacing="6" />
    </ItemsRepeater.Layout>
    <ItemsRepeater.ItemTemplate>
        <DataTemplate x:DataType="vm:PadCellViewModel">
            <Button Padding="0" CornerRadius="6"
                    AutomationProperties.Name="{x:Bind Name}"
                    AutomationProperties.AutomationId="{x:Bind AutomationId}"
                    PointerPressed="{x:Bind OnPointerPressed}"
                    PointerReleased="{x:Bind OnPointerReleased}"
                    PointerCanceled="{x:Bind OnPointerReleased}"
                    PointerCaptureLost="{x:Bind OnPointerReleased}"
                    KeyDown="{x:Bind OnKeyDown}">
                <Grid>
                    <!-- Theme-aware idle base under the device-literal LED glow -->
                    <Rectangle Margin="4" CornerRadius="4"
                               Fill="{ThemeResource P2SPadIdleBrush}" />
                    <Rectangle Margin="4" CornerRadius="4"
                               Fill="{x:Bind LedBrush, Mode=OneWay}" />
                    <TextBlock Text="{x:Bind Number}" FontSize="11"
                               HorizontalAlignment="Center" VerticalAlignment="Center" />
                </Grid>
            </Button>
        </DataTemplate>
    </ItemsRepeater.ItemTemplate>
</ItemsRepeater>
```

(Add `xmlns:vm="using:Pico2Seq_App.ViewModels"` to the page.) Delete `BuildPadGrid`, `_pads`, `OnPadPressed`, `OnPadReleased`, and `GetOrCreateBrush`'s pad-glow path from code-behind; keep `ElectrodeBitsFor`.

- [ ] **Step 4: Rewire code-behind.** In `MainPage`:

```csharp
// ViewModel: public ObservableCollection<PadCellViewModel> Pads { get; } = new();
// ctor, before InitializeComponent():
for (int pad = 0; pad < 32; ++pad)
    ViewModel.Pads.Add(new PadCellViewModel(pad, OnPadTouch));

private ushort _touchBits;
private void OnPadTouch(int pad, bool isDown)
{
    _touchBits = isDown
        ? (ushort)(_touchBits | ElectrodeBitsFor(pad))
        : (ushort)(_touchBits & ~ElectrodeBitsFor(pad));
    EngineApi.p2s_push_touch(_touchBits);
}
```

`UpdatePadGlow` becomes VM updates (serial-gated poll unchanged):

```csharp
private void UpdatePadGlow()
{
    for (int pad = 0; pad < 32; ++pad)
    {
        int i = pad * 3; // LED index == pad grid index until proven otherwise
        byte r = _ledBytes[i], g = _ledBytes[i + 1], b = _ledBytes[i + 2];
        _padCells[pad].LedBrush = r + g + b == 0
            ? null
            : GetOrCreateBrush(r, g, b);
    }
}
```

(`_padCells` is just an alias — index straight into `ViewModel.Pads`.)

- [ ] **Step 5: Verify.** Build and run: pads glow as before on hover-free idle; tabbing reaches pads (visible focus!), Space toggles a step's gate (verify a note sounds while playing); hover shows Fluent hover state *and* glow; switch Windows to High Contrast — idle pads and OLED bezel adapt, glowing pads stay device-colored.
- [ ] **Step 6: Commit** `feat(desktop): data-driven accessible pad grid with theme-aware idle glow`.

### Task 3: Transport feel + header labels + dead-code removal

**Files:**
- Modify: `desktop/app/ViewModels/MainPageViewModel.cs`
- Modify: `desktop/app/MainPage.xaml:18-51`

**Interfaces:**
- Produces: optimistic transport state (VM flips immediately, poll reconciles); Space = play/stop; labeled status values; `EngineText` property removed.

- [ ] **Step 1: Optimistic transport.** Today `IsChecked` is OneWay from a poll-updated property, so a click visually reverts for up to 33 ms — reads as lag:

```csharp
[RelayCommand]
private void ToggleTransport()
{
    TransportRunning = !TransportRunning;   // immediate feedback; poll reconciles
    if (TransportRunning)
        EngineApi.p2s_transport_start();
    else
        EngineApi.p2s_transport_stop();
}
```

- [ ] **Step 2: Space accelerator** on the transport button in `MainPage.xaml`:

```xml
<ToggleButton x:Name="TransportButton"
              IsChecked="{x:Bind ViewModel.TransportRunning, Mode=OneWay}"
              Command="{x:Bind ViewModel.ToggleTransportCommand}"
              AutomationProperties.AutomationId="TransportButton"
              ToolTipService.ToolTip="Play / Stop (Space)">
    <ToggleButton.KeyboardAccelerators>
        <KeyboardAccelerator Key="Space" />
    </ToggleButton.KeyboardAccelerators>
    ...
```

- [ ] **Step 3: Labeled header values.** Replace the four fused strings (`"Voice: 1"`, `"120 BPM"`) with caption/value pairs so screen readers announce label and value separately and the layout stops depending on string concatenation:

```xml
<StackPanel Orientation="Vertical" VerticalAlignment="Center">
    <TextBlock Text="Tempo" Style="{StaticResource CaptionTextBlockStyle}" />
    <TextBlock Text="{x:Bind ViewModel.TempoText, Mode=OneWay}"
               Style="{StaticResource SubtitleTextBlockStyle}" />
</StackPanel>
```

VM values become bare (`TempoText = $"{status.TempoBpm:0} BPM"`, `VoiceText = $"Voice {status.SelectedVoice + 1}"` — keep the unit in the value, drop concatenation into labels). Add `AutomationProperties.Name` on each value pair.
- [ ] **Step 4: Delete `EngineText`** (populated every poll, bound nowhere — `MainPageViewModel.cs:31-32,41`) and add the OLED accessibility name: `AutomationProperties.Name="Device OLED screen"` on `OledImage` (`MainPage.xaml:68`).
- [ ] **Step 5: Verify.** Space toggles playback from anywhere on the page; clicking Play shows the Stop state instantly; Narrator announces "Tempo, 90 BPM".
- [ ] **Step 6: Commit** `feat(desktop): spacebar transport, optimistic state, labeled header values`.

### Task 4: Error feedback, startup state, live region

**Files:**
- Modify: `desktop/app/MainPage.xaml` (InfoBar row)
- Modify: `desktop/app/MainPage.xaml.cs` (start/retry logic, ProgressRing)

**Interfaces:**
- Produces: `StartEngine()` (idempotent — checks `p2s_is_running`), `OnRetryEngine` handler; `EngineBar` InfoBar with AutomationId `EngineBar` (Task 13 tests it).

**Why:** engine failure is currently one gray caption ("engine FAILED to start") with no recovery path; there is no startup indicator; status text is invisible to assistive tech.

- [ ] **Step 1: Add the InfoBar** under the header row (row 1 becomes a content presenter; shift status caption to row 2):

```xml
<InfoBar x:Name="EngineBar" Grid.Row="1" Severity="Error" IsOpen="False" IsClosable="True"
         Title="Audio engine" AutomationProperties.AutomationId="EngineBar">
    <InfoBar.ActionButton>
        <Button Content="Retry" Click="OnRetryEngine"
                AutomationProperties.AutomationId="EngineRetry" />
    </InfoBar.ActionButton>
</InfoBar>
```

- [ ] **Step 2: Restructure startup** so it can be retried:

```csharp
private void StartEngine()
{
    if (EngineApi.p2s_is_running() != 0)
    {
        ViewModel.StatusLine = "engine running";
        return;
    }
    ViewModel.StatusLine = "engine starting";
    try
    {
        var options = new EngineApi.InitOptions
        {
            StorageDir = IntPtr.Zero, StartAudioDevice = true, StartControlThread = true,
        };
        int rc = EngineApi.p2s_init(options);
        ViewModel.StatusLine = rc == 0 ? "engine running" : $"engine failed to start (code {rc})";
        EngineBar.IsOpen = rc != 0;
        EngineBar.Message = rc == 0 ? "" : $"p2s_init returned {rc}. Check no other instance is running.";
    }
    catch (Exception ex)
    {
        ViewModel.StatusLine = "engine error";
        EngineBar.IsOpen = true;
        EngineBar.Message = ex.Message;
    }
}
```

`OnLoaded` calls `StartEngine()` then starts the poll timer **only on success**; `OnRetryEngine` closes the bar and calls `StartEngine()` (+ timer start on success). Guard `PollEngine` with `if (EngineApi.p2s_is_running() == 0) return;`.
- [ ] **Step 3: Live region.** On the status caption: `AutomationProperties.LiveSetting="Polite"` so Narrator announces transport/engine changes.
- [ ] **Step 4: Verify.** Temporarily rename the DLL → build/run → InfoBar shows with a working Retry (restore DLL → Retry recovers); Narrator announces "engine running". Revert the rename.
- [ ] **Step 5: Commit** `feat(desktop): engine InfoBar with retry, live-region status`.

---

## Phase B — Engine ABI extensions (native, TDD)

All new functions go in `desktop/native/src/api.{h,cpp}`; tests in a new `desktop/native/tests/test_api_extensions.cpp` added to the `p2s_desktop_tests` target in `desktop/native/CMakeLists.txt:156-160`. Model tests on `tests/test_headless.cpp` (init with `startAudioDevice=false, startControlThread=false`, pump `DesktopApp::updateOnce()` manually).

### Task 5: Mutator queue + playhead/preset/notice getters

**Files:**
- Modify: `desktop/native/src/DesktopApp.h`, `desktop/native/src/DesktopApp.cpp` (queue + drain)
- Modify: `desktop/native/src/shim_runtime.cpp` (queue singleton) — or a new `desktop/native/src/ApiQueue.{h,cpp}`
- Modify: `desktop/native/src/api.h`, `desktop/native/src/api.cpp`
- Test: `desktop/native/tests/test_api_extensions.cpp` (new)
- Modify: `desktop/app/Native/EngineApi.cs`

**Interfaces:**
- Produces (C ABI, all return 0 on success / -1 on bad args unless noted):

```c
// Read-only, lock-free (same pattern as p2s_get_status):
int  p2s_get_playhead(int voice);      // Sequencer::getCurrentStep(), 0..63, -1 if voice invalid
int  p2s_get_voice_preset(int voice);  // uiState.voicePresetIndices[voice], -1 if invalid
int  p2s_poll_notice(int *outKind, int *outVoice);
     // 1 if a UIState notice is currently active (oledNoticeUntil > now),
     // with kind = UIState::OledNoticeKind value (UIState.h:37) and 0-based voice;
     // 0 if none. Does not clear the notice — the OLED is the owner.

// Mutators — safe from any thread, executed on the control thread:
void p2s_apply_preset(int voice, int presetIndex);  // Task 7, declared here for the queue design
void p2s_randomize_voice(int voice, int depthPercent, long long seed);
void p2s_reset_voice(int voice);
void p2s_request_save(void);
```

- Produces (C#): `p2s_get_playhead`, `p2s_get_voice_preset`, `p2s_poll_notice` imports in `EngineApi.cs`.

**Why the queue:** firmware mutators (preset apply, randomize) run *inside* the control loop's matrix scan; calling `applyVoicePreset` from the GUI thread would race `updateOnce`. The port plan's InputRouter will route gesture input the same way — build the queue once here.

- [ ] **Step 1: Write the failing test** (new `test_api_extensions.cpp`):

```cpp
#include "api.h"
#include "DesktopApp.h"
#include "app/AppState.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("playhead getter tracks the sequencer", "[api]")
{
    REQUIRE(p2s_init(nullptr) == 0);
    REQUIRE(p2s_get_playhead(0) == 0);
    REQUIRE(p2s_get_playhead(-1) == -1);
    REQUIRE(p2s_get_playhead(4) == -1);
    p2s_transport_start();
    for (int i = 0; i < 500; ++i)
        DesktopApp::updateOnce();          // headless pump, mirrors test_headless.cpp
    p2s_transport_stop();
    // With the audio device off the clock advance path depends on the offline
    // pump, so assert identity + bounds, not wall-clock progress (see Step 5).
    int ph = p2s_get_playhead(0);
    REQUIRE(ph >= 0);
    REQUIRE(ph < 64);
    REQUIRE(ph == sequencers[0]->getCurrentStep());
    p2s_shutdown();
}
```

Run: `cmake --build build_desktop --target p2s_desktop_tests && ctest --test-dir build_desktop -R desktop_tests --output-on-failure` — expect **link failure** (symbols don't exist).
- [ ] **Step 2: Add the queue.** In `shim_runtime.cpp` (or new `ApiQueue.cpp`):

```cpp
namespace p2s::host {
namespace {
std::mutex g_apiMu;
std::vector<std::function<void()>> g_apiQueue;
}
void postApiAction(std::function<void()> f)
{
    std::lock_guard<std::mutex> lock(g_apiMu);
    g_apiQueue.push_back(std::move(f));
}
void drainApiActions()
{
    std::vector<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lock(g_apiMu);
        local.swap(g_apiQueue);
    }
    for (auto &f : local) f();
}
}
```

Declare both in `shims/p2s_host_hooks.h` (the existing desktop↔shim seam). Call `p2s::host::drainApiActions();` as the first statement of `DesktopApp::updateOnce` (DesktopApp.cpp) — autosave/Session deferred actions already follow this "consume in updateOnce" pattern (DesktopApp.cpp:138-187).
- [ ] **Step 3: Implement the getters** in `api.cpp` (it already includes `app/AppState.h`):

```cpp
int p2s_get_playhead(int voice)
{
    if (voice < 0 || voice >= VoiceSystem::MAX_VOICES) return -1;
    return sequencers[voice]->getCurrentStep();
}

int p2s_get_voice_preset(int voice)
{
    if (voice < 0 || voice >= VoiceSystem::MAX_VOICES) return -1;
    return uiState.voicePresetIndices[voice];
}

int p2s_poll_notice(int *outKind, int *outVoice)
{
    if (uiState.oledNoticeUntil > millis())
    {
        if (outKind) *outKind = static_cast<int>(uiState.oledNoticeKind);
        if (outVoice) *outVoice = uiState.oledNoticeVoice;
        return 1;
    }
    return 0;
}
```

(`millis()` is available via the Arduino shim; `uiState` is the same extern `p2s_get_status` already reads.)
- [ ] **Step 4: Implement stub mutators** (bodies land in Task 7; declare now so the C# side can bind once): post-to-queue wrappers calling the real engine calls listed in Task 7.
- [ ] **Step 5: Run tests** — the suite including `test_headless` and `test_host_clock` must pass. If the playhead test is flaky on timing, assert `get_playhead(0)` is in `0..63` after start and that it equals the value from `sequencers[0]->getCurrentStep()` directly (identity, not timing).
- [ ] **Step 6: C# mirror** in `EngineApi.cs`:

```csharp
[DllImport(Dll)] public static extern int p2s_get_playhead(int voice);
[DllImport(Dll)] public static extern int p2s_get_voice_preset(int voice);
[DllImport(Dll)] public static extern int p2s_poll_notice(out int kind, out int voice);
```

- [ ] **Step 7: Commit** `feat(desktop): api queue + playhead/preset/notice getters with tests`.

### Task 6: Pattern read/write + lane names/lengths

**Files:**
- Modify: `desktop/native/src/api.h`, `desktop/native/src/api.cpp`
- Test: `desktop/native/tests/test_api_extensions.cpp`
- Modify: `desktop/app/Native/EngineApi.cs`

**Interfaces:**
- Produces:

```c
// Pattern memory: MAX_STEPS_COUNT is 64 (SequencerDefs.h:20); the UI surfaces 16.
// Caller passes out64 with capacity >= 64. *outStepCount receives the lane's
// active length (its polymeter length).
int p2s_get_pattern(int voice, int paramId, float *out64, int *outStepCount);
int p2s_set_step_value(int voice, int paramId, int stepIdx, float value);
     // Sequencer::setStepParameterValue — clamps 0..stepCount, GUI clamps stepIdx < 16
int p2s_set_lane_length(int voice, int paramId, int stepCount);
     // Sequencer::setParameterStepCount, clamped 2..16 (UI parity)
int p2s_get_lane_name(int voice, int paramId, char *out, int capacity);
     // VoicePresets::getSequencerParamName(uiState.voicePresetIndices[voice], (ParamId)paramId)
     // — lane names are preset-specific (Bright/Pick/T60 on waveguide, etc.)
```

- Produces (C#): `p2s_get_pattern(int, int, float[], out int)`, `p2s_set_step_value`, `p2s_set_lane_length`, `p2s_get_lane_name(int, int, System.Text.StringBuilder, int)`.

- [ ] **Step 1: Write the failing test:**

```cpp
TEST_CASE("pattern round-trip and lane metadata", "[api]")
{
    REQUIRE(p2s_init(nullptr) == 0);
    float lane[64]; int len = 0;
    REQUIRE(p2s_get_pattern(1, 0 /*Note*/, lane, &len) == 0);
    REQUIRE(len >= 2 && len <= 16);
    REQUIRE(p2s_set_step_value(1, 0, 3, 7.0f) == 0);
    REQUIRE(p2s_get_pattern(1, 0, lane, &len) == 0);
    REQUIRE(lane[3] == 7.0f);
    REQUIRE(p2s_set_step_value(1, 0, 16, 1.0f) == -1);   // UI clamps to 16 steps
    REQUIRE(p2s_set_lane_length(1, 2 /*Filter*/, 8) == 0);
    REQUIRE(p2s_get_pattern(1, 2, lane, &len) == 0);
    REQUIRE(len == 8);
    char name[32];
    REQUIRE(p2s_get_lane_name(1, 2, name, sizeof(name)) == 0);
    REQUIRE(name[0] != '\0');
    REQUIRE(p2s_get_pattern(4, 0, lane, &len) == -1);    // voice bounds
    REQUIRE(p2s_get_pattern(0, 9, lane, &len) == -1);    // ParamId::Count == 9
    p2s_shutdown();
}
```

- [ ] **Step 2: Implement** using `sequencers[voice]->get/setStepParameterValue((ParamId)paramId, stepIdx)` and `get/setParameterStepCount`; validate `voice ∈ [0,4)`, `paramId ∈ [0,9)`, `stepIdx ∈ [0,16)`, `stepCount ∈ [2,16]`. `p2s_get_lane_name` uses `std::snprintf`-style copy via `const char*` from `VoicePresets::getSequencerParamName` (include `voice/VoicePresets.h`).
- [ ] **Step 3: Run the suite — pass. C# mirror, commit** `feat(desktop): pattern read/write + lane name/length API with tests`.

### Task 7: Preset / randomize / reset / save actions

**Files:**
- Modify: `desktop/native/src/api.h`, `desktop/native/src/api.cpp`
- Test: `desktop/native/tests/test_api_extensions.cpp`
- Modify: `desktop/app/Native/EngineApi.cs`

**Interfaces:**
- Produces (mutators from Task 5 now real; all post onto the queue):

```c
int  p2s_get_preset_count(void);                       // VoicePresets::getPresetCount()
int  p2s_get_preset_name(int presetIndex, char *out, int capacity);
     // VoicePresets::getPresetName, -1 if index out of range
// void p2s_apply_preset(int voice, int presetIndex)   — declared in Task 5, body here:
     // mirrors UIEventHandler.cpp:470-490: bounds-check, uiState.voicePresetIndices[voice]=idx,
     // then VoiceSetup::applyVoicePreset(voice, idx) (staged, click-safe, transport-safe)
// void p2s_randomize_voice(int voice, int depthPercent, long long seed)
     // sequencers[voice]->randomizeParameters(clamp(depthPercent,0,100), (uint64_t)seed)
// void p2s_reset_voice(int voice)
     // sequencers[voice]->resetAllSteps()  (the firmware long-press reset, ButtonHandlers path)
// void p2s_request_save(void)
     // Session::requestSave(); consumed by the existing updateOnce deferred-I/O block
```

- Produces (C#): `p2s_get_preset_count`, `p2s_get_preset_name`, `p2s_apply_preset`, `p2s_randomize_voice`, `p2s_reset_voice`, `p2s_request_save`.

- [ ] **Step 1: Write the failing test:**

```cpp
TEST_CASE("preset/randomize/reset actions", "[api]")
{
    REQUIRE(p2s_init(nullptr) == 0);
    REQUIRE(p2s_get_preset_count() == 29);
    char name[48];
    REQUIRE(p2s_get_preset_name(0, name, sizeof(name)) == 0);
    REQUIRE(name[0] != '\0');
    REQUIRE(p2s_get_preset_name(29, name, sizeof(name)) == -1);

    p2s_apply_preset(2, 0);
    p2s_randomize_voice(2, 35, 12345);
    REQUIRE(p2s_get_voice_preset(2) == 0);
    // Deterministic seed: two randomize runs from the same state differ from a
    // different seed on at least one non-Gate lane value.
    p2s_reset_voice(2);
    float lane[64]; int len;
    p2s_get_pattern(2, 1 /*Velocity*/, lane, &len);
    p2s_shutdown();
}

TEST_CASE("request_save writes session file", "[api]")
{
    REQUIRE(p2s_init(nullptr) == 0);
    p2s_request_save();
    for (int i = 0; i < 50; ++i) DesktopApp::updateOnce();   // drain + deferred I/O
    // Default storage dir is %APPDATA%\Pico2Seq (DesktopStorage.cpp:34-42); the
    // test passes a storageDir override through P2sInitOptions — assert
    // session.p2s exists in that dir (mirror test_headless.cpp's storage setup).
    p2s_shutdown();
}
```

- [ ] **Step 2: Implement the five mutators** as `p2s::host::postApiAction` lambdas (queue from Task 5) plus the two read getters. Include `app/VoiceSetup.h`, `voice/VoicePresets.h`, `app/Session.h` in api.cpp.
- [ ] **Step 3: Run the suite — pass. C# mirror, commit** `feat(desktop): preset/randomize/reset/save action API with tests`.

---

## Phase C — Desktop-native pages

### Task 8: NavigationView shell + engine lifetime out of `MainPage`

**Files:**
- Modify: `desktop/app/MainWindow.xaml`, `MainWindow.xaml.cs`
- Create: `desktop/app/PatternsPage.xaml` + `.cs`, `desktop/app/SoundsPage.xaml` + `.cs`, `desktop/app/SettingsPage.xaml` + `.cs` (stubs with a header + "coming in Task N" body — filled by Tasks 9-11)
- Modify: `desktop/app/MainPage.xaml.cs` (remove engine lifecycle; keep poll)

**Interfaces:**
- Produces: `MainWindow.EngineReady` instance bool + `MainWindow.Current` static accessor (pages poll only when true); nav items with AutomationIds `NavStudio`, `NavPatterns`, `NavSounds`, `NavSettings`; `MainPage` (Studio) is the replica panel and keeps transport/pads/OLED.

**Why:** `MainPage.OnUnloaded` currently calls `p2s_shutdown` (`MainPage.xaml.cs:151-155`) — the moment a second page exists, navigating away kills the engine. Engine lifetime belongs to the window; page lifetime to the Frame.

- [ ] **Step 1: Move the engine.** `MainWindow.xaml.cs`: call `StartEngine()`-equivalent (move Task 4's `StartEngine` logic + retry InfoBar to the window — the InfoBar moves to `MainWindow.xaml` above the nav view) in the constructor after `InitializeComponent`; shutdown in `Closed`:

```csharp
public bool EngineReady { get; private set; }

public MainWindow()
{
    InitializeComponent();
    // ... sizing from Task 1 ...
    StartEngine();
    RootFrame.Navigate(typeof(MainPage));
    Closed += (_, _) => { if (EngineApi.p2s_is_running() != 0) EngineApi.p2s_shutdown(); };
}
```

`MainPage.OnLoaded` keeps only the poll timer (guard `PollEngine` on `MainWindow.Current.EngineReady` — add `public static MainWindow Current { get; private set; }` set in the constructor); `OnUnloaded` shrinks to stopping the timer.
- [ ] **Step 2: Add the NavigationView** in `MainWindow.xaml` (replacing the bare Frame as row 1 content; the TitleBar row stays):

```xml
<NavigationView x:Name="Nav" Grid.Row="1" PaneDisplayMode="Left"
                IsBackButtonVisible="Collapsed" IsSettingsVisible="False"
                OpenPaneLength="148" SelectionChanged="OnNavSelectionChanged"
                AutomationProperties.AutomationId="MainNav">
    <NavigationView.MenuItems>
        <NavigationViewItem Content="Studio" Tag="studio" IsSelected="True"
                            AutomationProperties.AutomationId="NavStudio">
            <NavigationViewItem.Icon><FontIcon Glyph="&#xE8D5;" /></NavigationViewItem.Icon>
        </NavigationViewItem>
        <NavigationViewItem Content="Patterns" Tag="patterns"
                            AutomationProperties.AutomationId="NavPatterns">
            <NavigationViewItem.Icon><FontIcon Glyph="&#xE71D;" /></NavigationViewItem.Icon>
        </NavigationViewItem>
        <NavigationViewItem Content="Sounds" Tag="sounds"
                            AutomationProperties.AutomationId="NavSounds">
            <NavigationViewItem.Icon><FontIcon Glyph="&#xE90B;" /></NavigationViewItem.Icon>
        </NavigationViewItem>
        <NavigationViewItem Content="Settings" Tag="settings"
                            AutomationProperties.AutomationId="NavSettings">
            <NavigationViewItem.Icon><FontIcon Glyph="&#xE713;" /></NavigationViewItem.Icon>
        </NavigationViewItem>
    </NavigationView.MenuItems>
    <Frame x:Name="RootFrame" />
</NavigationView>
```

```csharp
private void OnNavSelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
{
    if (args.SelectedItem is not NavigationViewItem item || item.Tag is not string tag) return;
    Type page = tag switch
    {
        "patterns" => typeof(PatternsPage),
        "sounds" => typeof(SoundsPage),
        "settings" => typeof(SettingsPage),
        _ => typeof(MainPage),
    };
    if (RootFrame.CurrentSourcePageType != page)
        RootFrame.Navigate(page);
}
```

(`RootFrame` moves inside the NavView; remove the old row-1 Frame.) Studio remains the landing page — the replica is the primary surface per the port plan.
- [ ] **Step 3: Stub pages** — each with `x:Class="Pico2Seq_App.<Name>"`, a page header (`TextBlock` `TitleTextBlockStyle`), and `AutomationProperties.AutomationId`. Patterns/Sounds stubs say "Wired up in the next tasks"; Settings stub gets content in Task 11.
- [ ] **Step 4: Verify.** Navigate to each page and back — audio keeps playing (start transport on Studio first); no engine restart on navigation; back button on mouse works via Frame? (NavigationView without back button — fine.)
- [ ] **Step 5: Commit** `feat(desktop): navigation shell, engine lifetime at window level`.

### Task 9: Patterns page — lane overview + step inspector

**Files:**
- Modify: `desktop/app/PatternsPage.xaml` + `.cs`
- Create: `desktop/app/ViewModels/PatternsViewModel.cs` (with `LaneViewModel`, `StepCellViewModel` nested or sibling files)

**Interfaces:**
- Consumes: `p2s_get_pattern`, `p2s_set_step_value`, `p2s_set_lane_length`, `p2s_get_lane_name`, `p2s_get_playhead` (Tasks 5-6), existing poll timer pattern.
- Produces: page AutomationId `PatternsPage`; per-step buttons `Step_{voice}_{param}_{step}`.

**Design:** fixes the hardware's biggest pain — one lane, one step at a time. 9 lanes (Note, Velocity, Filter, Attack, Decay, Octave, GateLength, Gate, Slide) × 16 steps of the selected voice, each lane with its live polymeter length and playhead, editable in place. The inspector shows one step's full lane values at once (the hardware does long-press + hold-param + encoder *per value*).

- [ ] **Step 1: ViewModels.**

```csharp
public sealed partial class StepCellViewModel : ObservableObject
{
    public int Param { get; init; }
    public int Step { get; init; }
    public string AutomationId => $"Step_{Voice}_{Param}_{Step}";
    public int Voice { get; init; }
    public bool IsSwitch => Param is 7 or 8;          // Gate, Slide

    [ObservableProperty] public partial float Value { get; set; }        // 0..1 (Note: scale steps /36)
    [ObservableProperty] public partial bool IsOn { get; set; }          // Gate/Slide
    [ObservableProperty] public partial bool IsPlayhead { get; set; }
    [ObservableProperty] public partial bool IsActive { get; set; }      // step < lane length
    [ObservableProperty] public partial bool IsSelected { get; set; }
}
```

`LaneViewModel`: `Param`, `Name` (from `p2s_get_lane_name`), `Steps` (`StepCellViewModel[16]`), `Length` (observable int, 2-16), `IsSwitchLane`.
- [ ] **Step 2: Page layout** (XAML): voice `SelectorBar` (V1-V4, AutomationId `PatternsVoice`); a 9-row lane grid built in code-behind per the existing pad-grid technique (familiar in this codebase, avoids nested `ItemsRepeater` x:Bind subtleties): each row = lane name `TextBlock` (110px) + 16 cells (34px) + `NumberBox` length editor (Value=`Length`, Min=2, Max=16, SpinButtonPlacementMode=Inline, AutomationId `Len_{param}`, `UpdateSourceTrigger=PropertyChanged` via `ValueChanging`→`p2s_set_lane_length`). Cell visuals: switch lanes = `ToggleButton` checked bound to `IsOn`; value lanes = `Button` containing a bottom-aligned `Rectangle` whose `Height = Value * 28` (opacity 0.9); inactive steps (≥ lane length) 40% opacity; `IsPlayhead` → 2px accent `Border` overlay using `SystemAccentColor`. Click a cell → inspector row (bottom `Expander`, AutomationId `StepInspector`) shows the 16-step strip of that lane with a `Slider` (0-1, tick 0.05) writing `p2s_set_step_value` on `ValueChanged` (Note lane: `NumberBox` 0-36, integer).
- [ ] **Step 3: Poll integration.** The page owns a 33 ms timer while visible (started `OnLoaded`, stopped `OnUnloaded`): each tick read `p2s_get_playhead(voice)` and the 9 lanes of the *visible* voice (`p2s_get_pattern` × 9 ≈ 2.3 KB — cheap) and update VMs. Skip writes-while-editing: if a `Slider` has focus on the selected step, don't overwrite that cell's `Value` that tick (compare to last pushed value).
- [ ] **Step 4: Verify.** Toggle Gate cells → notes sound in sync with Studio; drag a Filter slider → audible while playing; set Note lane length to 5 → lane playhead wraps at 5 while Gate still runs 16 (polymeter, visible!); preset-specific lane names show (load a waveguide preset in Sounds later → names become Bright/Pick/T60).
- [ ] **Step 5: Commit** `feat(desktop): Patterns page - 9-lane polymeter overview and step inspector`.

### Task 10: Sounds page — preset grid + randomizer with real depth control

**Files:**
- Modify: `desktop/app/SoundsPage.xaml` + `.cs`
- Create: `desktop/app/ViewModels/SoundsViewModel.cs`
- Modify: `desktop/app/Themes/P2SThemeResources.xaml` (only if a new brush is needed — prefer built-ins)

**Interfaces:**
- Consumes: `p2s_get_preset_count/name`, `p2s_apply_preset`, `p2s_randomize_voice`, `p2s_reset_voice`, `p2s_get_voice_preset` (Task 7), `App.Current.Window` statics for dialogs (`App.xaml.cs:22-37` already exposes them).
- Produces: AutomationIds `PresetTile_{n}` (1-29), `PresetVoiceSelector`, `RandomizeDepth`, `RandomizeButton`, `ResetVoiceButton`, `ClearAllButton`.

**Design:** replaces "Stop → Settings → unlabeled pads apply presets" and the fixed-35% randomizer. This page is *desktop-native*, but every action routes through the firmware's own functions (staged preset apply, depth/seed randomize), so behavior parity is preserved by construction.

- [ ] **Step 1: Preset grid.** On page load, fetch names 0-28 into `ObservableCollection<PresetItem>` (`Index`, `Name`, `IsCurrent` per selected voice). XAML: `ItemsRepeater` + `UniformGridLayout` (MinItemWidth 168, MinItemHeight 44) of `ToggleButton`-styled tiles (AutomationId `PresetTile_{Index+1}`, `ToolTipService.ToolTip="Apply to Voice N"`), `IsCurrent` → accent border; click → `p2s_apply_preset(selectedVoice, index)` + transient `InfoBar` (Severity Success, auto-close 2 s): "Preset '{name}' applied to Voice {n}".
- [ ] **Step 2: Voice selector.** `RadioButtons` (Voice 1-4, AutomationId `PresetVoiceSelector`) bound to `SelectedVoice`; selection refreshes tile `IsCurrent` from `p2s_get_voice_preset`.
- [ ] **Step 3: Randomizer card.** `Slider` 0-100 (default **35** = `ParameterManager::kDefaultRandomizeDepth`, AutomationId `RandomizeDepth`, header "Randomize depth %"), optional `NumberBox` seed (0 = random-from-clock, tooltip "0 = random; any other value repeats the same result"), `Button` Randomize (AutomationId `RandomizeButton`) → `p2s_randomize_voice(selectedVoice, depth, seed)`; `Button` "Reset steps" → `ContentDialog` (title "Reset Voice {n} steps?", body names the voice and preset, primary "Reset" / secondary "Cancel" — verb-labelled, destructive) → `p2s_reset_voice`; `Button` "Reset all voices" → same dialog pattern with count in body → 4× `p2s_reset_voice`.
- [ ] **Step 4: Help note.** Caption under the card: "Gate and Slide lanes are never randomized — rhythm stays yours (firmware rule, ParameterManager.cpp:36-41)."
- [ ] **Step 5: Verify.** Apply preset 5 to Voice 2 → OLED (Studio) shows updated sound; randomize at 100 twice with seed 7 → identical patterns (re-check via Patterns page); Reset with transport running doesn't glitch audio (queue path).
- [ ] **Step 6: Commit** `feat(desktop): Sounds page - named preset grid and depth-controlled randomizer`.

### Task 11: Settings page + session files + manifest hygiene

**Files:**
- Modify: `desktop/app/SettingsPage.xaml` + `.cs`
- Modify: `desktop/app/Package.appxmanifest` (remove `systemai:Capability Name="systemAIModels"` at :51)

**Interfaces:**
- Consumes: `p2s_request_save` (Task 7), `App.Window`/`App.WindowHandle` statics for pickers, storage default `%APPDATA%\Pico2Seq\session.p2s` (DesktopStorage.cpp:34-42, 73).
- Produces: Settings cards (SettingsExpander pattern, developer-tool silhouette); file pickers.

- [ ] **Step 1: Appearance card.** `RadioButtons` Theme: System / Light / Dark → set `((FrameworkElement)App.Window.Content).RequestedTheme` (default = `Default`); persists via `ApplicationData.Current.LocalSettings`.
- [ ] **Step 2: Storage card.** Show resolved storage dir (text, selectable); "Open folder" button → `Process.Start(new ProcessStartInfo { FileName = dir })` (unpackaged-safe); "Save now" → `p2s_request_save()` + InfoBar "Session saved" (the engine autosaves ~1 s after Stop anyway — DesktopApp.cpp:189-209 — say so in the caption).
- [ ] **Step 3: Session file card.** "Export session as…" → `FileSavePicker` (`.p2s`, suggested name `session.p2s`); "Import session…" → `FileOpenPicker` + copy over the storage path. **Guard:** if `p2s_transport_running()` (declare the import in `EngineApi.cs` — it exists, EngineApi.cs:44, currently unused), show an `InfoBar` warning "Stop transport first" instead of copying; the file is byte-compatible with the hardware's LittleFS session (DesktopStorage.cpp:1-5) — say that in the card caption ("Files interchange with the Pico 2 device").
- [ ] **Step 4: Keyboard map card.** Static list: Space = play/stop; pad Space/Enter = toggle gate; more arrive with port-plan Phase 2.
- [ ] **Step 5: Manifest.** Delete the `systemAIModels` capability line; Description already fixed in Task 1.
- [ ] **Step 6: Verify.** Theme switch is immediate and survives restart; export → delete `%APPDATA%\Pico2Seq\session.p2s` → import → state restored; export while playing is blocked with the InfoBar.
- [ ] **Step 7: Commit** `feat(desktop): settings page, session export/import, manifest cleanup`.

### Task 12: First-run onboarding + gesture tooltips

**Files:**
- Modify: `desktop/app/MainPage.xaml` + `.cs` (TeachingTip)
- Modify: `desktop/app/ViewModels/PadCellViewModel.cs` (tooltip text)

**Interfaces:**
- Produces: non-targeted `TeachingTip` on first run (LocalSettings flag `FirstRunTipSeen`), AutomationId `FirstRunTip`.

- [ ] **Step 1: TeachingTip** (non-blocking — never a dialog) shown once from `MainPage.OnLoaded` when the flag is unset:

```xml
<TeachingTip x:Name="FirstRunTip" IsOpen="False" AutomationProperties.AutomationId="FirstRunTip"
             Title="This is your Pico2Seq panel"
             PreferredPlacement="Bottom">
    The pad grid and screen mirror the hardware 1:1 — tap pads to toggle steps,
    hold to edit them (once pad holds land in the port). For desktop-speed editing
    use Patterns; for sounds and the randomizer use Sounds.
</TeachingTip>
```

- [ ] **Step 2: Gesture tooltips** on existing controls: OLED border ("Live view of the device screen"), transport ("Play / Stop (Space)"), pads ("Tap: toggle step · long-press: step edit · Shift: clear" — phrase per `docs/manual.md`; keep the unimplemented clauses out until the port plan adds them — today: "Tap: toggle step · Space/Enter: toggle").
- [ ] **Step 3: Verify.** Delete LocalSettings flag → tip shows once, closes, never returns; tooltips present on hover and via Narrator.
- [ ] **Step 4: Commit** `feat(desktop): first-run TeachingTip and gesture tooltips`.

---

## Phase D — Verification & docs

### Task 13: WinUI UI batch tests

**Files:**
- Create: `desktop/app/UITests/uitests.md` + batch script per the winui-ui-testing skill
- Modify (only if gaps found): pages missing AutomationIds

**Interfaces:**
- Consumes: AutomationIds from Tasks 2-12 (`Pad1..Pad32`, `TransportButton`, `EngineBar`, `EngineRetry`, `MainNav`, `NavStudio/NavPatterns/NavSounds/NavSettings`, `PatternsVoice`, `Step_*`, `Len_*`, `StepInspector`, `PresetTile_1..29`, `PresetVoiceSelector`, `RandomizeDepth`, `RandomizeButton`, `ResetVoiceButton`, `ClearAllButton`, `FirstRunTip`).

- [ ] **Step 1:** Load the winui:winui-ui-testing skill; generate the batch script asserting: transport toggles via Space (`TransportButton` `ToggleState`), pad `Pad5` toggles gate (LED/OLED changes), navigation to all four pages, preset tile 3 applies (`p2s_get_voice_preset` reflected in tile state), randomize depth slider value persists, export-while-playing blocked InfoBar appears.
- [ ] **Step 2:** Run all tests in one pass; fix any selector/layout fallout; re-run to green.
- [ ] **Step 3: Commit** `test(desktop): WinUI UI batch suite`.

### Task 14: Docs + full-suite verification

**Files:**
- Create: `docs/desktop-app.md` (user guide: pages, keyboard map, session file interchange with hardware, desktop-only features vs replica)
- Modify: `README.md` (short desktop section + link), `docs/superpowers/plans/2026-09-17-windows-desktop-port.md` (one-line cross-reference to this plan)

- [ ] **Step 1:** Write `docs/desktop-app.md` — audience: a musician; cover the three pages, what stays hardware-faithful (Studio) vs desktop-native (Patterns/Sounds/Settings), and the session.p2s interchange caveat (stop transport before import/export).
- [ ] **Step 2:** Run everything: `ctest --test-dir build_desktop -R desktop_tests --output-on-failure`; full host suite from repo root Ninja build; `dotnet build` clean; UI batch green.
- [ ] **Step 3:** Update this plan's checkboxes; commit `docs(desktop): desktop app user guide and plan cross-reference`.

---

## Out of scope (deliberate)

- Hardware-parity control surface (faders, 8-button tile, encoder dial, lidar slider, Settings-on-OLED) — that is the port plan's Phase 2, Tasks 8-12. This plan's Sounds page overlays preset/randomize convenience but does **not** replace the replica surface.
- Tempo/scale/shuffle/theme *setters* — reachable via replica controls in port-plan Phase 2; direct exports would fork the firmware's UI logic (contra its zero-fork principle).
- MIDI, audio-device picker (miniaudio default device), ASIO, MSIX signing — port-plan out-of-scope list stands.
- Localizable resources (.resw) — single-locale app; the label/value split in Task 3 keeps the door open.

## Risks

- **`x:Bind` event-to-VM methods inside `DataTemplate`** (Task 2) — supported (generated code calls the method on the typed item), but if the XAML compiler balks, fall back to attaching handlers in code-behind while walking `PadRepeater` children; do not reintroduce per-pad `Background` writes.
- **`p2s_get_status` reads `uiState` racily** (no lock, api.cpp:94-110) — pre-existing, tolerated for status; the new getters follow the same tolerance, but *mutators* must not (hence the queue).
- **Poll cost on Patterns page** — 9 × 64 floats + playhead per tick is trivial; if profiling ever says otherwise, gate lane refresh on `p2s_processed_step_count()` change (already exported).
- **Concurrent user edits** — implementer must `git status` before each commit and expect in-flight changes outside `desktop/`.
