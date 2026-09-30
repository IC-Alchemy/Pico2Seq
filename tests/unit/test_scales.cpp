#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "scales/scales.h"
#include "tuning/TuningState.h"

TEST_CASE("Scale table has correct number of scales", "[scales]") {
    REQUIRE(CLASSIC_SCALES_COUNT == 18);
    REQUIRE(SCALES_COUNT == 47); // the eighteen classic rows plus 29 tuned ones
}

TEST_CASE("Each scale has 48 step entries", "[scales]") {
    REQUIRE(SCALE_STEPS == 48);
}

TEST_CASE("Scale step[0] is always 0 semitones (root)", "[scales]") {
    for (size_t s = 0; s < SCALES_COUNT; ++s) {
        REQUIRE(scale[s][0] == 0);
    }
}

TEST_CASE("Scale values are monotonically non-decreasing", "[scales]") {
    for (size_t s = 0; s < SCALES_COUNT; ++s) {
        for (size_t i = 1; i < SCALE_STEPS; ++i) {
            INFO("Scale " << s << " step " << i);
            REQUIRE(scale[s][i] >= scale[s][i - 1]);
        }
    }
}

TEST_CASE("Classic scale values are within MIDI semitone range [0, 127]", "[scales]") {
    // Native rows hold degrees of a tuning, which can exceed 127 (six periods of 53-EDO).
    for (size_t s = 0; s < SCALES_COUNT; ++s) {
        if (scaleIsNative(s)) continue;
        for (size_t i = 0; i < SCALE_STEPS; ++i) {
            REQUIRE(scale[s][i] >= 0);
            REQUIRE(scale[s][i] <= 127);
        }
    }
}

TEST_CASE("Every scale has a short name that fits the OLED", "[scales]") {
    for (size_t s = 0; s < SCALES_COUNT; ++s) {
        INFO("Scale " << s << " " << scaleNames[s]);
        REQUIRE(scaleShortNames[s] != nullptr);
        REQUIRE(scaleShortNames[s][0] != '\0');
        REQUIRE(std::strlen(scaleShortNames[s]) <= 10);
        REQUIRE(std::strlen(scaleNames[s]) <= 21);
    }
}

TEST_CASE("Chromatic scale has consecutive semitones", "[scales]") {
    const size_t chromatic_idx = 12;
    REQUIRE(std::string(scaleNames[chromatic_idx]) == "Chromatic");
    for (size_t i = 0; i < 12; ++i) {
        REQUIRE(scale[chromatic_idx][i] == (int)i);
    }
}

TEST_CASE("Ionian (major) scale has correct intervals", "[scales]") {
    // Major scale: 0 2 4 5 7 9 11 12 ...
    const int expected[] = {0, 2, 4, 5, 7, 9, 11, 12};
    for (int i = 0; i < 8; ++i) {
        REQUIRE(scale[0][i] == expected[i]);
    }
}

TEST_CASE("Every seven-note classic scale repeats at the octave in all 48 steps", "[scales]") {
    // Hand-typed rows once drifted from their pattern: Ionian step 38 read 66 (F#) for 65 (F),
    // Aeolian step 36 read 61 for 62 and Locrian steps 11, 18 and 21 read 19, 31 and 35.
    struct Mode { size_t row; int degrees[7]; };
    const Mode modes[] = {
        {0, {0, 2, 4, 5, 7, 9, 11}},   // Ionian
        {1, {0, 2, 3, 5, 7, 9, 10}},   // Dorian
        {2, {0, 1, 3, 5, 7, 8, 10}},   // Phrygian
        {3, {0, 2, 4, 6, 7, 9, 11}},   // Lydian
        {4, {0, 2, 4, 5, 7, 9, 10}},   // Mixolydian
        {5, {0, 2, 3, 5, 7, 8, 10}},   // Aeolian
        {6, {0, 1, 3, 5, 6, 8, 10}},   // Locrian
        {8, {0, 1, 4, 5, 7, 8, 10}},   // Phrygian Dominant
        {9, {0, 2, 4, 6, 7, 9, 10}},   // Lydian Dominant
        {10, {0, 2, 3, 5, 7, 8, 11}},  // Harmonic Minor
    };
    for (const Mode &mode : modes) {
        for (int i = 0; i < static_cast<int>(SCALE_STEPS); ++i) {
            INFO(scaleNames[mode.row] << " step " << i);
            REQUIRE(scale[mode.row][i] == std::min(12 * (i / 7) + mode.degrees[i % 7], 72));
        }
    }
}

TEST_CASE("Pentatonic Minor doubles each note of its pattern up to the top of the grid", "[scales]") {
    // Steps 0..41 are five notes per octave, each written twice; the top of the row holds
    // the last notes (steps 42 and up are not checked).
    const int degrees[5] = {0, 3, 5, 7, 10};
    for (int i = 0; i < 42; ++i) {
        const int note = i / 2;
        INFO("Pentatonic Minor step " << i);
        REQUIRE(scale[7][i] == 12 * (note / 5) + degrees[note % 5]);
    }
}

