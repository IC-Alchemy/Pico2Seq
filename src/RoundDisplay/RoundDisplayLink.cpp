#include "RoundDisplayLink.h"

#include <Arduino.h>
#include <Wire.h>
#include <uClock.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../app/AppState.h" // AppState::performanceInput, voiceSystem
#include "../app/VoiceEditor.h"
#include "../LEDMatrix/LEDMatrixFeedback.h" // getActiveThemeColors
#include "../pico2seq-core/scales/scales.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../sensors/DistanceSensor.h" // distanceSensor
#include "../ui/ButtonManager.h"       // getHeldParameterParamId
#include "../voice/MusicalValues.h"
#include "../voice/VoiceEditParameters.h"
#include "../voice/VoiceManager.h"
#include "../voice/VoicePresets.h"

// ========================= Round Display Link =========================
// The Pico-side half of the round-panel link: it re-runs the OLED's page
// priority chain (src/OLED/oled.cpp:243-506) without touching a pixel,
// serializes the winning page into an rdisplay::PAGE_FRAME packet and pushes
// it over the main Wire bus (0x3E) at the shared 40 ms display tick.
// Protocol and page table: docs/superpowers/plans/
// 2026-09-20-round-display-link-option-b.md (§3, §4.3, §6).
// =======================================================================

