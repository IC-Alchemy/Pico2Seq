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
// Bit positions inside PatchSnapshot::flags (the table beside that struct in
// ProjectSnapshot.h). Public because the user preset codec must set and test the very bit the
// song loader reads; a second copy of the layout could only drift.
inline constexpr uint8_t kUsePatchBases = 1u << 0;
inline constexpr uint8_t kBaseGate = 1u << 1;
inline constexpr uint8_t kBaseSlide = 1u << 2;
inline constexpr uint8_t kRecipeRetrigger = 1u << 3;
inline constexpr uint8_t kHasOverdrive = 1u << 4;
inline constexpr uint8_t kHasEnvelope = 1u << 5;
inline constexpr uint8_t kHasFilter = 1u << 6;
inline constexpr uint8_t kEnabled = 1u << 7;

void capturePatch(const VoiceConfig &config, persistence::PatchSnapshot &out) noexcept;

// Rebuilds a patch from its factory preset (correct flash-resident descriptor
// pointers) overlaid with saved values. False + factory fallback when the save
// wants ENGINE_RECIPE but the preset carries no recipe (unreconstructible).
bool applyPatch(uint8_t presetIndex, const persistence::PatchSnapshot &in,
                VoiceConfig &out) noexcept;
} // namespace voicecodec

#endif
