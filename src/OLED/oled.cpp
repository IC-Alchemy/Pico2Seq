#include "oled.h"
#include "../voice/Voice.h"
#include "../voice/VoicePresets.h"
#include "../voice/MusicalValues.h"
#include "../app/VoiceEditor.h"
#include "../app/AppState.h"
#include "../voice/VoiceSystem.h" // Added for complete VoiceSystem type
#include "../../includes.h"
#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include "../pico2seq-core/sequencer/ShuffleTemplates.h"
#include "../pico2seq-core/scales/scales.h"
#include "../ui/ButtonManager.h"
#include <algorithm>
#include <cstring> // For strcmp, strlen
#include <Arduino.h>

// ========================= OLED Display Module =========================
// Overview:
// - Purpose: Centralized UI rendering for PicoMudrasSequencer on an SH110X OLED.
// - Responsibilities: Initialize display hardware, render hierarchical UI (settings,
//   parameter edit, default status), and provide immediate visual feedback for voice
//   parameter changes using an observer-style interface.
// - Design principles:
//   1) Deterministic, low-overhead drawing: minimize dynamic heap use and heavy
//      drawing inside real-time pathways; batch draw calls and refresh once.
//   2) Clear priority model: setting/parameter edit screens pre-empt default UI to
//      avoid flicker and conflicting renders.
//   3) Embedded-friendly timing: all temporary views use millis()-based timeouts
//      rather than blocking delays (except startup animation which is one-shot).
//   4) Portability: only rely on Arduino-compatible primitives and SH110X API.
//   5) Maintainability: readable layout constants and small helper functions.
// - Performance notes:
//   - We clear the display once per update and render everything for that frame,
//     then call display() exactly once to avoid partial refresh flicker.
//   - Geometry is computed with simple integer math to keep CPU usage low.
//   - Where possible we reuse UIState/Sequencer data to avoid recomputation.
// =======================================================================
OLEDDisplay::OLEDDisplay() : displayHardware(OLEDConstants::SCREEN_WIDTH, OLEDConstants::SCREEN_HEIGHT, &Wire, OLEDConstants::RESET_PIN,
                                                  /*preclk=*/400000, /*postclk=*/400000),
                             isDisplayInitialized(false)
{
  // The panel powers up with arbitrary RAM; 0xFF guarantees the first
  // commitFrame() after begin() sees the cleared buffer as "changed" and
  // actually transfers it.
  memset(frameShadow_, 0xFF, kFrameBytes);
}

// begin():
// - Brings up the OLED hardware at the configured I2C address, returns false on
//   failure so caller can degrade gracefully.
// - Sets default text properties so subsequent draw calls have predictable state.
// - Runs a one-shot startup animation (it uses delays by design since it's only
//   executed during boot and never in the real-time audio loop).
bool OLEDDisplay::begin()
{
  // Initialize display hardware with I2C address
  if (!displayHardware.begin(OLEDConstants::I2C_ADDRESS, true))
  {
    Serial.println("[ERROR] OLED display initialization failed!");
    return false;
  }

  isDisplayInitialized = true;

  // Clear display and set default text properties
  displayHardware.clearDisplay();
  displayHardware.setTextSize(1);
  displayHardware.setTextColor(SH110X_WHITE);
  displayHardware.setCursor(0, 0);

  // Show startup animation
  runStartupAnimation();

  Serial.println("OLED display initialized successfully");
  return true;
}

void OLEDDisplay::clear()
{
  if (!isDisplayInitialized)
  {
    return;
  }

  displayHardware.clearDisplay();
  commitFrame();
}

// Pushes the redrawn framebuffer to the panel only when its content differs
// from the last physical transfer. Every view redraws the whole buffer after
// clearDisplay(), which resets the library's dirty window, so without this gate
// each update() pays a full ~1 KB I2C frame push even when the screen is
// static — the single largest consumer of the core-0 loop budget.
void OLEDDisplay::commitFrame()
{
  if (!isDisplayInitialized)
  {
    return;
  }

  const uint8_t *frame = displayHardware.getBuffer();
  if (memcmp(frame, frameShadow_, kFrameBytes) == 0)
  {
    return; // Panel already shows this frame — skip the wire transfer.
  }

  displayHardware.display();
  memcpy(frameShadow_, frame, kFrameBytes);
}

void OLEDDisplay::setVoiceManager(VoiceManager *voiceManager)
{
  voiceManagerReference = voiceManager;
  Serial.println("OLED: Voice manager reference set");
}

