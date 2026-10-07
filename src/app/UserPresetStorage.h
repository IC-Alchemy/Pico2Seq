#ifndef PICO2SEQ_USER_PRESET_STORAGE_H
#define PICO2SEQ_USER_PRESET_STORAGE_H

// UserPresetStorage: your own presets on flash (LittleFS), next to the song file.
// Musical role: presets sent from the PC editor stay on the box through power cycles and
// show up as extra pages in the preset browser.
// Technical role: binds the portable UserPresetStore to /presets.p2u (written through
// /presets.tmp and renamed, like the song save), and owns the one store and directory the
// browser, LEDs, OLED and PC link share. Core 0 only.

#include "../pico2seq-core/persistence/UserPresetBank.h"
#include "../presetlink/UserPresetStore.h"
#include <cstdint>

struct UIState;

namespace UserPresetStorage
{
// Mount happened (SessionStorage::begin); read the bank from flash. Returns the number of
// presets the browser can show (0 on a first boot or a damaged file).
uint16_t begin();

presetlink::UserPresetStore &store();
const persistence::UserPresetDirectory &directory();

// The name to show for a voice's sound: its user preset's name, else the factory name.
const char *voiceLabel(const UIState &state, uint8_t voice);

// A voice's user-preset origin survives a save/load in the two spare bytes of its patch:
// tag = slot + 1 (0 = factory), check = a hash of the name at that time, so a slot that
// holds a different preset after the bank changed is not mistaken for the original.
uint8_t sessionTag(const UIState &state, uint8_t voice);
uint8_t sessionCheck(const UIState &state, uint8_t voice);
// Restores UIState::voiceUserSlot/voiceUserName from a saved tag; clears them if the slot
// no longer holds the same preset (or the base preset differs).
void restoreFromSession(UIState &state, uint8_t voice, uint8_t tag, uint8_t check);

// Keeps each voice's origin truthful after the bank was replaced.
void refreshAfterBankChange(UIState &state);
} // namespace UserPresetStorage

#endif
