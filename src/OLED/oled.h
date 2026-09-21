// oled.h — SH1106 128x64 status screen (Core 0, I2C0).
// Player view: what's editable now — held-lane value, ENV page, preset menu.
// One view per frame by priority; commitFrame() pushes only changed 128-byte
// pages so a static screen costs no I2C traffic.
#ifndef OLED_H
#define OLED_H

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "../ui/UIState.h"
#include "../ui/ButtonManager.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include "../LEDMatrix/LEDConstants.h"

struct VoiceConfig;
class SequencerView;

// Callback into the screen when a voice value or selection changes, so the
// player sees the edit without waiting for the next poll.
class VoiceParameterObserver
{
public:
  virtual ~VoiceParameterObserver() = default;

  // Fired with the new value already applied; state is the voice snapshot.
  virtual void onVoiceParameterChanged(uint8_t voiceId, const VoiceState &state) = 0;

  // Fired on voice select (0-based id); the screen re-reads on next update.
  virtual void onVoiceSwitched(uint8_t newVoiceId) = 0;
};

// SH1106 status screen: held-lane value, ENV page, preset/settings menus, and
// a step-bar mirror of the gate lane. Priority-ordered, one view per frame;
// all drawing is Core 0, millis()-timed, never blocking.
class OLEDDisplay : public VoiceParameterObserver
{
public:
  OLEDDisplay();

  // Probe the panel at 0x3C; false = run headless, caller degrades gracefully.
  bool begin();

  // Render from UI + sequencer snapshots (no voice config: some views degrade).
  void update(const UIState &uiState, const SequencerView &sequencers);

  // Full render with voice configs for musical (Hz/note) value formatting.
  void update(const UIState &uiState, const SequencerView &sequencers,
              class VoiceManager *voiceManager);

  // Blank the panel now (also pushes, so LEDs/OLED stay in sync).
  void clear();

  // False until begin() probes the panel — every view bails when headless.
  bool isInitialized() const { return isDisplayInitialized; }

  // Cached for observer callbacks between update() calls.
  void setVoiceManager(class VoiceManager *voiceManager);

  // VoiceParameterObserver interface implementation
  void onVoiceParameterChanged(uint8_t voiceId, const VoiceState &state) override;
  void onVoiceSwitched(uint8_t newVoiceId) override;

  // Observer callback with full context: redraws settings views immediately.
  void onVoiceSwitched(const UIState &uiState, class VoiceManager *voiceManager);

private:
  void displayVoiceEditor(const UIState &uiState, class VoiceManager *voiceManager);
  // SH1106 driver; all drawing goes to its 1 KB buffer, commitFrame() pushes.
  Adafruit_SH1106G displayHardware;
  bool isDisplayInitialized = false;

  // Cached for observer callbacks between update() calls (never owned).
  class VoiceManager *voiceManagerReference = nullptr;

  // Startup-animation clock only; the live screen never animates on a timer.
  uint32_t lastAnimationFrameMs = 0;
  uint8_t borderAnimationPhase = 0;

  // Size of one full SH1106G framebuffer in bytes (1 bit per pixel).
  static constexpr uint16_t kFrameBytes =
      OLEDConstants::SCREEN_WIDTH * OLEDConstants::SCREEN_HEIGHT / 8;

  // Shadow of the framebuffer content the panel actually shows. Poisoned in
  // the constructor so the first commitFrame() after begin() still pushes
  // every page rather than trusting power-up RAM.
  uint8_t frameShadow_[kFrameBytes];

  // Push the redrawn buffer page by page, skipping pages the panel already
  // shows. Views clearDisplay() first (resetting the lib's dirty window), so
  // without this gate every frame would cost a full ~1 KB I2C push.
  void commitFrame();

  void drawVoiceHeader(const UIState &state, bool prominent);
  void drawMusicalValue(const char *text, int y);
  // Displays composed values from the same read-only snapshot as playback;
  // showDistance adds lidar feedback and base shows the unmodulated value.
  void displayParameterInfo(ParamId id, const Step &values, const UIState &state,
                            uint8_t step, const VoiceConfig *config, bool selected,
                            bool showDistance, bool base);
  // ENV mode: the selected step's four envelope lanes (the ENV faders), the
  // last moved one marked, values in parentheses following the patch.
  void displayEnvelopePage(const UIState &state, const Sequencer &sequence,
                           const VoiceConfig *config);

  // Preset pick / sound-buffet list, per SettingsSubMode.
  void displaySettingsMenu(const UIState &uiState);

  // Transient "what just changed" card after a settings-pad tap.
  void displayVoiceParameterInfo(const UIState &uiState, class VoiceManager *voiceManager,
                                 uint8_t leadVoiceId, uint8_t bassVoiceId);

  // Settings-pad grid help page (tap = toggle/+, Shift+tap = -).
  void displayVoiceParameterToggles(const UIState &uiState, class VoiceManager *voiceManager);

  // Redraw settings views outside the frame cadence (voice-switch feedback).
  void forceUpdate(const UIState &uiState, class VoiceManager *voiceManager);

  // Visual enhancement helper functions

  // Gate-lane mirror: tall bar = sounding step, mid = gated, short = rest.
  void drawStepIndicators(const Sequencer &sequencer, int yPosition);

  // One-shot boot splash; delays allowed here, never in the live loop.
  void runStartupAnimation();
};

// Live screen instance (Core 0 owns it).
extern OLEDDisplay oledDisplay;

#endif // OLED_H