TEST_CASE("Scale names array is non-null and non-empty", "[scales]") {
    for (size_t s = 0; s < SCALES_COUNT; ++s) {
        REQUIRE(scaleNames[s] != nullptr);
        REQUIRE(scaleNames[s][0] != '\0');
    }
}

TEST_CASE("The original thirteen scales keep their numbers", "[scales]") {
    // Saved songs store the scale by index: new scales may only be appended.
    const char *original[] = {"Ionian Major", "Dorian", "Phrygian", "Lydian", "Mixolydian",
                              "Aeolian Minor", "Locrian", "Pentatonic Minor", "Phrygian Dominant",
                              "Lydian Dominant", "Harmonic Minor", "Wholetone", "Chromatic"};
    for (size_t s = 0; s < 13; ++s) {
        INFO("Scale " << s);
        REQUIRE(std::string(scaleNames[s]) == original[s]);
    }
}

TEST_CASE("All Degrees and the tuned scales are the native rows, every other row is classic", "[scales]") {
    REQUIRE(std::string(scaleNames[SCALE_ALL_DEGREES]) == "All Degrees");
    for (size_t s = 0; s < SCALES_COUNT; ++s) {
        INFO("Scale " << s);
        REQUIRE(scaleIsNative(s) == (s == SCALE_ALL_DEGREES || s >= SCALE_FIRST_TUNED));
    }
    REQUIRE_FALSE(scaleIsNative(SCALES_COUNT));
    REQUIRE_FALSE(scaleIsNative(200));
    REQUIRE_FALSE(scaleIsNative(64)); // past the mask, not a shift by the width
    for (size_t i = 0; i < SCALE_STEPS; ++i)
        REQUIRE(scale[SCALE_ALL_DEGREES][i] == static_cast<int>(i));
}

TEST_CASE("The tuned scales are built from their tuning's own degrees", "[scales]") {
    struct Tuned { const char *name; int period; std::vector<int> pattern; };
    const Tuned tuned[] = {
        {"Maqam Rast", 24, {0, 4, 7, 10, 14, 18, 21}},
        {"Maqam Bayati", 24, {0, 3, 6, 10, 14, 16, 20}},
        {"Maqam Hijaz", 24, {0, 2, 8, 10, 14, 16, 20}},
        {"Maqam Saba", 24, {0, 3, 6, 8, 14, 16, 20}},
        {"19-EDO Major", 19, {0, 3, 6, 8, 11, 14, 17}},
        {"19-EDO Minor", 19, {0, 3, 5, 8, 11, 13, 16}},
        {"19-EDO Pentatonic", 19, {0, 3, 6, 11, 14}},
        {"31-EDO Major", 31, {0, 5, 10, 13, 18, 23, 28}},
        {"31-EDO Minor", 31, {0, 5, 8, 13, 18, 21, 26}},
        {"31-EDO Pentatonic", 31, {0, 5, 10, 18, 23}},
        {"22-EDO Major", 22, {0, 4, 8, 9, 13, 17, 21}},
        {"22-EDO Minor", 22, {0, 4, 5, 9, 13, 14, 18}},
        {"22-EDO Pentatonic", 22, {0, 4, 8, 13, 17}},
        {"17-EDO Major", 17, {0, 3, 6, 7, 10, 13, 16}},
        {"17-EDO Minor", 17, {0, 3, 4, 7, 10, 11, 14}},
        {"15-EDO Heptatonic", 15, {0, 2, 4, 6, 8, 11, 13}},
        {"15-EDO Pentatonic", 15, {0, 3, 6, 9, 12}},
        {"10-EDO Pentatonic", 10, {0, 2, 4, 6, 8}},
        {"41-EDO Major", 41, {0, 7, 14, 17, 24, 31, 38}},
        {"41-EDO Minor", 41, {0, 7, 10, 17, 24, 27, 34}},
        {"41-EDO Pentatonic", 41, {0, 7, 14, 24, 31}},
        {"53-EDO Major", 53, {0, 9, 18, 22, 31, 40, 49}},
        {"53-EDO Minor", 53, {0, 9, 13, 22, 31, 35, 44}},
        {"53-EDO Pentatonic", 53, {0, 9, 18, 31, 40}},
        {"Overtone Heptatonic", 16, {0, 2, 4, 6, 8, 11, 14}},
        {"Overtone Pentatonic", 16, {0, 2, 4, 8, 11}},
        {"Partch Major", 43, {0, 8, 14, 18, 25, 31, 39}},
        {"Partch Minor", 43, {0, 8, 12, 18, 25, 29, 36}},
        {"Bohlen-Pierce Lambda", 13, {0, 2, 3, 4, 6, 7, 9, 10, 12}},
    };
    REQUIRE(sizeof(tuned) / sizeof(tuned[0]) == SCALES_COUNT - SCALE_FIRST_TUNED);
    for (size_t t = 0; t < sizeof(tuned) / sizeof(tuned[0]); ++t) {
        const size_t index = SCALE_FIRST_TUNED + t;
        const Tuned &spec = tuned[t];
        INFO(spec.name);
        REQUIRE(std::string(scaleNames[index]) == spec.name);
        const int notes = static_cast<int>(spec.pattern.size());
        for (size_t i = 0; i < SCALE_STEPS; ++i) {
            // Each period repeats the pattern; the tail holds six periods up, like the classic rows.
            const int expected = std::min(spec.period * (static_cast<int>(i) / notes) +
                                              spec.pattern[i % notes],
                                          6 * spec.period);
            INFO("step " << i);
            REQUIRE(scale[index][i] == expected);
        }
        // The notes of one period, counted the way the arpeggiator counts them.
        REQUIRE(scaleNotesPerPeriod(scale[index], spec.period) == notes);
    }
}

