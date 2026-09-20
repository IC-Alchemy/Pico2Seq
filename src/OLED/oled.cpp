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
#include "../ui/ControlSurfaceLogic.h"
#include "../ui/SettingsPads.h"
#include <algorithm>
#include <cstring> // For strcmp, strlen
#include <Arduino.h>

// ========================= OLED Display Module =========================
// Overview:
// - Purpose: Centralized UI rendering for Pico2Seq on an SH110X OLED.
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
//   - Each view draws a whole frame into the 1024-byte buffer; commitFrame()
//     then pushes only the 128-byte pages that differ from what the panel
//     already shows, so a static screen sends nothing at all.
//   - Geometry is computed with simple integer math to keep CPU usage low.
//   - Where possible we reuse UIState/Sequencer data to avoid recomputation.
// =======================================================================
// OLED lives on the main bus (Wire, I2C0) at 400 kHz; Wire1 stays a tiles-only
// 100 kHz bus. commitFrame() pushes pages over Wire, so the two must agree.
// The driver's pre/post transfer rates match the bus as well: its display()
// re-clocks the shared TwoWire object, and a 100 kHz value there would leave
// every sensor on the bus slowed down after a push.
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

namespace
{
// One SH1106 page covers 8 rows across the 128-column panel = 128 framebuffer
// bytes.
constexpr uint16_t kPageBytes = OLEDConstants::SCREEN_WIDTH;

// The SH1106 GDDRAM is 132 columns wide, so panel column 0 sits at RAM column
// 2 — the offset Adafruit_SH1106G.cpp bakes in as `_page_start_offset`.
constexpr uint8_t kPageColumnOffset = 2;

// Pushes one 128-byte framebuffer page into the panel's GDDRAM. The byte
// sequence mirrors Adafruit_SH110X::display(): a command transaction carrying
// `0xB0 | page` plus the two column-address nibbles, then a data transaction
// carrying the page behind the 0x40 data control byte. Like the library, the
// two transactions are separated by full STOPs; the SH1106 latches page and
// column until they are rewritten.
// Returns false when the bytes did not reach the panel, so the caller can keep
// its shadow for this page stale and retry on the next commit.
bool pushFramePage(uint8_t page, const uint8_t *pageData)
{
  Wire.beginTransmission(OLEDConstants::I2C_ADDRESS);
  Wire.write(0x00); // control byte: command stream
  Wire.write(0xB0 | page); // SH110X_SETPAGEADDR
  // Panel column 0 sits at RAM column kPageColumnOffset; like the library, send
  // the high column nibble first, then the low one.
  Wire.write(0x10 | (kPageColumnOffset >> 4));
  Wire.write(kPageColumnOffset & 0x0F);
  if (Wire.endTransmission() != 0)
  {
    return false;
  }

  Wire.beginTransmission(OLEDConstants::I2C_ADDRESS);
  Wire.write(0x40); // control byte: data stream
  const size_t queued = Wire.write(pageData, kPageBytes);
  const uint8_t status = Wire.endTransmission(); // always close the transaction
  // A short `queued` means Wire's TX buffer could not hold the whole page, so
  // report failure instead of letting the shadow claim a page we never sent.
  return status == 0 && queued == kPageBytes;
}
} // namespace

