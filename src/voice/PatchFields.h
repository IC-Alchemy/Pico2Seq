// PatchFields.h - which patch values a user preset can set, and where each lives.
// Musical role: the complete list of knobs the PC editor shows. Technical role: one
// row per PatchSnapshot value (key, byte offset, type, editor-catalog entry, when it
// applies). The upload validator walks this table, the schema dumped for the PC editor
// is generated from it, and tests pin it - so firmware and editor cannot disagree about
// the layout. Labels, units and ranges come from VoiceEdit::parameter(), never copied.
//
// Changing a row, or the PatchSnapshot layout, means bumping kLayoutVersion and
// regenerating the editor's resources (docs/preset-studio.md, "Changing the layout").
// Portable C++ - no Arduino/hardware includes here.
#pragma once

#include "VoiceEditParameters.h"
#include "../pico2seq-core/persistence/ProjectSnapshot.h"
#include <cstddef>
#include <cstdint>
#include <limits>

namespace patchfields
{

// Bump when a row is added, removed, moved or re-typed. The editor refuses a device
// whose layout version differs from the one it was built against.
inline constexpr uint16_t kLayoutVersion = 1;

enum class Type : uint8_t
{
    Float,  // IEEE-754 single
    Int32,  // signed 32-bit (harmony intervals)
    Byte,   // unsigned 8-bit (enums, counts)
    Flag    // one bit of PatchSnapshot::flags
};

// When the editor offers a value. Mirrors VoiceEdit::available() for the patch values;
// the editor greys a value out (rather than hiding it) and says why. Tests replay
// available() against these rules.
enum class Show : uint8_t
{
    Always,
    EngineOsc,       // oscillator engine
    EngineWaveguide, // plucked-string engine
    EngineNoiseFx,   // noise engine
    EngineHypersaw,  // seven-voice saw engine
    EngineRecipe,    // fixed-recipe engine
    Osc1,            // oscillator engine with at least 1 / 2 / 3 oscillators
    Osc2,
    Osc3,
    Pulse1,          // ... whose waveform has a pulse width
    Pulse2,
    Pulse3,
    Harmony1,        // oscillator 1 harmony; also tunes the other pitched engines
    Filter,          // main filter on
    Ladder,          // main filter on and ladder topology
    Overdrive,       // overdrive on
    Envelope,        // envelope on
    RecipeFm,        // feedback-FM recipe only
    RecipePhase,     // phase-morph recipe only
    RecipeSpectral,  // spectral DSF recipe only
    RecipePrism      // prism recipe only
};

// "No override": take this limit from the editor catalog. A finite sentinel, not NaN -
// the firmware is built with -ffast-math, which may assume NaN never occurs.
inline constexpr float kCatalog = std::numeric_limits<float>::lowest();

struct Field
{
    const char *key;      // stable id in files and the schema, "osc1.level"
    uint16_t offset;      // byte offset inside PatchSnapshot
    Type type;
    uint8_t mask;         // Flag: bit mask inside PatchSnapshot::flags, else 0
    VoiceEdit::Id edit;   // editor catalog row supplying label, group, unit and limits
    Show show;
    // Where the stored value may differ from the on-device editor's range. The high-pass
    // cutoff stores 0 for "off" (the voice bypasses it at 20 Hz or below), which the
    // editor's 20..20000 Hz knob never shows.
    float minimum = kCatalog;
    float maximum = kCatalog;
};

size_t count() noexcept;
const Field &field(size_t index) noexcept;
// -1 when the key is unknown.
int indexOfKey(const char *key) noexcept;

// Read/write one value as a float, whatever its stored type. write() rounds integer
// types and does not range-check (that is the validator's job).
float read(const persistence::PatchSnapshot &patch, const Field &f) noexcept;
void write(persistence::PatchSnapshot &patch, const Field &f, float value) noexcept;

// The range a stored value must lie in under `config`: VoiceEdit::limits() with this row's
// overrides applied. Not meaningful for Flag rows or the waveform ids (see UserPresetCodec).
void limits(const Field &f, const VoiceConfig &config, float &minimum, float &maximum) noexcept;

const char *showName(Show show) noexcept;
// Whether `show` holds for this configuration (the table-driven twin of
// VoiceEdit::available(), used by the schema tests).
bool shown(Show show, const VoiceConfig &config) noexcept;

// FNV-1a over every row (key, offset, type, mask, editor id, rule). Pinned by a test so a
// table edit cannot slip past without a kLayoutVersion bump.
uint32_t tableHash() noexcept;

// IEEE-754 finite test that survives -ffast-math (std::isfinite compiles to true there).
bool finiteFloat(float value) noexcept;

} // namespace patchfields