namespace
{

// 2 Hz heartbeat with an identical SEQ: the panel treats a same-SEQ frame as
// already-applied, so the re-send only proves the link is alive.
constexpr uint32_t kHeartbeatIntervalMs = 500;
// With the panel absent, re-probe at most once a second (hot-plug support).
constexpr uint32_t kReprobeIntervalMs = 1000;
constexpr uint8_t kConsecutiveSendFailuresBeforeReprobe = 3;
constexpr size_t kMaxWireFrameBytes = rdisplay::kMaxFrameBytes - 1;
constexpr uint32_t kMaxInt16 = 32767;
constexpr uint32_t kMinInt16Magnitude = 32768;

/** Zero-fill + truncate copy into the fixed char arrays the wire carries. */
bool deadlineActive(uint32_t deadline, uint32_t now) noexcept
{
  return deadline != 0 && static_cast<int32_t>(deadline - now) > 0;
}

void copyString(char *out, size_t cap, const char *text)
{
  if (cap == 0)
    return;
  out[0] = '\0';
  if (text == nullptr)
    return;
  const size_t length = std::strlen(text);
  std::memcpy(out, text, std::min(length, cap - 1));
  out[std::min(length, cap - 1)] = '\0';
}

uint16_t crgbToRgb565(const CRGB &color)
{
  return rdisplay::toRgb565(color.r, color.g, color.b);
}

/** The theme block every frame carries: voice hues from the theme's gate
 * colors, then playhead, backdrop and text accents, all RGB565. */
rdisplay::ThemeBlock buildThemeBlock()
{
  rdisplay::ThemeBlock theme = {};
  const LEDThemeColors *colors = getActiveThemeColors();
  if (colors == nullptr)
    return theme;
  for (uint8_t i = 0; i < LED_THEME_VOICE_COUNT; ++i)
    theme.voiceHue[i] = crgbToRgb565(colors->gateOn[i]);
  theme.playheadAccent = crgbToRgb565(colors->playheadAccent);
  theme.backgroundBase = crgbToRgb565(colors->backgroundBase);
  theme.textAccent = crgbToRgb565(colors->textAccent);
  return theme;
}

const char *noticeWord(UIState::OledNoticeKind kind)
{
  // Same wording the OLED notice renders (oled.cpp:285-296).
  switch (kind)
  {
  case UIState::OledNoticeKind::Saved:       return "SAVED";
  case UIState::OledNoticeKind::Loaded:      return "LOADED";
  case UIState::OledNoticeKind::LoadError:   return "LOAD ERR";
  case UIState::OledNoticeKind::VoiceCleared: return "CLEARED";
  case UIState::OledNoticeKind::AllCleared:  return "ALL CLEAR";
  default:                                   return "RANDOMIZED";
  }
}

/** Per-voice suffix under the notice word (oled.cpp:303-325). */
void noticeSub(UIState::OledNoticeKind kind, uint8_t voice,
               char *out, size_t cap)
{
  out[0] = '\0';
  if (kind == UIState::OledNoticeKind::Randomized ||
      kind == UIState::OledNoticeKind::VoiceCleared)
    snprintf(out, cap, "Voice %u", static_cast<unsigned>(voice) + 1);
}

int16_t clampDistanceMm(int value) noexcept
{
  if (value < -static_cast<int>(kMinInt16Magnitude))
    return -32768;
  if (value > static_cast<int>(kMaxInt16))
    return 32767;
  return static_cast<int16_t>(value);
}

uint8_t normalizeStep(int step) noexcept
{
  if (step < 0)
    return 0;
  return static_cast<uint8_t>(std::min(step, static_cast<int>(SequencerConstants::MAX_STEPS_COUNT) - 1));
}

const int *scaleRow()
{
  return scale[std::min<size_t>(currentScale, SCALES_COUNT - 1)];
}

/** The Step a lane's value string is formatted from (oled.cpp:341-378): the
 * playing step, or with the hand on it the hand-composed live value. */
void composeHandValue(ParamId id, Step &liveStep, const VoiceConfig *config)
{
  if (!AppState::performanceInput.handPresent)
    return;
  const float norm = AppState::performanceInput.recordingValue();
  const float stored = mapNormalizedValueToParamRange(id, norm);
  const float composed = config ? VoiceEdit::composeLane(id, stored, config) : stored;
  switch (id)
  {
  case ParamId::Velocity:  liveStep.velocityLevel = composed; break;
  case ParamId::Filter:    liveStep.filterCutoff = composed; break;
  case ParamId::Attack:    liveStep.attackTimeSeconds = composed; break;
  case ParamId::Decay:     liveStep.decayTimeSeconds = composed; break;
  case ParamId::Note:      liveStep.noteIndex = composed; break;
  case ParamId::Octave:    liveStep.octaveOffset = VoiceEdit::mapOctave(composed); break;
  case ParamId::GateLength:
    liveStep.gateLengthTicks = static_cast<uint16_t>(std::max(1.0f,
        composed * SequencerConstants::PULSES_PER_SEQUENCER_STEP_TICKS));
    break;
  default:
    break;
  }
}

/** MusicalValues::format with the codebase's "--" default when no config is
 * available (oled.cpp:594). */
void formatLaneValue(ParamId id, const Step &values, const VoiceConfig *config,
                     char *out, size_t cap)
{
  out[0] = '\0';
  if (config)
    MusicalValues::format(id, values, *config, scaleRow(), uClock.getTempo(),
                          out, cap);
  else
    copyString(out, cap, "--");
}

} // namespace

// ---------------------------------------------------------------------------
// Presence and identity
// ---------------------------------------------------------------------------

bool RoundDisplayLink::probe()
{
  // Tile-shaped register-pointer read (plan §3): pointer write, STOP, then a
  // one-byte read. Never blocks long — Wire.setTimeout(25, true) is armed by
  // ControlIO::beginMainBusAndLeds().
  Wire.beginTransmission(rdisplay::kDisplayAddress);
  if (Wire.write(rdisplay::kRegWhoAmI) != 1 || Wire.endTransmission() != 0)
    return false;
  if (Wire.requestFrom(rdisplay::kDisplayAddress, static_cast<uint8_t>(1)) != 1)
    return false;
  if (Wire.read() != rdisplay::kWhoAmIMagic)
    return false;

  uint8_t typeId = 0;
  uint8_t protoVer = 0;
  return probeIdentity(typeId, protoVer);
}

bool RoundDisplayLink::begin()
{
  lastProbeMs_ = millis();
  present_ = probe();
  return present_;
}

