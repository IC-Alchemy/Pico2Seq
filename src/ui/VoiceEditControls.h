#pragma once
#include "../voice/VoiceEditParameters.h"

namespace VoiceEdit {
struct Input {
  int8_t voice = -1;
  int8_t group = 0;
  int8_t parameter = 0;
  bool reset = false;
  bool exit = false;
  bool clearEncoder = false;
};
// All editor interaction state belongs to UIState; policy has no hardware
// calls.
struct Controls {
  bool active = false;
  bool fine = false;
  bool help = false;
  bool waitRelease = true;
  bool changed[4] = {};
  Id cursor[4] = {Id::Note, Id::Note, Id::Note, Id::Note};
  uint8_t previousButtons = 0, previousVoices = 0;
  uint32_t resetStarted = 0;
  bool resetArmed = false;

  void enter() noexcept {
    active = true;
    fine = false;
    help = false;
    waitRelease = true;
    previousButtons = previousVoices = 0;
    resetArmed = false;
  }
  Input poll(uint8_t buttons, uint8_t voices, uint32_t now) noexcept {
    Input result{};
    if (waitRelease) {
      if (buttons == 0 && voices == 0)
        waitRelease = false;
      previousButtons = buttons;
      previousVoices = voices;
      return result;
    }
    const uint8_t pressed = buttons & ~previousButtons;
    const uint8_t voicePress = voices & ~previousVoices;
    previousButtons = buttons;
    previousVoices = voices;
    const bool newFine = (buttons & 16) != 0;
    result.clearEncoder = newFine != fine;
    fine = newFine;
    for (uint8_t i = 0; i < 4; ++i)
      if (voicePress & (1u << i)) {
        result.voice = i;
        result.clearEncoder = true;
        resetArmed = false;
        break;
      }
    if (pressed & 128) {
      result.exit = true;
      return result;
    }
    if (pressed & 64)
      help = !help;
    if (pressed & 1)
      result.group = -1;
    if (pressed & 2)
      result.group = 1;
    if (pressed & 4)
      result.parameter = -1;
    if (pressed & 8)
      result.parameter = 1;
    if (result.group || result.parameter) {
      result.clearEncoder = true;
      resetArmed = false;
    }
    if (pressed & 32) {
      resetStarted = now;
      resetArmed = true;
    }
    if (!(buttons & 32))
      resetArmed = false;
    if (resetArmed && now - resetStarted >= 700) {
      result.reset = true;
      resetArmed = false;
    }
    return result;
  }
};
} // namespace VoiceEdit
