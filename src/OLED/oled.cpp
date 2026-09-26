#include "oled.h"
#include "../ui/ControlSurfaceLogic.h"
#include "../voice/Voice.h"
#include "../voice/VoicePresets.h"
#include "../voice/MusicalValues.h"
#include "../app/VoiceEditor.h"
#include "../app/AppState.h"
#include "../voice/VoiceSystem.h" // voice id -> slot lookup
#include "../../includes.h"
#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include "../pico2seq-core/sequencer/ShuffleTemplates.h"
#include "../pico2seq-core/scales/scales.h"
#include "../ui/ButtonManager.h"
#include "../ui/ControlSurfaceLogic.h"
#include "../ui/SettingsPads.h"
#include "../ui/ArpDisplay.h"
#include <algorithm>
#include <cstring> // strcmp, strlen
#include <Arduino.h>

// ========================= OLED Display Module =========================
// What's editable now, on a 128x64 SH1106 (Core 0, I2C0 at 400 kHz).
// One view per frame by priority (banner > held lane > settings > status);
// transient views are millis()-timed, never blocking. Each view redraws the
// 1 KB buffer and commitFrame() pushes only changed 128-byte pages.
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

// begin(): probe at 0x3C (false = headless); boot splash may delay, the live
// loop never does.
bool OLEDDisplay::begin()
{
  if (!displayHardware.begin(OLEDConstants::I2C_ADDRESS, true))
  {
    Serial.println("[ERROR] OLED display initialization failed!");
    return false;
  }

  isDisplayInitialized = true;

  displayHardware.clearDisplay();
  displayHardware.setTextSize(1);
  displayHardware.setTextColor(SH110X_WHITE);
  displayHardware.setCursor(0, 0);

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

// Thin wrapper: no voice config, so musical-value views degrade to raw lanes.
void OLEDDisplay::update(const UIState &uiState, const SequencerView &sequencers)
{
  update(uiState, sequencers, nullptr);
}

// Main render: strict view priority so two screens never fight for a frame:
// PARAM/UTIL banner > confirmation notice > held-lane value > settings pages >
// arp page (below Settings, above the step pages: the mode replaces what the
// panel edits, so step/gate/envelope views would show unreachable values) >
// gate-length bar > step/ENV edit > idle status. One view draws, then returns.
void OLEDDisplay::update(const UIState &uiState, const SequencerView &sequencers,
                         VoiceManager *voiceManager)
{
  if (!isDisplayInitialized)
  {
    return;
  }

  voiceManagerReference = voiceManager;

  displayHardware.clearDisplay();
  displayHardware.setTextSize(1);
  displayHardware.setTextColor(SH110X_WHITE);

  if(uiState.voiceEditor.active) {
    displayVoiceEditor(uiState,voiceManager);
    return;
  }
  // PARAM/UTIL strap flip: fullscreen banner for a short window.
  if (uiState.alchemyModeBannerUntil != 0 && millis() < uiState.alchemyModeBannerUntil)
  {
    const bool paramMode = (uiState.alchemyMode == UIState::AlchemyMode::Param);
    const char *banner = paramMode ? (uiState.arp.active() ? "ARP" : "PARAM") : "UTIL";
    const uint8_t bannerWidth = static_cast<uint8_t>(strlen(banner) * 6 * 3); // size-3 text
    displayHardware.setTextSize(3);
    displayHardware.setCursor((OLEDConstants::SCREEN_WIDTH - bannerWidth) / 2, 24);
    displayHardware.print(banner);
    displayHardware.setTextSize(1);
    displayHardware.setCursor((OLEDConstants::SCREEN_WIDTH - 10 * 6) / 2, 52);
    displayHardware.print(paramMode ? (uiState.arp.active() ? "> patterns <" : "> params <") : "> utility <");
    commitFrame();
    return;
  }

  // Confirmation notice (saved/loaded/macro/...): covers all, then resumes.
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
    case UIState::OledNoticeKind::Macro:        line1 = "MACRO"; break;
    case UIState::OledNoticeKind::DelayMix:     line1 = "DELAY MIX"; break;
    case UIState::OledNoticeKind::DelayTime:    line1 = "DELAY TIME"; break;
    case UIState::OledNoticeKind::DelayFeedback: line1 = "DELAY FB"; break;
    case UIState::OledNoticeKind::ArpOn:
      line1 = "ARP ON";
      line2 = "Touch pads, then Play";
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
    else if (uiState.oledNoticeKind == UIState::OledNoticeKind::DelayMix ||
             uiState.oledNoticeKind == UIState::OledNoticeKind::DelayTime ||
             uiState.oledNoticeKind == UIState::OledNoticeKind::DelayFeedback)
    {
      displayHardware.setTextSize(1);
      char valueLine[14];
      if (uiState.oledNoticeKind != UIState::OledNoticeKind::DelayTime)
        snprintf(valueLine, sizeof(valueLine), "%u %%", static_cast<unsigned>(uiState.oledNoticeValue));
      else
        snprintf(valueLine, sizeof(valueLine), "%u ms", static_cast<unsigned>(uiState.oledNoticeValue));
      const uint8_t valueLineWidth = static_cast<uint8_t>(strlen(valueLine) * 6);
      displayHardware.setCursor((OLEDConstants::SCREEN_WIDTH - valueLineWidth) / 2, 44);
      displayHardware.print(valueLine);
    }

    if (uiState.oledNoticeKind == UIState::OledNoticeKind::Macro)
    {
      // Zone + percent while the Shift + volume fader drives the macro knob.
      displayHardware.setTextSize(1);
      char macroLine[16];
      const uint8_t percent =
          uiState.macroNoticePercent > 100 ? 100 : uiState.macroNoticePercent;
      snprintf(macroLine, sizeof(macroLine), "%s %u%%",
               ControlSurface::masterMacroZoneName(percent / 100.0f),
               static_cast<unsigned>(percent));
      const uint8_t macroLineWidth = static_cast<uint8_t>(strlen(macroLine) * 6);
      displayHardware.setCursor((OLEDConstants::SCREEN_WIDTH - macroLineWidth) / 2, 44);
      displayHardware.print(macroLine);
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
    // Read the same stored/composed step that playback consumes. A separate
    // sensor preview can hide a failed write and previously omitted Release.
    const Step liveStep = sequence.getPlaybackStep(selected ? targetStep : UINT8_MAX);
    displayParameterInfo(held, liveStep, uiState,
                         targetStep, config, selected, true, false);
    commitFrame();
    return;
  }

  // Settings pages derive only from UIState's sub-mode.
  if (uiState.settingsMode)
  {
    const bool subParam = uiState.isVoiceParameterSettings();

    if (subParam && voiceManager)
    {
      displayVoiceParameterToggles(uiState, voiceManager);
      // Toggles view pushes its own frame; returning avoids a double push.
      return;
    }

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

  // Gate-length edit: bar over the 16 playable steps of the selected voice.
  if (uiState.gateSeqLengthMode)
  {
    const uint8_t gateLen = sequence.getParameterStepCount(ParamId::Gate);

    drawVoiceHeader(uiState, false);
    displayHardware.setCursor(2, 14);
    displayHardware.print("Sequence length");

    // Voice/length readout.
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
    displayHardware.drawRect(left, barY - 6, totalW, 6, SH110X_WHITE);
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
                              uClock.getTempo(), value, sizeof(value), showBase);
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

// Eight fixed rows; the rhythm strip uses one row. No marquee or automatic
// page switching while playing. Shift reveals the pattern choices and swaps
// the four faders from rhythm shaping to range/gate/swing/tone.
void OLEDDisplay::displayArpPage(const UIState &state)
{
  const auto &arp = state.arp;
  const auto &settings = arp.settings();
  const auto voice = std::min<uint8_t>(state.selectedVoiceIndex, 3);
  const size_t scaleIndex = std::min<size_t>(currentScale, SCALES_COUNT - 1);
  const float bpm = uClock.getTempo();
  const int preset = arp.rhythmPreset();
  const char *rhythm = Arpeggiator::rhythmPresetName(static_cast<uint8_t>(preset));
  char text[64]; // Format first, then visibly truncate once in line().
  auto line = [&](int y, const char *value) {
    ArpDisplay::Row fitted;
    ArpDisplay::fit(value, fitted);
    displayHardware.setTextSize(1);
    displayHardware.setCursor(1, y);
    displayHardware.print(fitted);
  };
  displayHardware.setTextWrap(false);
  snprintf(text, sizeof(text), "ARP %s V%u %s %s", isClockRunning ? ">" : "[]",
           unsigned(voice + 1), arp.rateLabel(), arp.latchEnabled() ? "HOLD" : "LIVE");
  line(0, text);
  displayHardware.fillRect(0, 8, 128, 8, SH110X_WHITE);
  displayHardware.setTextColor(SH110X_BLACK);
  line(8, VoicePresets::getPresetName(state.voicePresetIndices[voice]));
  displayHardware.setTextColor(SH110X_WHITE);

  if (state.shiftHeld) {
    snprintf(text, sizeof(text), "RHYTHM %u/%u rotate %u", unsigned(settings.hits),
             unsigned(settings.length), unsigned(settings.rotation));
    line(16, text);
    if (state.alchemyMode == UIState::AlchemyMode::Param) {
      line(24, "1All 2Pulse 3Tresillo");
      line(32, "4Five 5Orbit 6Seven");
    } else {
      line(24, "6:Restart 7:Clear");
      line(32, "PARAM: rhythm presets");
    }
    line(48, "1Oct 2Gate 3Sw 4Tone");
    snprintf(text, sizeof(text), "Dial:tempo %.0fbpm", bpm);
    line(56, text);
  } else {
    constexpr const char *scales[] = {"Major", "Dorian", "Phryg", "Lydian", "Mixolyd",
        "Minor", "Locrian", "MinPent", "PhrDom", "LydDom", "HarmMin", "Whole", "Chrom"};
    static_assert(sizeof(scales) / sizeof(scales[0]) == SCALES_COUNT);
    snprintf(text, sizeof(text), "%s %uoct %s", arp.patternLabel(), unsigned(settings.octaves), scales[scaleIndex]);
    line(16, text);
    ArpDisplay::Row chord;
    ArpDisplay::chord(arp, scale[scaleIndex], chord);
    line(24, chord);
    snprintf(text, sizeof(text), "%u/%u %s %.0fbpm", unsigned(settings.hits), unsigned(settings.length), rhythm, bpm);
    line(32, text);
    ArpDisplay::Row gate;
    ArpDisplay::gate(settings, bpm, gate);
    const unsigned swing = ArpDisplay::swingLong(settings);
    snprintf(text, sizeof(text), "G%s Sw%u:%u", gate, swing, 100 - swing);
    line(48, text);
    line(56, !isClockRunning ? "Play:start Hold:sound" : "Faders:rhythm S:tempo");
    if (isClockRunning && arp.lastDegree() != Arpeggiator::kNoDegree && state.arpLastNotes[0]) {
      displayHardware.fillRect(0, 56, 128, 8, SH110X_BLACK);
      char note[8];
      snprintf(note, sizeof(note), "%.7s", state.arpLastNotes);
      if (strlen(state.arpLastNotes) > 7) note[6] = '~';
      snprintf(text, sizeof(text), "Last %s Sh:more", note);
      line(56, text);
    }
  }

  // Bright cells are hits, tiny dots are rests; underline is the clock's
  // current position (including rests). Stopped transport has no playhead.
  for (uint8_t step = 0; step < settings.length; ++step) {
    const int left = step * 128 / settings.length;
    const int width = (step + 1) * 128 / settings.length - left - 2;
    if (arp.rhythmHitAt(step))
      displayHardware.fillRect(left + 1, 40, width, 4, SH110X_WHITE);
    else displayHardware.drawPixel(left + 1 + width / 2, 42, SH110X_WHITE);
    if (isClockRunning && arp.hasRhythmStep() && step == arp.rhythmStep())
      displayHardware.drawFastHLine(left + 1, 46, width, SH110X_WHITE);
  }

  // A recent movement replaces only the middle three rows. The patch, mode,
  // rate, rhythm and help stay in place; unsigned elapsed time handles wrap.
  if (state.arpControl != UIState::ArpControl::None &&
      static_cast<uint32_t>(millis() - state.arpControlAt) < 1400) {
    const char *label = "";
    const char *hint = "";
    ArpDisplay::Row value = "";
    using Control = UIState::ArpControl;
    switch (state.arpControl) {
    case Control::Octaves:
      label = "1 RANGE"; snprintf(value, sizeof(value), "%u oct", unsigned(settings.octaves)); break;
    case Control::Gate:
      label = "S 2 GATE length"; ArpDisplay::gate(settings, bpm, value); break;
    case Control::Swing: {
      label = "S 3 SWING long:short";
      const unsigned amount = ArpDisplay::swingLong(settings);
      snprintf(value, sizeof(value), "%u:%u", amount, 100 - amount); break;
    }
    case Control::Filter: {
      label = "S 4 TONE";
      const VoiceConfig *config = voiceManager ? voiceManager->getVoiceConfig(voiceSystem.getVoiceId(voice)) : nullptr;
      if (config) {
        Step step = MusicalValues::baseStep(*config);
        step.filterCutoff = VoiceEdit::composeLane(ParamId::Filter, settings.filter, config);
        MusicalValues::format(ParamId::Filter, step, *config, scale[scaleIndex], bpm, value, sizeof(value));
      } else snprintf(value, sizeof(value), "--");
      break;
    }
    case Control::Hits:
      label = "1 HITS"; snprintf(value, sizeof(value), "%u / %u", unsigned(settings.hits), unsigned(settings.length));
      hint = settings.hits ? "Hits spread evenly" : "Silent: raise Hits"; break;
    case Control::Length:
      label = "2 LENGTH"; snprintf(value, sizeof(value), "%u steps", unsigned(settings.length)); break;
    case Control::Rotate:
      label = "3 ROTATE"; snprintf(value, sizeof(value), "+%u", unsigned(settings.rotation)); break;
    case Control::Accent:
      label = "4 ACCENT";
      snprintf(value, sizeof(value), "1:%.2f", 1.0f - 0.75f * settings.accent);
      hint = "First hit : others"; break;
    case Control::Rate:
      label = "DIAL RATE"; snprintf(value, sizeof(value), "%s", arp.rateLabel()); hint = "T = triplet"; break;
    case Control::Tempo:
      label = "SHIFT DIAL TEMPO"; snprintf(value, sizeof(value), "%.0f BPM", bpm); break;
    case Control::Rhythm:
      label = "RHYTHM"; snprintf(value, sizeof(value), "%s", rhythm); hint = "Faders: reshape"; break;
    case Control::Latch:
      label = "HOLD CHORD"; snprintf(value, sizeof(value), "%s", arp.latchEnabled() ? "On" : "Off");
      hint = arp.latchEnabled() ? "New touch replaces it" : "Release lets notes go"; break;
    case Control::Restart:
      label = "RESTART"; snprintf(value, sizeof(value), "Step 1"); hint = "Next clock pulse"; break;
    default: break;
    }
    displayHardware.fillRect(0, 16, 128, 24, SH110X_BLACK);
    line(16, label);
    displayHardware.setTextSize(strlen(value) <= 10 ? 2 : 1);
    displayHardware.setCursor(1, 24);
    displayHardware.print(value);
    if (hint[0]) {
      displayHardware.fillRect(0, 56, 128, 8, SH110X_BLACK);
      line(56, hint);
    }
  }
  displayHardware.setTextSize(1);
  displayHardware.setTextWrap(true);
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
                          uClock.getTempo(), value, sizeof(value), base);
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
  // Preset pick for the selected voice.
  displayHardware.setCursor(OLEDConstants::SCREEN_WIDTH - 68, 2);
  displayHardware.print("Preset Mode");

  if (uiState.isPresetSelection())
  {
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
    int textWidth = strlen(currentPresetName) * 6 * presetSize; // 6px per size-1 column
    int centerX = (OLEDConstants::SCREEN_WIDTH - textWidth) / 2;
    displayHardware.setCursor(centerX, 20);
    displayHardware.print(currentPresetName);

    // Underline sweep marks the screen alive while picking.
    uint8_t phase = (millis() / 120) % (OLEDConstants::SCREEN_WIDTH - 10);
    displayHardware.drawFastHLine(OLEDConstants::TEXT_MARGIN, 38, OLEDConstants::SCREEN_WIDTH - 10, SH110X_WHITE);
    displayHardware.drawFastHLine(OLEDConstants::TEXT_MARGIN, 39, phase, SH110X_WHITE);

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
    // Voice list: one row per voice with its preset.
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

      // Blinking dot = alive; rows are static otherwise.
      uint8_t blinkPhase = ((millis() / 250) + voiceIndex) % 2;
      if (blinkPhase)
      {
        displayHardware.fillCircle(4, yPosition + 2, 2, SH110X_WHITE);
      }
      else
      {
        displayHardware.drawCircle(4, yPosition + 2, 2, SH110X_WHITE);
      }

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

  // Snapshot the notice event; the voice may have moved on already.
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

  voiceManagerReference = voiceManager;

  // Settings-only redraw; the live views refresh on their own frame.
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
  // Observer ping: map the voice id to its 0-based slot for the header.

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

  // Map the sounding voice id to its 0-based slot; unknown ids are ignored.
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

  voiceManagerReference = voiceManager;

  // Settings-only redraw; the live views refresh on their own frame.
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
  // Minimal observer overload: the header re-reads on the next update(), so
  // there is nothing to draw here. Kept to satisfy the interface.

  if (!isDisplayInitialized)
  {
    Serial.println("OLED: Voice switch ignored - display not initialized");
    return;
  }

  // Nothing to draw here; the UIState overload does the real work.
}

void OLEDDisplay::drawStepIndicators(const Sequencer &sequencer, int yPosition)
{
  // Gate-lane mirror: tall bar = sounding step, mid = gated, short = rest.
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

  for (uint8_t stepIndex = 0; stepIndex < stepCount; ++stepIndex)
  {
    const int stepXPosition = leftMargin + (stepIndex * totalWidth) / stepCount;
    const int nextStepXPosition = leftMargin + ((stepIndex + 1) * totalWidth) / stepCount;
    const int stepWidth = std::max(2, nextStepXPosition - stepXPosition - 1);

    const float gateValue = sequencer.getStepParameterValue(ParamId::Gate, pageStart + stepIndex);
    const bool isGateActive = gateValue > 0.5f;
    const bool isCurrentStep = (pageStart + stepIndex == currentStepIndex);

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

    // Filled = gated (audible), outline = rest.
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
  // One-shot boot splash (delays allowed: boot only, never the live loop).
  displayHardware.clearDisplay();

  for (int wipeWidth = 0; wipeWidth <= OLEDConstants::SCREEN_WIDTH; wipeWidth += 10)
  {
    displayHardware.fillRect(0, 0, wipeWidth, OLEDConstants::SCREEN_HEIGHT, SH110X_WHITE);
    commitFrame();
    delay(OLEDConstants::STARTUP_WIPE_DELAY_MS);

    displayHardware.clearDisplay();
    displayHardware.drawRect(0, 0, OLEDConstants::SCREEN_WIDTH, OLEDConstants::SCREEN_HEIGHT, SH110X_WHITE);
  }

  // Title drop, then the invite line.
  const char *applicationTitle = "ic alchemy";
  const int titleCharacterWidth = 12; // 6px columns at size 2
  const int titlePixelWidth = strlen(applicationTitle) * titleCharacterWidth;
  const int titleCenterX = (OLEDConstants::SCREEN_WIDTH - titlePixelWidth) / 2;

  for (int titleY = -16; titleY <= 18; titleY += 3)
  {
    displayHardware.clearDisplay();

    displayHardware.setTextSize(2);
    displayHardware.setCursor(titleCenterX, titleY);
    displayHardware.print(applicationTitle);
    commitFrame();

    delay(OLEDConstants::STARTUP_BOUNCE_DELAY_MS);
  }

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
            scale[std::min<size_t>(currentScale, SCALES_COUNT - 1)], uClock.getTempo(), value, sizeof(value),
            /*baseView=*/true);
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