TEST_CASE("Notes per period count distinct notes below the period", "[scales]") {
    REQUIRE(scaleNotesPerPeriod(scale[0], 12) == 7);
    REQUIRE(scaleNotesPerPeriod(scale[SCALE_ALL_DEGREES], 24) == 24);
    REQUIRE(scaleNotesPerPeriod(scale[SCALE_ALL_DEGREES], 13) == 13);
    REQUIRE(scaleNotesPerPeriod(nullptr, 12) == 0);
    REQUIRE(scaleNotesPerPeriod(scale[0], 0) == 0);
    REQUIRE(scaleNotesPerOctave(scale[0]) == 7);
}

TEST_CASE("Hindustani thaats are seven-note scales with the published intervals", "[scales]") {
    struct Thaat { const char *name; int offsets[7]; };
    const Thaat thaats[] = {
        {"Bhairav Thaat", {0, 1, 4, 5, 7, 8, 11}},
        {"Marwa Thaat", {0, 1, 4, 6, 7, 9, 11}},
        {"Poorvi Thaat", {0, 1, 4, 6, 7, 8, 11}},
        {"Todi Thaat", {0, 1, 3, 6, 7, 8, 11}},
    };
    for (size_t t = 0; t < 4; ++t) {
        const size_t index = 14 + t;
        INFO(thaats[t].name);
        REQUIRE(std::string(scaleNames[index]) == thaats[t].name);
        REQUIRE(scaleNotesPerOctave(scale[index]) == 7);
        for (size_t i = 0; i < SCALE_STEPS; ++i) {
            const int expected = std::min(12 * static_cast<int>(i / 7) + thaats[t].offsets[i % 7], 72);
            INFO("step " << i);
            REQUIRE(scale[index][i] == expected);
        }
    }
}

TEST_CASE("currentScaleNotesPerOctave counts the playing scale in the playing tuning", "[scales][tuning]") {
    const uint8_t savedScale = currentScale;
    const tuning::Selection savedTuning = tuningSelection;
    const auto scaleNamed = [](const char *name) {
        for (size_t i = 0; i < SCALES_COUNT; ++i)
            if (std::string(scaleNames[i]) == name) return static_cast<uint8_t>(i);
        FAIL("no scale named " << name);
        return uint8_t{0};
    };

    tuningSelection = tuning::Selection{};
    currentScale = scaleNamed("Ionian Major");
    REQUIRE(currentScaleNotesPerOctave() == 7);

    tuningSelection.tuningId = 1; // 24-EDO: Rast is seven notes among the 24, counted in degrees
    currentScale = scaleNamed("Maqam Rast");
    REQUIRE(currentScaleNotesPerOctave() == 7);
    currentScale = scaleNamed("All Degrees");
    REQUIRE(currentScaleNotesPerOctave() == 24);

    tuningSelection.tuningId = 96; // 22 Shruti: a thaat is seven of twelve semitone slots
    currentScale = scaleNamed("Bhairav Thaat");
    REQUIRE(currentScaleNotesPerOctave() == 7);
    currentScale = scaleNamed("All Degrees");
    REQUIRE(currentScaleNotesPerOctave() == 22);

    tuningSelection.tuningId = 4; // 22-EDO pentatonic
    currentScale = scaleNamed("22-EDO Pentatonic");
    REQUIRE(currentScaleNotesPerOctave() == 5);

    currentScale = 250; // out of range: the last scale, never a read past the table
    REQUIRE_NOTHROW(currentScaleNotesPerOctave());

    currentScale = savedScale;
    tuningSelection = savedTuning;
}