void OLEDDisplay::displayVoiceParameterToggles(const UIState &uiState, VoiceManager *voiceManager)
{
  if (!isDisplayInitialized || !voiceManager)
  {
    return;
  }

  displayHardware.clearDisplay();
  displayHardware.setTextSize(1);
  displayHardware.setTextColor(SH110X_WHITE);

  // Draw professional border
  displayHardware.drawRect(0, 0, OLEDConstants::SCREEN_WIDTH, OLEDConstants::SCREEN_HEIGHT, SH110X_WHITE);

  // Header with current voice indicator + sub-mode banner
  displayHardware.setCursor(OLEDConstants::TEXT_MARGIN - 3, 2);
  displayHardware.print("VOICE ");
  displayHardware.print(uiState.selectedVoiceIndex + 1);
  // Sub-mode indicator per new SettingsSubMode architecture
  displayHardware.setCursor(OLEDConstants::SCREEN_WIDTH - 70, 2);
  displayHardware.print("Param Mode");

  // Draw separator line under header
  displayHardware.drawFastHLine(OLEDConstants::TEXT_MARGIN - 3, OLEDConstants::LINE_SPACING,
                                OLEDConstants::SCREEN_WIDTH - (2 * OLEDConstants::TEXT_MARGIN) + 6, SH110X_WHITE);

  // Map selected voice index to actual voice ID
  const uint8_t currentVoiceID = voiceSystem.getVoiceId(uiState.selectedVoiceIndex);
  const VoiceConfig *voiceConfiguration = voiceManager->getVoiceConfig(currentVoiceID);

  if (!voiceConfiguration)
  {
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN - 3, 25);
    displayHardware.print("Voice config error");
    commitFrame();
    return;
  }

  // Voice parameter configuration data
  struct VoiceParameterDisplayInfo
  {
    const char *parameterName;
    int buttonNumber;
  };

  const VoiceParameterDisplayInfo parameterInfo[] = {
      {"Envelope", 8},
      {"Overdrive", 9},
      {"Filter Mode", 11},
      {"Filter Res", 12}};

  constexpr int parameterCount = sizeof(parameterInfo) / sizeof(parameterInfo[0]);

  // Display parameters in organized vertical layout
  const int startYPosition = 14;
  for (int paramIndex = 0; paramIndex < parameterCount; paramIndex++)
  {
    const int currentYPosition = startYPosition + (paramIndex * OLEDConstants::LINE_SPACING);

    // Display parameter name with colon
    displayHardware.setCursor(4, currentYPosition);
    displayHardware.print(parameterInfo[paramIndex].parameterName);
    displayHardware.print(":");

    // Display parameter value/state
    displayHardware.setCursor(70, currentYPosition);
    switch (parameterInfo[paramIndex].buttonNumber)
    {
    case 8: // Envelope
      displayHardware.print(voiceConfiguration->hasEnvelope ? "ON" : "OFF");
      break;
    case 9: // Overdrive
      displayHardware.print(voiceConfiguration->hasOverdrive ? "ON" : "OFF");
      break;
    case 11: // Filter Mode
    {
      const int filterModeIndex = static_cast<int>(voiceConfiguration->filterMode);
      if (filterModeIndex >= 0 && filterModeIndex < voiceui::kFilterModeCount)
      {
        displayHardware.print(voiceui::kFilterModeNames[filterModeIndex]);
      }
      else
      {
        displayHardware.print("UNK");
      }
    }
    break;
    case 12: // Filter Resonance
      displayHardware.print(static_cast<int>(voiceConfiguration->filterRes * 100));
      displayHardware.print("%");
      break;
    default:
      break;
    }
  }

  commitFrame();
}

// update() (thin wrapper):
// - For convenience, delegates to the extended overload by passing a null manager.
//   Keeps call sites simple when voice config is not needed for that frame.
void OLEDDisplay::update(const UIState &uiState, const Sequencer &seq1, const Sequencer &seq2,
                         const Sequencer &seq3, const Sequencer &seq4)
{
  // Call extended version with null voice manager
  update(uiState, seq1, seq2, seq3, seq4, nullptr);
}