// Pushes the redrawn framebuffer to the panel page by page, skipping the pages
// that still match what the panel shows. Every view redraws the whole buffer
// after clearDisplay(), which also resets the library's dirty window, so
// without this gate each update() pays a full ~1 KB I2C frame push — the single
// largest consumer of the core-0 loop budget — while a static screen now pushes
// nothing and an edit costs only the pages it touched.
//
// The panel shares the main Wire bus at 400 kHz (ControlIO::beginMainBusAndLeds()).
// Pushes go through Wire directly because Adafruit keeps the BusIO device and its
// dirty window behind protected members, so the library's own partial-update path
// cannot be reached from here.
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

  // 1024 bytes of framebuffer = 8 pages of 128 bytes; compare and push per page.
  constexpr uint8_t kPageCount = kFrameBytes / kPageBytes;
  for (uint8_t page = 0; page < kPageCount; ++page)
  {
    const uint16_t offset = static_cast<uint16_t>(page) * kPageBytes;
    if (memcmp(frame + offset, frameShadow_ + offset, kPageBytes) == 0)
    {
      continue; // Page unchanged since the last transfer — still on the panel.
    }

    if (!pushFramePage(page, frame + offset))
    {
      // Bus failure: leave this page's shadow stale so the next commit retries,
      // and stop pushing rather than spend more I2C timeout budget this frame.
      return;
    }
    memcpy(frameShadow_ + offset, frame + offset, kPageBytes);
  }
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

  if (uiState.lastVoiceParameterButton < SettingsPads::kPadCount &&
      uiState.voiceParameterNoticeVoice == uiState.selectedVoiceIndex &&
      uiState.hasVoiceParameterFeedback(millis())) {
    displayVoiceParameterInfo(uiState, voiceManager, 0, 0);
    return;
  }

  displayHardware.clearDisplay();
  displayHardware.setTextSize(1);
  displayHardware.setTextColor(SH110X_WHITE);
  displayHardware.setCursor(4, 2);
  displayHardware.print("VOICE ");
  displayHardware.print(uiState.selectedVoiceIndex + 1);
  displayHardware.print("  PARAMETERS");
  displayHardware.drawFastHLine(4, 12, OLEDConstants::SCREEN_WIDTH - 8, SH110X_WHITE);
  displayHardware.setCursor(4, 18);
  displayHardware.print("Tap: toggle / +");
  displayHardware.setCursor(4, 30);
  displayHardware.print("Shift + tap: -");
  displayHardware.setCursor(4, 42);
  displayHardware.print("LED: current value");
  displayHardware.setCursor(4, 54);
  displayHardware.print("Enc btn: presets");
  commitFrame();
}

