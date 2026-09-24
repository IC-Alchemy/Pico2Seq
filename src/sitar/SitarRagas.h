#pragma once

#include <cstdint>

// SitarRagas.h — the four ragas Sitar Explorer plays (portable, Core 0).
// Musical role: a sitar is not played in Western scale degrees; the fretboard,
// the taraf bank and the ornaments all follow a raga. Each table carries one
// raga's aroh (the ascending degrees above Sa), its vadi (the note the player
// returns home to) and samvadi (the answering note) — the two degrees the LEDs
// emphasize, so the panel shows where the raga wants to rest.
// Technical role: constexpr data plus step-index helpers shared by the
// fretboard mapping, the LED palette and the console dump. No hardware, no
// Arduino, so it is host-testable.
//
// Why there is no avaroh (descending) table: the panel's fingerboard is a fixed
// ladder of frets, and every descent in this mode is a meend — a continuous
// glide that passes through pitches instead of stepping down a scale, so there
// is nothing for a table to say.
namespace Sitar
{

constexpr uint8_t kRagaCount = 4;

// Sa (tonic) of the fingerboard, in MIDI notes: 48 = C3 = 130.81 Hz. A sitar's
// main string sits near a low vocal range, and the two octaves of frets above
// it reach C5 (523 Hz).
constexpr int kSaMidiNote = 48;

// The drone strings, in semitones above Sa. Pa and Sa' one octave apart is the
// classic chikari tuning (a fourth between the two struck strings), and the
// kharaj rings two octaves under the melody string.
constexpr int kChikariPaSemitones = 7;  // 196 Hz
constexpr int kChikariSaSemitones = 12; // 262 Hz
constexpr int kKharajSemitones = -24;   // 65 Hz

// Fret count across the two "fingerboard" touch rows: 16 frets, which is two
// octaves of the raga's seven degrees plus the first fret of the third.
constexpr uint8_t kFretCount = 16;

struct Raga
{
    const char *name;
    const char *thaat; // Parent scale, for the OLED and the console dump
    // Ascending degrees in semitones above Sa, 8 entries: the 8th is the
    // octave, so the table closes the circle the way a player counts it.
    int8_t aroh[8];
    uint8_t vadi;    // Index into aroh: the raga's home note
    uint8_t samvadi; // Index into aroh: the note that answers it
};

// One raga per voice button. Names are the common transliterations; the thaat
// (parent scale) is what the aroh is derived from, which is why Yaman carries
// teevra Ma (6) and Khamaj omits Re in its ascent.
inline constexpr Raga kRagas[kRagaCount] = {
    {"Yaman", "Kalyan", {0, 2, 4, 6, 7, 9, 11, 12}, 2, 6},
    {"Bhairav", "Bhairav", {0, 1, 4, 5, 7, 8, 11, 12}, 5, 1},
    {"Bhairavi", "Bhairavi", {0, 1, 3, 5, 7, 8, 10, 12}, 3, 0},
    {"Khamaj", "Khamaj", {0, 2, 4, 5, 7, 9, 10, 12}, 6, 2},
};

// Sargam names for the eight aroh entries (the 8th is the octave, which a
// vadi/samvadi index may legally point at).
inline constexpr const char *kSargam[8] = {"Sa", "Re", "Ga", "Ma",
                                          "Pa", "Dha", "Ni", "Sa'"};

// Clamped raga lookup: an out-of-range index plays the first raga rather than
// reading past the table (the UI can cycle a counter past Count while pressed).
inline constexpr const Raga &raga(uint8_t index)
{
    return kRagas[index < kRagaCount ? index : 0];
}

// Semitones above Sa for fret 0..15: the raga's seven degrees per octave, so
// fret 0 is Sa, fret 7 is Sa' and fret 14 is Sa''. The neck reaches one fret
// past the second octave, which is what a sitar's neck does — the frets do not
// stop politely at the octave.
constexpr int fretSemitones(const Raga &table, uint8_t fret)
{
    const uint8_t index = fret < kFretCount ? fret : kFretCount - 1;
    return table.aroh[index % 7] + 12 * (index / 7);
}

// MIDI note of fret 0..15 of this raga.
constexpr int fretMidiNote(const Raga &table, uint8_t fret)
{
    return kSaMidiNote + fretSemitones(table, fret);
}

// Degree index (0..6) a fret lands on, for sargam naming and LED emphasis.
constexpr uint8_t fretDegree(uint8_t fret)
{
    return static_cast<uint8_t>((fret < kFretCount ? fret : kFretCount - 1) % 7);
}

// Sargam name of a fret, e.g. fret 9 of Yaman = "Ga" in the upper octave.
inline const char *fretName(uint8_t fret)
{
    return kSargam[fretDegree(fret)];
}

} // namespace Sitar