bool RoundDisplayLink::probeIdentity(uint8_t &typeId, uint8_t &protoVer)
{
  typeId = 0;
  protoVer = 0;
  const uint8_t regs[] = {rdisplay::kRegTypeId, rdisplay::kRegProtoVer};
  uint8_t *outs[] = {&typeId, &protoVer};
  for (size_t i = 0; i < sizeof(regs); ++i)
  {
    Wire.beginTransmission(rdisplay::kDisplayAddress);
    if (Wire.write(regs[i]) != 1 || Wire.endTransmission() != 0)
      return false;
    if (Wire.requestFrom(rdisplay::kDisplayAddress, static_cast<uint8_t>(1)) != 1)
      return false;
    *outs[i] = static_cast<uint8_t>(Wire.read());
  }
  return typeId == rdisplay::kTypeDisplay && protoVer == rdisplay::kProtoVerV1;
}

// ---------------------------------------------------------------------------
// Page chain — the oled.cpp:243-506 gate order, serialized instead of drawn
// ---------------------------------------------------------------------------

namespace
{

// PageId::VoiceEditor (oled.cpp:258-261, 1002-1029): the focused voice's
// editor row — BASE value of the cursor parameter.
void buildVoiceEditorBody(const UIState &uiState, VoiceManager *voiceManager,
                          uint8_t *body, uint8_t &bodyLen)
{
  rdisplay::VoiceEditorBody out = {};
  const uint8_t voice = std::min<uint8_t>(uiState.selectedVoiceIndex, UIState::MAX_VOICES - 1);
  const auto *config = voiceManager
                           ? voiceManager->getVoiceConfig(voiceSystem.getVoiceId(voice))
                           : nullptr;
  out.voice = voice;
  out.changed = uiState.voiceEditor.changed[voice] ? 1 : 0;
  const auto id = uiState.voiceEditor.cursor[voice];
  if (config != nullptr)
  {
    copyString(out.paramName, sizeof(out.paramName), VoiceEdit::name(id, *config));
    const auto lane = VoiceEdit::sequenceLane(id, *config);
    char value[48] = "--";
    if (lane != ParamId::Count)
      MusicalValues::format(lane, MusicalValues::baseStep(*config), *config,
                            scaleRow(), uClock.getTempo(), value, sizeof(value));
    else
      VoiceEdit::format(id, *config, value, sizeof(value));
    copyString(out.paramValue, sizeof(out.paramValue), value);
  }
  else
  {
    copyString(out.paramName, sizeof(out.paramName), "--");
    copyString(out.paramValue, sizeof(out.paramValue), "--");
  }
  std::memcpy(body, &out, sizeof(out));
  bodyLen = sizeof(out);
}

// PageId::ModeBanner (oled.cpp:264-277): PARAM vs UTIL word.
void buildModeBannerBody(const UIState &uiState, uint8_t *body, uint8_t &bodyLen)
{
  rdisplay::ModeBannerBody out = {};
  out.kind = uiState.alchemyMode == UIState::AlchemyMode::Param ? 0 : 1;
  std::memcpy(body, &out, sizeof(out));
  bodyLen = sizeof(out);
}

// PageId::Notice (oled.cpp:282-329): all eight transient notices.
void buildNoticeBody(const UIState &uiState, uint8_t *body, uint8_t &bodyLen)
{
  rdisplay::NoticeBody out = {};
  out.kind = static_cast<uint8_t>(uiState.oledNoticeKind);
  out.voice = uiState.oledNoticeVoice;
  out.value = 0;
  copyString(out.word, sizeof(out.word), noticeWord(uiState.oledNoticeKind));
  char sub[16];
  noticeSub(uiState.oledNoticeKind, uiState.oledNoticeVoice,
            sub, sizeof(sub));
  copyString(out.sub, sizeof(out.sub), sub);
  std::memcpy(body, &out, sizeof(out));
  bodyLen = sizeof(out);
}

// PageId::HeldParam and PageId::ParamEdit (oled.cpp:341-383, 462-469 and
// displayParameterInfo, 583-626). mode: 0 = BASE view, 1 = LIVE, 2 = STEP.
void buildHeldParamBody(const UIState &uiState, ParamId id,
                        const Sequencer &sequence, const VoiceConfig *config,
                        bool showBase, uint8_t *body, uint8_t &bodyLen)
{
  rdisplay::HeldParamBody out = {};
  const bool selected = uiState.selectedStepForEdit >= 0;
  const uint8_t step = selected
                           ? normalizeStep(uiState.selectedStepForEdit)
                           : sequence.getCurrentStepForParameter(id);
  Step liveStep = sequence.getPlaybackStep(selected ? step : UINT8_MAX);
  composeHandValue(id, liveStep, config);

  out.paramId = static_cast<uint8_t>(id);
  out.voice = std::min<uint8_t>(uiState.selectedVoiceIndex, UIState::MAX_VOICES - 1);
  out.mode = showBase ? 0 : (selected ? 2 : 1);
  out.step = static_cast<int8_t>(step);
  const int mm = distanceSensor.getRawDistanceMm();
  out.distanceMm = clampDistanceMm(mm);
  out.handPresent = AppState::performanceInput.handPresent ? 1 : 0;

  char value[48] = "--";
  formatLaneValue(id, liveStep, config, value, sizeof(value));
  copyString(out.value, sizeof(out.value), value);
  // The base string is only carried while the BASE view is up — exactly the
  // case where the OLED prints it (oled.cpp:601).
  if (showBase && config != nullptr)
  {
    char base[48];
    formatLaneValue(id, MusicalValues::baseStep(*config), config, base, sizeof(base));
    copyString(out.base, sizeof(out.base), base);
  }
  std::memcpy(body, &out, sizeof(out));
  bodyLen = sizeof(out);
}

// PageId::SettingsToggles (oled.cpp:189-220, 721-748): the voice-parameter
// settings row, sourced from the notice snapshot fields.
void buildSettingsTogglesBody(const UIState &uiState, uint8_t *body, uint8_t &bodyLen)
{
  rdisplay::SettingsTogglesBody out = {};
  out.voice = uiState.voiceParameterNoticeVoice;
  copyString(out.name, sizeof(out.name), uiState.voiceParameterNoticeName);
  copyString(out.value, sizeof(out.value), uiState.voiceParameterNoticeValue);
  std::memcpy(body, &out, sizeof(out));
  bodyLen = sizeof(out);
}

// PageId::SettingsPresets (oled.cpp:635-678): the preset selection row.
void buildSettingsPresetsBody(const UIState &uiState, uint8_t *body, uint8_t &bodyLen)
{
  rdisplay::SettingsPresetsBody out = {};
  const uint8_t voice = std::min<uint8_t>(uiState.selectedVoiceIndex, UIState::MAX_VOICES - 1);
  out.selectedVoice = voice;
  out.count = VoicePresets::getPresetCount();
  for (uint8_t i = 0; i < UIState::MAX_VOICES; ++i)
  {
    out.presetIdx[i] = uiState.voicePresetIndices[i];
    out.changed[i] = uiState.voiceEditor.changed[i] ? 1 : 0;
  }
  copyString(out.selName, sizeof(out.selName),
             VoicePresets::getPresetName(uiState.voicePresetIndices[voice]));
  std::memcpy(body, &out, sizeof(out));
  bodyLen = sizeof(out);
}

// PageId::GateLength (oled.cpp:412-430): voice + gate step count.
void buildGateLengthBody(const UIState &uiState, const Sequencer &sequence,
                         uint8_t *body, uint8_t &bodyLen)
{
  rdisplay::GateLengthBody out = {};
  out.voice = std::min<uint8_t>(uiState.selectedVoiceIndex, UIState::MAX_VOICES - 1);
  out.length = sequence.getParameterStepCount(ParamId::Gate);
  std::memcpy(body, &out, sizeof(out));
  bodyLen = sizeof(out);
}

// PageId::StepEnv (oled.cpp:458-461, 508-545): the four ADSR lanes of the
// selected step, normalized to one byte each.
void buildStepEnvBody(const UIState &uiState, const Sequencer &sequence,
                      uint8_t *body, uint8_t &bodyLen)
{
  rdisplay::StepEnvBody out = {};
  const uint8_t step = normalizeStep(uiState.selectedStepForEdit);
  out.voice = std::min<uint8_t>(uiState.selectedVoiceIndex, UIState::MAX_VOICES - 1);
  out.step = step;
  out.lastLane = static_cast<uint8_t>(uiState.envFaderLane);
  constexpr ParamId kLanes[] = {ParamId::Attack, ParamId::Decay,
                                ParamId::Sustain, ParamId::Release};
  for (size_t i = 0; i < 4; ++i)
  {
    const float stored = sequence.getStepParameterValue(kLanes[i], step);
    const float clamped = std::min(1.0f, std::max(0.0f, stored));
    out.laneValue[i] = static_cast<uint8_t>(std::lround(clamped * 255.0f));
    out.laneFollows[i] = followsPatch(stored) ? 1 : 0;
  }
  std::memcpy(body, &out, sizeof(out));
  bodyLen = sizeof(out);
}

// PageId::Status (oled.cpp:470-503 plus the step dots at 901-956): the whole
// instrument at a glance.
void buildStatusBody(const UIState &uiState, const Sequencer &sequence,
                     const VoiceConfig *config, uint8_t *body, uint8_t &bodyLen)
{
  rdisplay::StatusBody out = {};
  out.bpmX10 = static_cast<uint16_t>(std::lround(uClock.getTempo() * 10.0f));
  out.playing = isClockRunning ? 1 : 0;
  out.currentStep = sequence.getCurrentStep();
  out.scaleIdx = static_cast<int8_t>(std::min<size_t>(currentScale, SCALES_COUNT - 1));
  out.shuffleIdx = uiState.currentShufflePatternIndex;

  const auto encoderId = VoiceEditor::encoderTarget();
  out.encTargetId = static_cast<uint8_t>(encoderId);
  if (config != nullptr)
  {
    const auto lane = VoiceEdit::sequenceLane(encoderId, *config);
    const Step playing = sequence.getPlaybackStep();
    const bool showBase = deadlineActive(uiState.encoderBaseViewUntil, millis());
    const Step values = showBase ? MusicalValues::baseStep(*config) : playing;
    char value[48] = "--";
    if (lane != ParamId::Count)
      MusicalValues::format(lane, values, *config, scaleRow(), uClock.getTempo(),
                            value, sizeof(value));
    else
      VoiceEdit::format(encoderId, *config, value, sizeof(value));
    copyString(out.encValue, sizeof(out.encValue), value);
  }
  else
    copyString(out.encValue, sizeof(out.encValue), "--");

  for (uint8_t i = 0; i < UIState::MAX_VOICES; ++i)
  {
    out.presetIdx[i] = uiState.voicePresetIndices[i];
    out.changed[i] = uiState.voiceEditor.changed[i] ? 1 : 0;
  }

  // Step dots as gate bits, MSB-first within each byte; only steps below the
  // gate length are defined (the rest read 0).
  const uint8_t gateLen = sequence.getParameterStepCount(ParamId::Gate);
  for (uint8_t i = 0; i < gateLen && i < sizeof(out.gateBits) * 8; ++i)
  {
    if (sequence.getStepParameterValue(ParamId::Gate, i) > 0.5f)
      out.gateBits[i / 8] |= static_cast<uint8_t>(0x80u >> (i % 8));
  }
  std::memcpy(body, &out, sizeof(out));
  bodyLen = sizeof(out);
}

/** Serialize the winning page with SEQ zeroed — the caller compares these
 * bytes against the shadow (page content only) and patches the real SEQ in
 * at send time. Returns the wire length, 0 on a packing failure. */
size_t buildFrame(const UIState &uiState, const SequencerView &sequencers,
                  VoiceManager *voiceManager, uint8_t *wire, size_t cap)
{
  const auto voice = std::min<uint8_t>(uiState.selectedVoiceIndex,
                                       static_cast<uint8_t>(sequencers.size() - 1));
  Sequencer &sequence = sequencers.clamped(voice);
  const auto *config = voiceManager
                           ? voiceManager->getVoiceConfig(voiceSystem.getVoiceId(voice))
                           : nullptr;
  const ParamId held = getHeldParameterParamId(uiState);
  const bool selected = uiState.selectedStepForEdit >= 0;
  const uint32_t now = millis();

  rdisplay::PageId page = rdisplay::PageId::Status;
  rdisplay::PageFrame frame = {};
  frame.protoVer = rdisplay::kProtoVerV1;
  frame.seq = 0; // patched at send time; the shadow compares content only
  frame.themeIdx = std::min<uint8_t>(static_cast<uint8_t>(uiState.currentThemeIndex),
                                     static_cast<uint8_t>(LEDTheme::COUNT) - 1);
  frame.theme = buildThemeBlock();

  // --- The gate chain, highest priority first (oled.cpp:258-506) ---
  if (uiState.voiceEditor.active)
  {
    page = rdisplay::PageId::VoiceEditor;
    buildVoiceEditorBody(uiState, voiceManager, frame.body, frame.bodyLen);
  }
  else if (deadlineActive(uiState.alchemyModeBannerUntil, now))
  {
    page = rdisplay::PageId::ModeBanner;
    buildModeBannerBody(uiState, frame.body, frame.bodyLen);
  }
  else if (deadlineActive(uiState.oledNoticeUntil, now) &&
           uiState.oledNoticeKind != UIState::OledNoticeKind::None)
  {
    page = rdisplay::PageId::Notice;
    buildNoticeBody(uiState, frame.body, frame.bodyLen);
  }
  else if (held != ParamId::Count)
  {
    page = rdisplay::PageId::HeldParam;
    // The OLED's held path never shows the BASE view (base=false at
    // oled.cpp:380) — BASE belongs to the encoder's own edit page below.
    buildHeldParamBody(uiState, held, sequence, config, /*showBase=*/false,
                       frame.body, frame.bodyLen);
  }
  else if (uiState.settingsMode && uiState.isVoiceParameterSettings())
  {
    page = rdisplay::PageId::SettingsToggles;
    buildSettingsTogglesBody(uiState, frame.body, frame.bodyLen);
  }
  else if (uiState.settingsMode)
  {
    page = rdisplay::PageId::SettingsPresets;
    buildSettingsPresetsBody(uiState, frame.body, frame.bodyLen);
  }
  else if (uiState.gateSeqLengthMode)
  {
    page = rdisplay::PageId::GateLength;
    buildGateLengthBody(uiState, sequence, frame.body, frame.bodyLen);
  }
  else
  {
    const auto editing = held != ParamId::Count ? held : uiState.currentEditParameter;
    const bool envelopePage = selected && (editing == ParamId::Count ||
                                deadlineActive(uiState.envViewUntil, now));
    if (envelopePage)
    {
      page = rdisplay::PageId::StepEnv;
      buildStepEnvBody(uiState, sequence, frame.body, frame.bodyLen);
    }
    else if (editing != ParamId::Count && (held != ParamId::Count || selected))
    {
      page = rdisplay::PageId::ParamEdit;
      const auto encoderId = VoiceEditor::encoderTarget();
      const ParamId encoderLane = config ? VoiceEdit::sequenceLane(encoderId, *config)
                                         : ParamId::Count;
      const bool showBase = config != nullptr &&
                            deadlineActive(uiState.encoderBaseViewUntil, now) &&
                            encoderLane == editing;
      buildHeldParamBody(uiState, editing, sequence, config, showBase,
                         frame.body, frame.bodyLen);
    }
    else
    {
      // The default page (oled.cpp:470-503): the whole instrument at a glance.
      page = rdisplay::PageId::Status;
      buildStatusBody(uiState, sequence, config, frame.body, frame.bodyLen);
    }
  }

  frame.page = page;
  return rdisplay::serializeFrame(frame, wire, cap);
}

} // namespace

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