// update() (thin wrapper):
// - For convenience, delegates to the extended overload by passing a null manager.
//   Keeps call sites simple when voice config is not needed for that frame.
void OLEDDisplay::update(const UIState &uiState, const SequencerView &sequencers)
{
  // Call extended version with null voice manager
  update(uiState, sequencers, nullptr);
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
// - Arpeggiator mode's own page sits below Settings and above the step pages:
//   the mode replaces what the panel edits, so the step/gate/envelope views
//   would be showing values nothing can reach (see docs/arpeggiator.md).
// - Timing: uses millis()-based timeouts from UIState to show transient UIs without
//   blocking the main loop.
// - Efficiency: clears once, sets text props once, and renders one view per frame.
void OLEDDisplay::update(const UIState &uiState, const SequencerView &sequencers,
                         VoiceManager *voiceManager)
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
    const char *line2 = "";
    switch (uiState.oledNoticeKind)
    {
    case UIState::OledNoticeKind::Saved:    line1 = "SAVED"; break;
    case UIState::OledNoticeKind::Loaded:   line1 = "LOADED"; break;
    case UIState::OledNoticeKind::LoadError: line1 = "LOAD ERR"; break;
    case UIState::OledNoticeKind::VoiceCleared: line1 = "CLEARED"; break;
    case UIState::OledNoticeKind::AllCleared:   line1 = "ALL CLEAR"; break;
    case UIState::OledNoticeKind::ArpOn:
      line1 = "ARP ON";
      line2 = "arp mode";
      break;
    case UIState::OledNoticeKind::ArpOff:
      line1 = "ARP OFF";
      line2 = "step seq";
      break;
    default: break;
    }

    displayHardware.setTextSize(2);
    const uint8_t line1Width = static_cast<uint8_t>(strlen(line1) * 12); // size-2 chars are 12px wide
    displayHardware.setCursor((OLEDConstants::SCREEN_WIDTH - line1Width) / 2, 16);
    displayHardware.print(line1);

    if (line2[0])
    {
      displayHardware.setTextSize(1);
      const uint8_t line2Width = static_cast<uint8_t>(strlen(line2) * 6);
      displayHardware.setCursor((OLEDConstants::SCREEN_WIDTH - line2Width) / 2, 44);
      displayHardware.print(line2);
    }
    else if (uiState.oledNoticeKind == UIState::OledNoticeKind::Randomized ||
             uiState.oledNoticeKind == UIState::OledNoticeKind::VoiceCleared)
    {
      // The voice line belongs to the sequencer's step randomize/clear. In
      // Arpeggiator mode the same notices mean the arp's chord instead, which
      // has no single voice.
      displayHardware.setTextSize(1);
      char voiceLine[16];
      if (uiState.arp.active())
        snprintf(voiceLine, sizeof(voiceLine), "%s", "chord");
      else
        snprintf(voiceLine, sizeof(voiceLine), "Voice %u", static_cast<unsigned>(uiState.oledNoticeVoice) + 1);
      const uint8_t voiceLineWidth = static_cast<uint8_t>(strlen(voiceLine) * 6);
      displayHardware.setCursor((OLEDConstants::SCREEN_WIDTH - voiceLineWidth) / 2, 44);
      displayHardware.print(voiceLine);
    }

    commitFrame();
    return;
  }

  const auto voice = std::min<uint8_t>(uiState.selectedVoiceIndex, sequencers.size() - 1);
  const Sequencer &sequence = sequencers.clamped(voice);
  const auto *config = voiceManager ? voiceManager->getVoiceConfig(voiceSystem.getVoiceId(voice)) : nullptr;
  const ParamId held = getHeldParameterParamId(uiState);
  const bool selected = uiState.selectedStepForEdit >= 0;

  // A held (or Shift-latched) parameter button outranks the settings and
  // sequence-length screens. It shows the composed value at that lane's
  // playing cursor (or the selected step if in step edit), which is exactly
  // what live recording writes and the voice plays, plus the lidar distance.
  if (held != ParamId::Count)
  {
    const uint8_t targetStep = selected ? static_cast<uint8_t>(uiState.selectedStepForEdit)
                                        : sequence.getCurrentStepForParameter(held);
    Step liveStep = sequence.getPlaybackStep(selected ? targetStep : UINT8_MAX);
    if (AppState::performanceInput.handPresent)
    {
      const float norm = AppState::performanceInput.recordingValue();
      const float stored = mapNormalizedValueToParamRange(held, norm);
      const float composed = config ? VoiceEdit::composeLane(held, stored, config) : stored;
      switch (held)
      {
        case ParamId::Velocity:
          liveStep.velocityLevel = composed;
          break;
        case ParamId::Filter:
          liveStep.filterCutoff = composed;
          break;
        case ParamId::Attack:
          liveStep.attackTimeSeconds = composed;
          break;
        case ParamId::Decay:
          liveStep.decayTimeSeconds = composed;
          break;
        case ParamId::Note:
          liveStep.noteIndex = composed;
          break;
        case ParamId::Octave:
          liveStep.octaveOffset = VoiceEdit::mapOctave(composed);
          break;
        case ParamId::GateLength:
          liveStep.gateLengthTicks = static_cast<uint16_t>(std::max(1.0f,
              composed * SequencerConstants::PULSES_PER_SEQUENCER_STEP_TICKS));
          break;
        default:
          break;
      }
    }
    displayParameterInfo(held, liveStep, uiState,
                         targetStep, config, selected, true, false);
    commitFrame();
    return;
  }

  // Priority-based display logic with SettingsSubMode handling
  //
  // New sub-mode architecture (UIState::SettingsSubMode):
  // - PRESET_SELECTION: show preset selection/main settings UI
  // - VOICE_PARAMETER: show parameter toggles UI
  //
  // The active settings page is derived only from UIState.
  if (uiState.settingsMode)
  {
    const bool subParam = uiState.isVoiceParameterSettings();

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

  // Arpeggiator mode replaces the sequencer screens below it: no step is
  // selected there, so the gate-length, envelope and parameter pages cannot
  // describe anything the panel is doing. Settings above still win, because the
  // preset browser stays reachable while the arp plays.
  if (uiState.arp.active())
  {
    displayArpPage(uiState);
    commitFrame();
    return;
  }

  // MEDIUM-LOW PRIORITY: Gate Sequence Length Mode (active while encoder control is held)
  if (uiState.gateSeqLengthMode)
  {
    const uint8_t gateLen = sequence.getParameterStepCount(ParamId::Gate);

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

    // Horizontal bar spans the 16 steps a voice can play.
    const int barY = 56;
    const int left = 2;
    const int right = OLEDConstants::SCREEN_WIDTH - 2;
    const int totalW = right - left;
    const uint8_t cappedLen = std::min<uint8_t>(gateLen, LEDConstants::MAX_STEP_BUTTONS);
    // Outline
    displayHardware.drawRect(left, barY - 6, totalW, 6, SH110X_WHITE);
    // Fill proportional to cappedLen
    const int fillW = (totalW - 2) * cappedLen / LEDConstants::MAX_STEP_BUTTONS;
    if (fillW > 0)
    {
      displayHardware.fillRect(left + 1, barY - 5, fillW, 4, SH110X_WHITE);
    }

    commitFrame();
    return;
  }

  const auto editing = held != ParamId::Count ? held : uiState.currentEditParameter;
  const auto encoderId = VoiceEditor::encoderTarget();
  const ParamId encoderLane = config ? VoiceEdit::sequenceLane(encoderId, *config) : ParamId::Count;
  const bool showBase = config && uiState.encoderBaseViewUntil != 0 &&
                        millis() < uiState.encoderBaseViewUntil;
  // Step Edit is ENV mode: the envelope page, unless a toggled parameter is
  // being edited and no ENV fader moved in the last moment.
  const bool envelopePage = selected && (editing == ParamId::Count ||
                            (uiState.envViewUntil != 0 && millis() < uiState.envViewUntil));
  if (envelopePage) {
    displayEnvelopePage(uiState, sequence, config);
  } else if (editing != ParamId::Count && (held != ParamId::Count || selected)) {
    const uint8_t step = selected ? static_cast<uint8_t>(uiState.selectedStepForEdit) :
                                   sequence.getCurrentStepForParameter(editing);
    const bool base = showBase && encoderLane == editing;
    displayParameterInfo(editing,
                         base ? MusicalValues::baseStep(*config) :
                                sequence.getPlaybackStep(selected ? step : UINT8_MAX),
                         uiState, step, config, selected, held != ParamId::Count, base);
  } else {
    // No parameter held: the encoder target's playing value, or for 1.5 s
    // after an encoder turn the base it changed (a step's own value could
    // otherwise hide the edit).
    drawVoiceHeader(uiState, true);
    displayHardware.setCursor(104, 0);
    displayHardware.print("S"); displayHardware.print(sequence.getCurrentStep() + 1);
    const auto id = encoderId;
    const auto lane = encoderLane;
    const Step playing = sequence.getPlaybackStep();
    const Step values = showBase ? MusicalValues::baseStep(*config) : playing;
    char value[48] = "--";
    if (config) {
      if (lane != ParamId::Count)
        MusicalValues::format(lane, values, *config, scale[std::min<size_t>(currentScale, SCALES_COUNT - 1)],
                              uClock.getTempo(), value, sizeof(value));
      else VoiceEdit::format(encoderId, *config, value, sizeof(value));
    }
    displayHardware.setTextSize(1);
    displayHardware.setCursor(2, 31);
    displayHardware.print(config ? VoiceEdit::name(encoderId, *config) : "Value");
    constexpr const char *shortScales[] = {
      "Major", "Dorian", "Phrygian", "Lydian", "Mixolyd", "Minor", "Locrian",
      "Min Pent", "Phryg Dom", "Lyd Dom", "Harm Min", "Whole", "Chromatic"};
    static_assert(sizeof(shortScales) / sizeof(shortScales[0]) == SCALES_COUNT);
    displayHardware.setCursor(68, 31);
    displayHardware.print(showBase ? "Base" : shortScales[std::min<size_t>(currentScale, SCALES_COUNT - 1)]);
    if (!playing.isGateActive) {
      displayHardware.setCursor(92, 0); displayHardware.print("R");
    }
    drawMusicalValue(value, 41);
    displayHardware.setTextSize(1);
    drawStepIndicators(sequence, 63);
  }

  commitFrame();
}

