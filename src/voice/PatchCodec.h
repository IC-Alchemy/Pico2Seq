#ifndef PICO2SEQ_PATCH_CODEC_H
#define PICO2SEQ_PATCH_CODEC_H

#include "../pico2seq-core/persistence/ProjectSnapshot.h"

struct VoiceConfig;

namespace voicecodec
{
void capturePatch(const VoiceConfig &config, persistence::PatchSnapshot &out) noexcept;

// Rebuilds a full config: starts from the factory preset (correct
// flash-resident descriptor pointers), overlays all saved value fields, then
// re-derives the layout pointer. Returns false if the saved engine is
// ENGINE_RECIPE but the preset carries no recipe (cannot reconstruct) — *out
// is then reset to the unmodified factory preset.
bool applyPatch(uint8_t presetIndex, const persistence::PatchSnapshot &in,
                VoiceConfig &out) noexcept;
} // namespace voicecodec

#endif