bool RoundDisplayLink::sendFrame(const uint8_t *bytes, size_t len, uint8_t seq)
{
  // The SUM covers every byte before it, the SEQ slot included — so the
  // counter goes in first, then the SUM is recomputed over the patched
  // buffer (the shadow-comparable bytes carry SEQ zeroed).
  uint8_t wire[rdisplay::kMaxFrameBytes] = {0};
  // kMaxFrameBytes includes the register byte, so leave room for it in the
  // Arduino Wire transaction as well as in the local frame scratch buffer.
  if (len < 4 || len > sizeof(wire) || len > kMaxWireFrameBytes)
    return false;
  std::memcpy(wire, bytes, len);
  wire[1] = seq;
  wire[len - 1] = rdisplay::frameSum(wire, len - 1);

  Wire.beginTransmission(rdisplay::kDisplayAddress);
  const size_t regWritten = Wire.write(rdisplay::kRegFrame);
  const size_t frameWritten = regWritten == 1 ? Wire.write(wire, len) : 0;
  const uint8_t endStatus = Wire.endTransmission();
  if (regWritten == 1 && frameWritten == len && endStatus == 0)
  {
    // The register byte and the frame are queued as one I2C transaction. Do
    // not mark the shadow clean unless both writes and STOP succeeded.
    ++sentFrames_;
    consecutiveSendFailures_ = 0;
    lastSendMs_ = millis();
    return true;
  }
  // Drop and wait: the shadow stays dirty, the next display tick retries
  // (never an in-line retry — plan §2, AlchemyTiles.h:36-38 discipline).
  ++sendFailures_;
  ++consecutiveSendFailures_;
  if (consecutiveSendFailures_ >= kConsecutiveSendFailuresBeforeReprobe)
  {
    present_ = false;
    lastProbeMs_ = millis();
    consecutiveSendFailures_ = 0;
  }
  return false;
}