// update() (main):
// - The heart of the display state machine. It enforces a strict priority order so
//   that mutually exclusive views do not fight for the screen in a given frame.
//   Priority from highest to lowest:
//     0) Transient PARAM/UTIL mode banner and confirmation notice (short windows)
//     1) In settings + voice parameter edit active (recent interaction window)
//     2) In settings main/preset menu
//     3) Transient voice parameter info (outside settings, brief after-change)
//     4) Default status (scale, shuffle, selected voice, encoder, step indicators)
// - Timing: uses millis()-based timeouts from UIState to show transient UIs without
//   blocking the main loop.
// - Efficiency: clears once, sets text props once, and renders one view per frame.
void OLEDDisplay::update(const UIState &uiState, const Sequencer &seq1, const Sequencer &seq2,
                         const Sequencer &seq3, const Sequencer &seq4, VoiceManager *voiceManager)
{
  if (!isDisplayInitialized)
  {
    return;
  }

  // Store voice manager reference for immediate updates
  voiceManagerReference = voiceManager;

  displayHardware.clearDisplay();
  displayHardware.setTextSize(1);
  displayHardware.setTextColor(SH110X_WHITE);

  if(uiState.voiceEditor.active) {
    displayVoiceEditor(uiState,voiceManager);
    return;
  }
  // HIGHEST PRIORITY: transient PARAM / UTIL banner shown for a short window
  // after the GP7 mode strap flips the Alchemy control surface function set.
  if (uiState.alchemyModeBannerUntil != 0 && millis() < uiState.alchemyModeBannerUntil)
  {
    const bool paramMode = (uiState.alchemyMode == UIState::AlchemyMode::Param);
    const char *banner = paramMode ? "PARAM" : "UTIL";
    const uint8_t bannerWidth = static_cast<uint8_t>(strlen(banner) * 6 * 3); // size-3 text
    displayHardware.setTextSize(3);
    displayHardware.setCursor((OLEDConstants::SCREEN_WIDTH - bannerWidth) / 2, 24);
    displayHardware.print(banner);
    displayHardware.setTextSize(1);
    displayHardware.setCursor((OLEDConstants::SCREEN_WIDTH - 10 * 6) / 2, 52);
    displayHardware.print(paramMode ? "> params <" : "> utility <");
    commitFrame();
    return;
  }

  // Transient confirmation notice (replaces the old control-cluster LED
  // flashes). Shown just below the PARAM/UTIL banner, above everything else,
  // then the previous view resumes.
  if (uiState.oledNoticeUntil != 0 && millis() < uiState.oledNoticeUntil &&
      uiState.oledNoticeKind != UIState::OledNoticeKind::None)
  {
    const char *line1 = "RANDOMIZED";

    displayHardware.setTextSize(2);
    const uint8_t line1Width = static_cast<uint8_t>(strlen(line1) * 12); // size-2 chars are 12px wide
    displayHardware.setCursor((OLEDConstants::SCREEN_WIDTH - line1Width) / 2, 16);
    displayHardware.print(line1);

    if (uiState.oledNoticeKind == UIState::OledNoticeKind::Randomized)
    {
      displayHardware.setTextSize(1);
      char voiceLine[12];
      snprintf(voiceLine, sizeof(voiceLine), "Voice %u", static_cast<unsigned>(uiState.oledNoticeVoice) + 1);
      const uint8_t voiceLineWidth = static_cast<uint8_t>(strlen(voiceLine) * 6);
      displayHardware.setCursor((OLEDConstants::SCREEN_WIDTH - voiceLineWidth) / 2, 44);
      displayHardware.print(voiceLine);
    }

    commitFrame();
    return;
  }

  // Priority-based display logic with SettingsSubMode handling
  //
  // New sub-mode architecture (UIState::SettingsSubMode):
  // - PRESET_SELECTION: show preset selection/main settings UI
  // - VOICE_PARAMETER: show parameter toggles UI
  //
  // Legacy flags (inPresetSelection, inVoiceParameterMode) are still honored for
  // backward compatibility, but currentSubMode is the source of truth.
  if (uiState.settingsMode)
  {
    const bool subPreset = (uiState.currentSubMode == UIState::SettingsSubMode::PRESET_SELECTION) || uiState.inPresetSelection;  // legacy compat
    const bool subParam = (uiState.currentSubMode == UIState::SettingsSubMode::VOICE_PARAMETER) || uiState.inVoiceParameterMode; // legacy compat

    if (subParam && voiceManager)
    {
      displayVoiceParameterToggles(uiState, voiceManager);
      // displayVoiceParameterToggles() ends with its own commitFrame(); no
      // second push needed here.
      return;
    }

    // Default to preset selection/main settings when in preset sub-mode
    // or when no voiceManager is provided.
    displaySettingsMenu(uiState);
    commitFrame();
    return;
  }

  // MEDIUM-LOW PRIORITY: Gate Sequence Length Mode (active while encoder control is held)
  if (uiState.gateSeqLengthMode)
  {
    // Determine current sequencer and its gate length
    const Sequencer &currentSeq = (uiState.selectedVoiceIndex == 0) ? seq1 : (uiState.selectedVoiceIndex == 1) ? seq2
                                                                         : (uiState.selectedVoiceIndex == 2)   ? seq3
                                                                                                               : seq4;
    const uint8_t gateLen = currentSeq.getParameterStepCount(ParamId::Gate);

    // Header
    drawVoiceHeader(uiState, false);
    displayHardware.setCursor(2, 14);
    displayHardware.print("Sequence length");

    // Voice and length info
    displayHardware.setTextSize(1);
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 24);
    displayHardware.print("Voice: ");
    displayHardware.print(uiState.selectedVoiceIndex + 1);

    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 36);
    displayHardware.print("Length: ");
    displayHardware.setTextSize(2);
    displayHardware.print(gateLen);

    // Horizontal bar spans the supported 64-step track length.
    const int barY = 56;
    const int left = 2;
    const int right = OLEDConstants::SCREEN_WIDTH - 2;
    const int totalW = right - left;
    const uint8_t cappedLen = std::min<uint8_t>(gateLen, SequencerConstants::MAX_STEPS_COUNT);
    // Outline
    displayHardware.drawRect(left, barY - 6, totalW, 6, SH110X_WHITE);
    // Fill proportional to cappedLen
    const int fillW = (totalW - 2) * cappedLen / SequencerConstants::MAX_STEPS_COUNT;
    if (fillW > 0)
    {
      displayHardware.fillRect(left + 1, barY - 5, fillW, 4, SH110X_WHITE);
    }

    commitFrame();
    return;
  }

  const Sequencer *sequences[] = {&seq1, &seq2, &seq3, &seq4};
  const auto voice = std::min<uint8_t>(uiState.selectedVoiceIndex, 3);
  const Sequencer &sequence = *sequences[voice];
  const auto *config = voiceManager ? voiceManager->getVoiceConfig(voiceSystem.getVoiceId(voice)) : nullptr;
  const ParamId held = getHeldParameterParamId(uiState);
  const ParamId editing = held != ParamId::Count ? held : uiState.currentEditParameter;
  const bool selected = uiState.selectedStepForEdit >= 0;
  if (editing != ParamId::Count && (held != ParamId::Count || selected)) {
    const uint8_t step = selected ? static_cast<uint8_t>(uiState.selectedStepForEdit) :
                                   sequence.getCurrentStepForParameter(editing);
    displayParameterInfo(editing, sequence.getPlaybackStep(selected ? step : UINT8_MAX),
                         uiState, step, config, selected);
  } else if (selected) {
    drawVoiceHeader(uiState, false);
    displayHardware.setCursor(2, 18);
    displayHardware.setTextSize(2);
    displayHardware.print("Step "); displayHardware.print(uiState.selectedStepForEdit + 1);
    displayHardware.setTextSize(1);
    displayHardware.setCursor(2, 43); displayHardware.print("Hold parameter");
    displayHardware.setCursor(2, 54); displayHardware.print("to edit this step");
  } else {
    drawVoiceHeader(uiState, true);
    displayHardware.setCursor(104, 0);
    displayHardware.print("S"); displayHardware.print(sequence.getCurrentStep() + 1);
    const auto id = VoiceEditor::encoderTarget();
    const auto lane = config ? VoiceEdit::sequenceLane(id, *config) : ParamId::Count;
    const Step values = sequence.getPlaybackStep();
    char value[48] = "--";
    if (config) {
      if (lane != ParamId::Count)
        MusicalValues::format(lane, values, *config, scale[std::min<size_t>(currentScale, SCALES_COUNT - 1)],
                              uClock.getTempo(), value, sizeof(value));
      else VoiceEdit::format(id, *config, value, sizeof(value));
    }
    displayHardware.setTextSize(1);
    displayHardware.setCursor(2, 31);
    displayHardware.print(config ? VoiceEdit::name(id, *config) : "Value");
    constexpr const char *shortScales[] = {
      "Major", "Dorian", "Phrygian", "Lydian", "Mixolyd", "Minor", "Locrian",
      "Min Pent", "Phryg Dom", "Lyd Dom", "Harm Min", "Whole", "Chromatic"};
    static_assert(sizeof(shortScales) / sizeof(shortScales[0]) == SCALES_COUNT);
    displayHardware.setCursor(68, 31);
    displayHardware.print(shortScales[std::min<size_t>(currentScale, SCALES_COUNT - 1)]);
    if (!values.isGateActive) {
      displayHardware.setCursor(92, 0); displayHardware.print("R");
    }
    drawMusicalValue(value, 41);
    displayHardware.setTextSize(1);
    drawStepIndicators(sequence, 63);
  }

  commitFrame();
}

