// UserPresetCodec - turns an uploaded UserPresetRecord into something safe to play.
// Musical role: a preset made on the PC sounds exactly as designed, or is refused with a
// reason the editor can show ("Warm Bass: Resonance 1.4 is above 1.0"). Nothing half-valid
// ever reaches the audio core.
// Technical role: strict validation against the editor's own limits (VoiceEdit::limits),
// canonicalisation of the derived bytes, and the record <-> VoiceConfig bridge built on
// voicecodec::applyPatch, so a user preset loads through the same path as a saved song.
// Control thread only; no allocation.
#pragma once

#include "../pico2seq-core/persistence/UserPresetBank.h"

struct VoiceConfig;

namespace usercodec
{

enum class Problem : uint8_t
{
    None,
    BadName,        // empty, over 15 characters, non-ASCII, or dirty trailing bytes
    BadPlace,       // page/pad outside the user pages
    BadBase,        // base factory preset does not exist
    BadReserved,    // flags/reserved bytes not zero
    BadField,       // `field` indexes patchfields::field(): not finite, out of range or not an allowed choice
    EngineNeedsRecipe // engine = recipe, but the base preset carries no recipe
};

struct Check
{
    Problem problem = Problem::None;
    uint8_t field = 0xFF; // BadField: row in patchfields
    bool ok() const noexcept { return problem == Problem::None; }
};

// The shortest distance the validator tolerates beyond a limit (float representation of
// decimal limits like 0.001 must not reject their own bound).
inline constexpr float kLimitSlack = 1e-4f;

// Strict: rejects instead of clamping, so an editor bug is loud. Does not modify.
Check validate(const persistence::UserPresetRecord &record) noexcept;

// Fills the bytes the editor does not own: patch.presetIndex mirrors baseIndex, the
// use-patch-bases flag is set, paramSet follows the engine (and hard-sync waveforms) the
// way the on-device editor derives it, quantised bases are rounded, reserved bytes and
// name padding are zeroed. Call before validate() on data from outside.
void canonicalize(persistence::UserPresetRecord &record) noexcept;

// Rebuilds the playable voice from the record's base factory preset plus its values.
// False if the record cannot be played (invalid base, recipe engine without recipe);
// `out` then holds the base preset's factory sound.
bool toConfig(const persistence::UserPresetRecord &record, VoiceConfig &out) noexcept;

// Captures a running voice as a record (name/place/colour are left for the caller).
void fromConfig(const VoiceConfig &config, uint8_t baseIndex,
                persistence::UserPresetRecord &out) noexcept;

// A record holding a factory preset's sound, for the editor to start from.
void fromFactory(uint8_t presetIndex, persistence::UserPresetRecord &out) noexcept;

const char *problemName(Problem problem) noexcept;

} // namespace usercodec
