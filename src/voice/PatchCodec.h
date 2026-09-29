#ifndef PICO2SEQ_PATCH_CODEC_H
#define PICO2SEQ_PATCH_CODEC_H

// PatchCodec — session save/load for one voice patch: capturePatch() flattens
// a VoiceConfig to a snapshot; applyPatch() rebuilds it from the factory
// preset (which owns the flash-resident layout/recipe pointers) plus saved
// values. Control/session thread only; no allocation.
#include "../pico2seq-core/persistence/ProjectSnapshot.h"

struct VoiceConfig;

namespace voicecodec
{
void capturePatch(const VoiceConfig &config, persistence::PatchSnapshot &out) noexcept;

// Rebuilds a patch from its factory preset (correct flash-resident descriptor
// pointers) overlaid with saved values. False + factory fallback when the save
// wants ENGINE_RECIPE but the preset carries no recipe (unreconstructible).
bool applyPatch(uint8_t presetIndex, const persistence::PatchSnapshot &in,
                VoiceConfig &out) noexcept;
} // namespace voicecodec

#endif