void OLEDDisplay::drawMusicalValue(const char *text, int y)
{
  const size_t length = strlen(text);
  displayHardware.setTextSize(length <= 10 ? 2 : 1);
  displayHardware.setCursor(2, y);
  if (length <= 20) { displayHardware.print(text); return; }
  // Long oscillator chords use two small rows without clipping note names.
  size_t split = 20;
  for (size_t i = 1; i <= 20; ++i) if (text[i] == '/') split = i;
  char first[21];
  memcpy(first, text, split); first[split] = 0;
  displayHardware.print(first);
  displayHardware.setCursor(2, y + 8);
  displayHardware.print(text + split + (text[split] == '/' ? 1 : 0));
}

void OLEDDisplay::drawVoiceHeader(const UIState &state, bool prominent)
{
  const uint8_t voice = std::min<uint8_t>(state.selectedVoiceIndex, 3);
  const char *preset = VoicePresets::getPresetName(state.voicePresetIndices[voice]);
  displayHardware.setTextSize(1);
  displayHardware.setCursor(2, 0);
  displayHardware.print("V"); displayHardware.print(voice + 1);
  displayHardware.print(state.voiceEditor.changed[voice] ? "*" : " ");
  if (prominent) {
    displayHardware.setCursor(32, 0);
    displayHardware.print(isClockRunning ? ">" : "[]");
    displayHardware.print(" "); displayHardware.print(uClock.getTempo(), 0); displayHardware.print(" BPM");
    displayHardware.setTextSize(strlen(preset) <= 10 ? 2 : 1);
    displayHardware.setCursor(2, 12); displayHardware.print(preset);
  } else {
    displayHardware.setCursor(26, 0); displayHardware.print(preset);
  }
  displayHardware.setTextSize(1);
}