// Why Arpeggiator mode needs its own page: the mode replaces what the pads, the
// four faders and the encoder do, so every step/parameter page would be showing
// values that nothing on the panel can reach. This page shows the arp's four
// fader settings, the held chord, what the engine is playing right now, and the
// lidar dynamics that set the last note's velocity.
void OLEDDisplay::displayArpPage(const UIState &state)
{
  const Arpeggiator::Engine &arp = state.arp;
  const size_t scaleIndex = std::min<size_t>(currentScale, SCALES_COUNT - 1);
  const int *row = scale[scaleIndex];
  const uint8_t voice = std::min<uint8_t>(state.selectedVoiceIndex, UIState::MAX_VOICES - 1);

  // Header: the mode, the voice the arp plays through, and the rate the dial
  // selects.
  displayHardware.setTextSize(1);
  displayHardware.setCursor(2, 0);
  displayHardware.print("ARP");
  displayHardware.setCursor(28, 0);
  displayHardware.print("V");
  displayHardware.print(voice + 1);
  const char *rate = arp.rateLabel();
  displayHardware.setCursor(OLEDConstants::SCREEN_WIDTH - 2 - 6 * static_cast<int>(strlen(rate)), 0);
  displayHardware.print(rate);
  displayHardware.drawFastHLine(2, 9, 124, SH110X_WHITE);

  // Pattern, octave range (fader 1) and latch.
  displayHardware.setCursor(2, 12);
  displayHardware.print(arp.patternLabel());
  char octaves[12];
  snprintf(octaves, sizeof(octaves), "oct %u", static_cast<unsigned>(arp.settings().octaves));
  displayHardware.setCursor(46, 12);
  displayHardware.print(octaves);
  if (arp.latchEnabled())
  {
    displayHardware.setCursor(100, 12);
    displayHardware.print("LATCH");
  }

  // The chord as note names, in the order the walk will visit it: ascending
  // chord degrees normally, press order for the Order pattern, which is what
  // that pattern actually plays. Scale degrees are named through the same note
  // table the step sequencer uses.
  const bool playsAsEntered = arp.settings().pattern == Arpeggiator::Pattern::Order;
  char chord[40] = "";
  size_t used = 0;
  const uint8_t count = arp.chordCount();
  for (uint8_t i = 0; i < count; ++i)
  {
    const uint8_t degree = playsAsEntered ? arp.orderDegree(i) : arp.chordDegree(i);
    char name[16];
    MusicalValues::noteName(static_cast<float>(degree), 0, row, name, sizeof(name));
    const size_t length = strlen(name);
    if (used + length + 1 >= sizeof(chord))
    {
      snprintf(chord + used, sizeof(chord) - used, "+");
      break;
    }
    if (used)
      chord[used++] = ' ';
    memcpy(chord + used, name, length + 1);
    used += length;
  }
  displayHardware.setCursor(2, 24);
  displayHardware.print(count ? chord : "touch pads for chord");

  // Live readout: how many notes the walk has played since the last restart,
  // and the notes that are gated on right now (up to four, in Chord pattern).
  char playing[24] = "";
  size_t playingUsed = 0;
  for (uint8_t slot = 0; slot < Arpeggiator::kMaxSlots; ++slot)
  {
    uint8_t degree = 0;
    uint8_t octave = 0;
    if (!arp.slotSounding(slot, degree, octave))
      continue;
    char name[16];
    MusicalValues::noteName(static_cast<float>(degree), 12 * static_cast<int>(octave), row,
                            name, sizeof(name));
    const size_t length = strlen(name);
    if (playingUsed + length + 2 >= sizeof(playing))
      break;
    if (playingUsed)
      playing[playingUsed++] = '/';
    memcpy(playing + playingUsed, name, length + 1);
    playingUsed += length;
  }
  if (!playing[0] && arp.lastDegree() != Arpeggiator::kNoDegree)
  {
    // Between gates nothing is sounding; a mono pattern still wants to show the
    // note it just played, or the readout would blink at every gate. The
    // parentheses mark it as the last note rather than a gated one.
    char lastName[16];
    MusicalValues::noteName(static_cast<float>(arp.lastDegree()),
                            12 * static_cast<int>(arp.lastOctave()), row, lastName,
                            sizeof(lastName));
    snprintf(playing, sizeof(playing), "(%s)", lastName);
  }
  displayHardware.setCursor(2, 36);
  displayHardware.print("S");
  displayHardware.print(arp.stepCount());
  displayHardware.print(" ");
  displayHardware.print(playing[0] ? playing : "--");

  // Lidar dynamics: a bar the hand fills, with the raw distance beside it.
  const int barLeft = 2;
  const int barRight = 86;
  const int barY = 58;
  displayHardware.drawRect(barLeft, barY - 6, barRight - barLeft, 6, SH110X_WHITE);
  const int fill = static_cast<int>((barRight - barLeft - 2) * arp.dynamics());
  if (fill > 0)
    displayHardware.fillRect(barLeft + 1, barY - 5, fill, 4, SH110X_WHITE);
  char hand[16];
  const int mm = distanceSensor.getRawDistanceMm();
  if (!arp.handInRange()) snprintf(hand, sizeof(hand), "no hand");
  else if (mm < 0) snprintf(hand, sizeof(hand), "--mm");
  else snprintf(hand, sizeof(hand), "%dmm", mm);
  displayHardware.setCursor(92, 52);
  displayHardware.print(hand);
}

