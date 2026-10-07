#ifndef PICO2SEQ_PRESET_LINK_SERVICE_H
#define PICO2SEQ_PRESET_LINK_SERVICE_H

// PresetLinkService: the USB-serial end of the PC editor (Preset Studio).
// Musical role: plug the box into the computer and its presets can be edited, auditioned
// on a voice and sent over - no card, no drive, no button chord.
// Technical role: a Core 0 loop slice that reads the CDC port, feeds the portable frame
// parser and command session, and writes replies. Bytes that are not part of a frame go back
// to the caller (the bench console shares this port). Never blocks: a pass reads at most a
// few hundred bytes and one reply per request.

#include <cstdint>

namespace PresetLinkService
{
// Call every loop pass. `onConsoleByte` receives bytes that are not link traffic.
void poll(uint32_t nowMs, void (*onConsoleByte)(uint8_t));

// True while a bank upload owns the filesystem: the song autosave and Save must wait.
bool busy();

// True shortly after the editor last spoke, so a stray byte is not taken for a console command.
bool editorActive(uint32_t nowMs);
} // namespace PresetLinkService

#endif