void OLEDDisplay::displayParameterInfo(ParamId id, const Step &values,
                                       const UIState &state, uint8_t step,
                                       const VoiceConfig *config, bool selected)
{
  drawVoiceHeader(state, false);
  displayHardware.drawFastHLine(2, 10, 124, SH110X_WHITE);
  displayHardware.setCursor(2, 14);
  displayHardware.print(config ? VoiceEdit::name(static_cast<VoiceEdit::Id>(id), *config) : paramName(id));
  displayHardware.setCursor(104, 14);
  displayHardware.print("S"); displayHardware.print(step + 1);
  char value[48] = "--";
  if (config)
    MusicalValues::format(id, values, *config, scale[std::min<size_t>(currentScale, SCALES_COUNT - 1)],
                          uClock.getTempo(), value, sizeof(value));
  drawMusicalValue(value, 27);
  displayHardware.setTextSize(1);
  displayHardware.setCursor(2, 46);
  displayHardware.print(selected ? "STEP " : "LIVE ");
  displayHardware.print(values.isGateActive ? "NOTE" : "REST");
  displayHardware.setCursor(2, 56);
  if (id == ParamId::Note || id == ParamId::Octave)
    displayHardware.print(scaleNames[std::min<size_t>(currentScale, SCALES_COUNT - 1)]);
  else if (id == ParamId::GateLength) {
    displayHardware.print(values.gateLengthTicks); displayHardware.print("/120 step ticks");
  } else if (id == ParamId::Slide && config && values.hasSlide) {
    char glide[24]; MusicalValues::time(config->slideSeconds, glide, sizeof(glide));
    displayHardware.print("Glide "); displayHardware.print(glide);
  } else if (id == ParamId::Filter && config && !VoiceParameters::binding(*config, id).target)
    displayHardware.print("Cutoff before env");
}

void OLEDDisplay::displaySettingsMenu(const UIState &uiState)
{
  displayHardware.setTextSize(1);
  // Sub-mode indicator per new SettingsSubMode architecture
  displayHardware.setCursor(OLEDConstants::SCREEN_WIDTH - 68, 2);
  displayHardware.print("Preset Mode");

  if (uiState.inPresetSelection)
  {
    // Enhanced preset selection with cycling interface
    int currentPresetIndex = (uiState.settingsMenuIndex < UIState::MAX_VOICES) ? uiState.voicePresetIndices[uiState.settingsMenuIndex] : 0;

    // Header with voice info
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, OLEDConstants::TEXT_MARGIN);
    displayHardware.print("VOICE ");
    displayHardware.print(uiState.settingsMenuIndex + 1);

    // Draw separator line
    displayHardware.drawFastHLine(OLEDConstants::TEXT_MARGIN, OLEDConstants::HEADER_HEIGHT,
                                  OLEDConstants::SCREEN_WIDTH - 10, SH110X_WHITE);

    // Current preset - large and centered
    displayHardware.setTextSize(2);
    const char *currentPresetName = VoicePresets::getPresetName(currentPresetIndex);
    const int presetSize = strlen(currentPresetName) <= 10 ? 2 : 1;
    displayHardware.setTextSize(presetSize);
    int textWidth = strlen(currentPresetName) * 6 * presetSize; // Approximate width for size 2
    int centerX = (OLEDConstants::SCREEN_WIDTH - textWidth) / 2;
    displayHardware.setCursor(centerX, 20);
    displayHardware.print(currentPresetName);

    // Subtle underline animation
    uint8_t phase = (millis() / 120) % (OLEDConstants::SCREEN_WIDTH - 10);
    displayHardware.drawFastHLine(OLEDConstants::TEXT_MARGIN, 38, OLEDConstants::SCREEN_WIDTH - 10, SH110X_WHITE);
    displayHardware.drawFastHLine(OLEDConstants::TEXT_MARGIN, 39, phase, SH110X_WHITE);

    // Navigation indicators
    displayHardware.setTextSize(1);

    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 45);
    displayHardware.print("Pads 8-");
    displayHardware.print(7 + VoicePresets::presetCountOnPage(VoicePresets::getPresetCount(), uiState.presetPage));
    displayHardware.print(" #");
    displayHardware.print(currentPresetIndex + 1);
    displayHardware.print("/");
    displayHardware.print(VoicePresets::getPresetCount());

    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 56);
    displayHardware.print("Page ");
    displayHardware.print(uiState.presetPage + 1);
    displayHardware.print("/");
    displayHardware.print(VoicePresets::presetPageCount(VoicePresets::getPresetCount()));
    displayHardware.print(" 6< >7");

  }
  else
  {
    // Enhanced main settings menu
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, OLEDConstants::TEXT_MARGIN);
    displayHardware.setTextSize(1);
    displayHardware.print("Sound Buffet");

    // Draw separator line
    displayHardware.drawFastHLine(OLEDConstants::TEXT_MARGIN, OLEDConstants::HEADER_HEIGHT,
                                  OLEDConstants::SCREEN_WIDTH - 10, SH110X_WHITE);

    // Voice configurations with better visual hierarchy (4 voices)
    for (int voiceIndex = 0; voiceIndex < 4; voiceIndex++)
    {
      int yPosition = 16 + voiceIndex * 9;

      // Animated bullet indicator
      uint8_t blinkPhase = ((millis() / 250) + voiceIndex) % 2;
      if (blinkPhase)
      {
        displayHardware.fillCircle(4, yPosition + 2, 2, SH110X_WHITE);
      }
      else
      {
        displayHardware.drawCircle(4, yPosition + 2, 2, SH110X_WHITE);
      }

      // Current preset name for each voice
      displayHardware.setCursor(12, yPosition);
      displayHardware.print(voiceIndex + 1); displayHardware.print(" ");
      const char *presetName = (voiceIndex < UIState::MAX_VOICES) ? VoicePresets::getPresetName(uiState.voicePresetIndices[voiceIndex]) : "Unknown";
      displayHardware.print(presetName);
    }

    // Prompt for preset selection buttons when in Preset sub-mode
    // (pads 8 .. 8+presetCount-1, e.g. 8-22 for the 15-preset bank)
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 56);
    displayHardware.print("Pads 8-");
    displayHardware.print(7 + VoicePresets::presetCountOnPage(VoicePresets::getPresetCount(), uiState.presetPage));
  }
}