void OLEDDisplay::displayEnvelopePage(const UIState &state, const Sequencer &sequence,
                                      const VoiceConfig *config)
{
  const uint8_t step = static_cast<uint8_t>(std::max(0, state.selectedStepForEdit));
  drawVoiceHeader(state, false);
  displayHardware.setCursor(98, 0);
  displayHardware.print("S"); displayHardware.print(step + 1);
  displayHardware.drawFastHLine(2, 10, 124, SH110X_WHITE);
  const Step values = sequence.getPlaybackStep(step);
  const int *row = scale[std::min<size_t>(currentScale, SCALES_COUNT - 1)];
  constexpr ParamId kLanes[] = {ParamId::Attack, ParamId::Decay, ParamId::Sustain, ParamId::Release};
  int y = 13;
  for (const ParamId lane : kLanes) {
    displayHardware.setCursor(2, y);
    displayHardware.print(lane == state.envFaderLane ? ">" : " ");
    displayHardware.print(config ? VoiceEdit::laneName(lane, *config) : paramName(lane));
    char value[24] = "--";
    if (config)
      MusicalValues::format(lane, values, *config, row, uClock.getTempo(), value, sizeof(value));
    char shown[28];
    if (followsPatch(sequence.getStepParameterValue(lane, step)))
      snprintf(shown, sizeof(shown), "(%s)", value);
    else
      snprintf(shown, sizeof(shown), "%s", value);
    displayHardware.setCursor(OLEDConstants::SCREEN_WIDTH - 2 - 6 * static_cast<int>(strlen(shown)), y);
    displayHardware.print(shown);
    y += 10;
  }
  // The encoder edits this step too: name the lane it turns.
  const ParamId encoderLane = ControlSurface::stepEditParameter(
      ParamId::Count, state.currentEditParameter, state.currentEncoderParameter);
  displayHardware.setCursor(2, 55);
  displayHardware.print("()=patch");
  if (encoderLane != ParamId::Count) {
    displayHardware.print(" Enc:");
    displayHardware.print(config ? VoiceEdit::laneName(encoderLane, *config) : paramName(encoderLane));
  }
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
                                       const VoiceConfig *config, bool selected,
                                       bool showDistance, bool base)
{
  drawVoiceHeader(state, false);
  displayHardware.drawFastHLine(2, 10, 124, SH110X_WHITE);
  displayHardware.setCursor(2, 14);
  displayHardware.print(config ? VoiceEdit::laneName(id, *config) : paramName(id));
  displayHardware.setCursor(104, 14);
  displayHardware.print("S"); displayHardware.print(step + 1);
  char value[48] = "--";
  if (config)
    MusicalValues::format(id, values, *config, scale[std::min<size_t>(currentScale, SCALES_COUNT - 1)],
                          uClock.getTempo(), value, sizeof(value));
  drawMusicalValue(value, 27);
  displayHardware.setTextSize(1);
  displayHardware.setCursor(2, 46);
  if (base) displayHardware.print("BASE");
  else {
    displayHardware.print(selected ? "STEP " : "LIVE ");
    displayHardware.print(values.isGateActive ? "NOTE" : "REST");
  }
  if (showDistance)
  {
    char hand[16];
    const int mm = distanceSensor.getRawDistanceMm();
    if (mm < 0) snprintf(hand, sizeof(hand), "--mm");
    else if (AppState::performanceInput.handPresent) snprintf(hand, sizeof(hand), "%dmm", mm);
    else snprintf(hand, sizeof(hand), "(%dmm)", mm);
    displayHardware.setCursor(OLEDConstants::SCREEN_WIDTH - 2 - 6 * static_cast<int>(strlen(hand)), 46);
    displayHardware.print(hand);
  }
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

  if (uiState.isPresetSelection())
  {
    // Enhanced preset selection with cycling interface
    int currentPresetIndex = (uiState.selectedVoiceIndex < UIState::MAX_VOICES) ? uiState.voicePresetIndices[uiState.selectedVoiceIndex] : 0;

    // Header with voice info
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, OLEDConstants::TEXT_MARGIN);
    displayHardware.print("VOICE ");
    displayHardware.print(uiState.selectedVoiceIndex + 1);

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

    // Pad N holds preset N (VoicePresets::presetIndexForPad)
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 45);
    displayHardware.print("Pads 0-");
    displayHardware.print(VoicePresets::getPresetCount() - 1);
    displayHardware.print(" #");
    displayHardware.print(currentPresetIndex + 1);
    displayHardware.print("/");
    displayHardware.print(VoicePresets::getPresetCount());

    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 56);
    displayHardware.print("V1-V4 select voice");
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

    // Prompt for preset selection pads when in Preset sub-mode
    // (pads 0 .. presetCount-1, e.g. 0-28 for the 29-preset bank)
    displayHardware.setCursor(OLEDConstants::TEXT_MARGIN, 56);
    displayHardware.print("Pads 0-");
    displayHardware.print(VoicePresets::getPresetCount() - 1);
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

  // Snapshot the event rather than re-reading a possibly changed voice.
  displayHardware.setCursor(4, 2);
  displayHardware.print("VOICE ");
  displayHardware.print(uiState.voiceParameterNoticeVoice + 1);
  displayHardware.print("  PARAMETERS");
  displayHardware.drawFastHLine(4, 12, OLEDConstants::SCREEN_WIDTH - 8, SH110X_WHITE);
  displayHardware.setCursor(4, 20);
  displayHardware.print(uiState.voiceParameterNoticeName);
  displayHardware.setCursor(4, 34);
  displayHardware.setTextSize(strlen(uiState.voiceParameterNoticeValue) <= 10 ? 2 : 1);
  displayHardware.print(uiState.voiceParameterNoticeValue);
  displayHardware.setTextSize(1);
  displayHardware.setCursor(4, 55);
  displayHardware.print("Pad ");
  displayHardware.print(uiState.lastVoiceParameterButton + 1);

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
    const bool subParam = uiState.isVoiceParameterSettings();

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
    const bool subParam = uiState.isVoiceParameterSettings();

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