void RoundDisplayLink::update(const UIState &uiState,
                              const SequencerView &sequencers,
                              VoiceManager *voiceManager)
{
  const uint32_t now = millis();
  if (!present_)
  {
    // Panel absent: re-probe at most every kReprobeIntervalMs and stay quiet.
    if (now - lastProbeMs_ >= kReprobeIntervalMs)
    {
      lastProbeMs_ = now;
      present_ = probe();
    }
    if (!present_)
      return;
  }

  uint8_t bytes[rdisplay::kMaxFrameBytes] = {0};
  const size_t len = buildFrame(uiState, sequencers, voiceManager,
                                bytes, sizeof(bytes));
  if (len == 0)
    return;

  const bool differs = len != lastSentLen_ ||
                       std::memcmp(bytes, lastSent_, len) != 0;
  if (!differs && !dirty_)
  {
    // Unchanged content: only the heartbeat keeps the link provably alive.
    if (now - lastSendMs_ >= kHeartbeatIntervalMs)
      sendFrame(bytes, len, lastSeq_);
    return;
  }

  // SEQ advances only when the content changed; a dirty re-send keeps the
  // counter so the panel's equality rule sees the same frame.
  uint8_t seq = differs
                    ? static_cast<uint8_t>((lastSeq_ + 1) & rdisplay::kStatusSeqMask)
                    : lastSeq_;
  if (differs && hasPending_)
  {
    if (len == pendingLen_ && std::memcmp(bytes, pending_, len) == 0)
      seq = pendingSeq_; // retry the exact ambiguous payload with its SEQ
    else
      seq = static_cast<uint8_t>((pendingSeq_ + 1) & rdisplay::kStatusSeqMask);
  }
  if (sendFrame(bytes, len, seq))
  {
    std::memcpy(lastSent_, bytes, len);
    lastSentLen_ = len;
    lastSeq_ = seq;
    hasPending_ = false;
    pendingLen_ = 0;
    dirty_ = false;
  }
  else
  {
    std::memcpy(pending_, bytes, len);
    pendingLen_ = len;
    pendingSeq_ = seq;
    hasPending_ = true;
  }
}

void RoundDisplayLink::clear()
{
  // Invalidate the shadow only: the next update() rebuilds the frame and
  // re-sends it with the same SEQ, which the panel applies idempotently.
  dirty_ = true;
}