void OLEDDisplay::displayVoiceParameterInfo(const UIState &uiState, VoiceManager *voiceManager,
                                            uint8_t leadVoiceId, uint8_t bassVoiceId)
{
  if (!isDisplayInitialized || !voiceManager)
    return;

  displayHardware.clearDisplay();
  displayHardware.setTextSize(1);
  displayHardware.setTextColor(SH110X_WHITE);

  // Get current voice configuration
  uint8_t selected = uiState.selectedVoiceIndex;
  uint8_t currentVoiceId = (selected == 0) ? leadVoiceId : (selected == 1) ? bassVoiceId
                                                                           : voiceSystem.getVoiceId(selected);
  const VoiceConfig *config = voiceManager->getVoiceConfig(currentVoiceId);

  if (!config)
  {
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 20);
    displayHardware.print("Voice config error");
    commitFrame();
    return;
  }

  // Header
  displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, OLEDConstants::TEXT_MARGIN);
  displayHardware.setTextSize(1);
  displayHardware.print("VOICE ");
  displayHardware.print(selected + 1);
  displayHardware.print(" PARAMETERS");

  // Draw separator line
  displayHardware.drawFastHLine(OLEDConstants::TEXT_MARGIN, OLEDConstants::HEADER_HEIGHT,
                                OLEDConstants::SCREEN_WIDTH - 10, SH110X_WHITE);

  // Parameter information based on button pressed
  const char *paramName = "";
  String paramValue = "";

  switch (uiState.lastVoiceParameterButton)
  {
  case 8:
    paramName = "Envelope";
    paramValue = config->hasEnvelope ? "ON" : "OFF";
    break;
  case 9:
    paramName = "Overdrive";
    paramValue = config->hasOverdrive ? "ON" : "OFF";
    break;
  case 11:
  {
    paramName = "Filter Mode";
    int mode = static_cast<int>(config->filterMode);
    if (mode >= 0 && mode < voiceui::kFilterModeCount)
    {
      paramValue = voiceui::kFilterModeNames[mode];
    }
    else
    {
      paramValue = "Unknown";
    }
  }
  break;
  case 12:
    paramName = "Filter Res";
    paramValue = String(config->filterRes, 2);
    break;
  default:
    paramName = "Parameter";
    paramValue = String(uiState.lastVoiceParameterButton);
    break;
  }

  // Display parameter name
  displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 20);
  displayHardware.setTextSize(1);
  displayHardware.print(paramName);
  displayHardware.print(":");

  // Display parameter value
  displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 35);
  displayHardware.setTextSize(2);
  displayHardware.print(paramValue);

  // Show button number
  displayHardware.setTextSize(1);
  displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 55);
  displayHardware.print("Button ");
  displayHardware.print(uiState.lastVoiceParameterButton);

  commitFrame();
}

void OLEDDisplay::forceUpdate(const UIState &uiState, VoiceManager *voiceManager)
{
  if (!isDisplayInitialized)
  {
    Serial.println("OLED: Force update failed - display not initialized");
    return;
  }

  if (!voiceManager)
  {
    Serial.println("OLED: Force update failed - voiceManager is null");
    return;
  }

  // Store voice manager reference
  voiceManagerReference = voiceManager;

  // Force immediate update in settings mode with new sub-mode handling
  if (uiState.settingsMode)
  {
    const bool subPreset = (uiState.currentSubMode == UIState::SettingsSubMode::PRESET_SELECTION) || uiState.inPresetSelection;
    const bool subParam = (uiState.currentSubMode == UIState::SettingsSubMode::VOICE_PARAMETER) || uiState.inVoiceParameterMode;

    if (subParam && voiceManager)
    {
      displayVoiceParameterToggles(uiState, voiceManager);
    }
    else
    {
      displaySettingsMenu(uiState);
    }
    commitFrame();
  }
}

