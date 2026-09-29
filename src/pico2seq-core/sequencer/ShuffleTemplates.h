#ifndef SHUFFLE_TEMPLATES_H
#define SHUFFLE_TEMPLATES_H

#include <cstdint>

const int NUM_SHUFFLE_TEMPLATES = 16;
const int SHUFFLE_TEMPLATE_SIZE = 16; // One bar of 16ths; all rows must fill it

struct ShuffleTemplate
{
    const char *name; // UI label shown on the OLED
    int8_t ticks[SHUFFLE_TEMPLATE_SIZE]; // Per-16th offset; keep within +/-120
};

// ShuffleTemplates: per-16th micro-timing grooves that make the grid feel human.
// Offsets are ticks at 480 PPQN (120/step); + delays the step (laid-back),
// - pulls it early. Even steps stay on the beat, odd steps carry the swing.
const ShuffleTemplate shuffleTemplates[NUM_SHUFFLE_TEMPLATES] = {

    {"No Shuffle", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"Teeny Swing", {0, 5, 0, 6, 0, 5, 0, 6, 0, 5, 0, 6, 0, 5, 0, 6}},
    {"Lil' Swing (53%)", {0, 10, 0, 11, 0, 10, 0, 9, 0, 10, 0, 10, 0, 11, 0, 10}},
    {"Neg' Swing (53%)", {0, -10, 0, -11, 0, -10, 0, -9, 0, -10, 0, -10, 0, -11, 0, -10}},
    {"CornBread", {0, 13, 0, 14, 0, 13, 0, 14, 0, 15, 0, 13, 0, 15, 0, 14}},

    // Light swing: offbeats drag just enough to loosen straight 16ths.
    {"Swing (55%)", {0, 17, 0, 18, 0, 17, 0, 18, 0, 17, 0, 18, 0, 18, 0, 17}},
    {"Swing (56%)", {0, 19, 0, 18, 0, 19, 0, 20, 0, 19, 0, 20, 0, 23, 0, 20}},
    {"Swing (57%)", {0, 23, 0, 22, 0, 23, 0, 23, 0, 22, 0, 23, 0, 22, 0, 23}},
    {"Swing (60%)", {0, 30, 0, 28, 0, 30, 0, 28, 0, 31, 0, 29, 0, 31, 0, 28}},

    {"Big Swang (60%)", {0, 36, 0, 34, 0, 36, 0, 33, 0, 36, 0, 36, 0, 36, 0, 36}},
    {"Phatty Swang", {0, 40, 0, 40, 0, 40, 0, 40, 0, 40, 0, 40, 0, 40, 0, 40}},
    {"Big Swang (62%)", {0, 50, 0, 48, 0, 50, 0, 48, 0, 50, 0, 48, 0, 50, 0, 48}},
    {"Humanize 1", {0, -1, 3, 1, 0, -2, 1, 0, -1, 1, 0, 3, -1, 2, 1, -2}},
    {"Humanize 2", {0, -1, 0, 2, 1, 2, 0, -2, -1, 0, 1, 1, 2, 2, 3, 1}},

    {"Hip-Hop", {0, 40, 0, 22, 0, 40, 0, 22, 0, 40, 0, 22, 0, 40, 0, 22}},
    {"Funk Groove", {0, 35, 0, 20, 0, 30, 0, 20, 0, 35, 0, 20, 0, 30, 0, 20}}

};

// Name lookup for the OLED; out-of-range index means a corrupt session slot.
inline const char *getShuffleTemplateName(uint8_t index)
{
    if (index >= NUM_SHUFFLE_TEMPLATES)
    {
        return "Invalid";
    }
    return shuffleTemplates[index].name;
}
#endif // SHUFFLE_TEMPLATES_H