void OLEDDisplay::onVoiceParameterChanged(uint8_t voiceId, const VoiceState &state)
{
  // This method is called immediately when a voice parameter changes
  // Provides immediate visual feedback with proper voice ID mapping

  if (!isDisplayInitialized)
  {
    Serial.println("OLED: Parameter change ignored - display not initialized");
    return;
  }

  if (!voiceManagerReference)
  {
    Serial.println("OLED: Parameter change ignored - no voice manager reference");
    return;
  }

  // Determine which voice this corresponds to
  uint8_t displayVoiceNumber = 0;
  bool voiceFound = false;

  for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++)
  {
    if (voiceId == voiceSystem.getVoiceId(i))
    {
      displayVoiceNumber = i; // 0-based index
      voiceFound = true;
      break;
    }
  }

  if (!voiceFound)
  {
    Serial.print("OLED: Warning - Unknown voice ID: ");
    Serial.println(voiceId);
    return;
  }
  /*
      // Comprehensive debug output for troubleshooting
      Serial.println("=== OLED Voice Parameter Change ===");
      Serial.print("Voice ID: ");
      Serial.print(voiceId);
      Serial.print(" -> Display Voice: ");
      Serial.println(displayVoiceNumber);
      Serial.print("Note: ");
      Serial.print(state.noteIndex);
      Serial.print(" Velocity: ");
      Serial.print(state.velocityLevel);
      Serial.print(" Filter: ");
      Serial.print(state.filterCutoff);
      Serial.print(" Attack: ");
      Serial.print(state.attackTimeSeconds);
      Serial.print(" Decay: ");
      Serial.println(state.decayTimeSeconds);
      Serial.print("Lead Voice ID: ");
      Serial.println(voice1Id);
      Serial.print("Bass Voice ID: ");
      Serial.println(voice2Id);
      Serial.println("=================================");


      */
}

void OLEDDisplay::onVoiceSwitched(const UIState &uiState, VoiceManager *voiceManager)
{
  if (!isDisplayInitialized)
  {
    Serial.println("OLED: Voice switch ignored - display not initialized");
    return;
  }

  if (!voiceManager)
  {
    Serial.println("OLED: Voice switch ignored - no voice manager");
    return;
  }

  // Store voice manager reference
  voiceManagerReference = voiceManager;

  // Force immediate update in settings mode with new sub-mode handling
  if (uiState.settingsMode)
  {
    const bool subPreset = (uiState.currentSubMode == UIState::SettingsSubMode::PRESET_SELECTION) || uiState.inPresetSelection;
    const bool subParam = (uiState.currentSubMode == UIState::SettingsSubMode::VOICE_PARAMETER) || uiState.inVoiceParameterMode;

    if (subParam && voiceManager)
    {
      displayVoiceParameterToggles(uiState, voiceManager);
    }
    else
    {
      displaySettingsMenu(uiState);
    }
    commitFrame();
  }
}

void OLEDDisplay::onVoiceSwitched(uint8_t newVoiceId)
{
  // Interface-compliant method for VoiceParameterObserver
  // This is called when the voice system switches to a new voice

  if (!isDisplayInitialized)
  {
    Serial.println("OLED: Voice switch ignored - display not initialized");
    return;
  }

  // Serial.print("OLED: Voice switched to ID ");
  // Serial.println(newVoiceId);

  // This method provides the minimal interface compliance
  // The extended version with UIState and VoiceManager parameters
  // should be used for full functionality
}

void OLEDDisplay::drawStepIndicators(const Sequencer &sequencer, int yPosition)
{
  // Draw step indicator bars showing gate states and current playhead position
  uint8_t stepCount = sequencer.getParameterStepCount(ParamId::Gate);
  if (stepCount == 0)
  {
    stepCount = LEDConstants::MAX_STEP_BUTTONS; // Default to 16 steps
  }
  const uint8_t currentStepIndex = sequencer.getCurrentStep();
  const uint8_t pageStart = (currentStepIndex / 16) * 16;
  stepCount = std::min<uint8_t>(16, stepCount > pageStart ? stepCount - pageStart : 16);
  const int leftMargin = 4;
  const int rightMargin = OLEDConstants::SCREEN_WIDTH - 4;
  const int totalWidth = rightMargin - leftMargin;

  // Draw individual step indicators
  for (uint8_t stepIndex = 0; stepIndex < stepCount; ++stepIndex)
  {
    // Calculate step indicator position and width
    const int stepXPosition = leftMargin + (stepIndex * totalWidth) / stepCount;
    const int nextStepXPosition = leftMargin + ((stepIndex + 1) * totalWidth) / stepCount;
    const int stepWidth = max(2, nextStepXPosition - stepXPosition - 1);

    // Get step gate state and determine if this is the current step
    const float gateValue = sequencer.getStepParameterValue(ParamId::Gate, pageStart + stepIndex);
    const bool isGateActive = gateValue > 0.5f;
    const bool isCurrentStep = (pageStart + stepIndex == currentStepIndex);

    // Calculate indicator height based on state
    int indicatorHeight;
    if (isCurrentStep)
    {
      indicatorHeight = 6; // Tallest for current step
    }
    else if (isGateActive)
    {
      indicatorHeight = 4; // Medium height for active gates
    }
    else
    {
      indicatorHeight = 2; // Shortest for inactive gates
    }

    const int indicatorTopY = yPosition - indicatorHeight;

    // Draw step indicator (filled for active gates, outline for inactive)
    if (isGateActive)
    {
      displayHardware.fillRect(stepXPosition, indicatorTopY, stepWidth, indicatorHeight, SH110X_WHITE);
    }
    else
    {
      displayHardware.drawRect(stepXPosition, indicatorTopY, stepWidth, indicatorHeight, SH110X_WHITE);
    }
  }
}

void OLEDDisplay::runStartupAnimation()
{
  // Professional startup animation with wipe effect and title bounce
  displayHardware.clearDisplay();

  // Horizontal wipe effect across screen
  for (int wipeWidth = 0; wipeWidth <= OLEDConstants::SCREEN_WIDTH; wipeWidth += 10)
  {
    displayHardware.fillRect(0, 0, wipeWidth, OLEDConstants::SCREEN_HEIGHT, SH110X_WHITE);
    commitFrame();
    delay(OLEDConstants::STARTUP_WIPE_DELAY_MS);

    // Clear and redraw border for scanning effect
    displayHardware.clearDisplay();
    displayHardware.drawRect(0, 0, OLEDConstants::SCREEN_WIDTH, OLEDConstants::SCREEN_HEIGHT, SH110X_WHITE);
  }

  // Title bounce animation
  const char *applicationTitle = "ic alchemy";
  const int titleCharacterWidth = 12; // Approximate width per character at size 2
  const int titlePixelWidth = strlen(applicationTitle) * titleCharacterWidth;
  const int titleCenterX = (OLEDConstants::SCREEN_WIDTH - titlePixelWidth) / 2;

  // Animate title dropping down and bouncing
  for (int titleY = -16; titleY <= 18; titleY += 3)
  {
    displayHardware.clearDisplay();

    displayHardware.setTextSize(2);
    displayHardware.setCursor(titleCenterX, titleY);
    displayHardware.print(applicationTitle);
    commitFrame();

    delay(OLEDConstants::STARTUP_BOUNCE_DELAY_MS);
  }

  // Final settle with subtitle
  displayHardware.setTextSize(1);
  displayHardware.setCursor(18, 44);
  displayHardware.print("Let's play");
  commitFrame();
  delay(OLEDConstants::STARTUP_SETTLE_DELAY_MS);
}

void OLEDDisplay::displayVoiceEditor(const UIState &state, VoiceManager *manager)
{
  // update() already cleared the buffer and set size/colour before dispatching.
  const auto index=state.selectedVoiceIndex;
  drawVoiceHeader(state, false);
  if(state.voiceEditor.help) {
    const char *lines[]={"1/2 Group  3/4 Param","5 Hold: fine","6 Hold: reset base","7 Help  8 Exit","Play after exit"};
    for(int i=0;i<5;++i) {displayHardware.setCursor(0,13+i*10);displayHardware.print(lines[i]);}
  } else if(manager) {
    const auto *config=manager->getVoiceConfig(voiceSystem.getVoiceId(index));
    if(config) {
      const auto id=state.voiceEditor.cursor[index];
      char value[48];
      const auto lane = VoiceEdit::sequenceLane(id, *config);
      if (lane != ParamId::Count) {
        MusicalValues::format(lane, MusicalValues::baseStep(*config), *config,
            scale[std::min<size_t>(currentScale, SCALES_COUNT - 1)], uClock.getTempo(), value, sizeof(value));
      } else VoiceEdit::format(id,*config,value,sizeof(value));
      displayHardware.setCursor(0,13);displayHardware.print(VoiceEdit::groupName(VoiceEdit::parameter(id).group));
      displayHardware.setCursor(0,25);displayHardware.print(VoiceEdit::name(id,*config));
      displayHardware.setCursor(0,37);displayHardware.print(value);
      displayHardware.setCursor(0,49);
      displayHardware.print(VoiceEdit::sequenceLane(id,*config)!=ParamId::Count?"BASE / STOPPED":"PATCH / STOPPED");
      displayHardware.setCursor(0,57);displayHardware.print(state.voiceEditor.fine?"FINE    7 Help 8 Exit":"ENC edit 7 Help 8 Exit");
    }
  }
  commitFrame();
}
