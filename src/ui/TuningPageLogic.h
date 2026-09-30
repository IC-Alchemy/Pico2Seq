#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../pico2seq-core/scales/scales.h"
#include "../pico2seq-core/tuning/Tuning.h"
#include "../pico2seq-core/tuning/TuningScales.h"
#include "TuningPageControls.h"

// TuningPageLogic.h - what the Tuning page's controls mean (Core 0, pure functions).
//
// Musical role: turns a pad, voice button, encoder detent or fader position into a change
// of the one global tuning::Selection (tuning, tonic, A4), the scale that plays in it, and the
// favourites Bank, and names everything for the OLED. Choosing a tuning also chooses a scale
// that belongs to it (tuning/TuningScales.h), so a twelve-note mode is never left playing
// across 24 notes per octave.
// Technical role: no hardware, no globals, no allocation - every function takes the state it
// edits, so the same code is unit-tested on the host and called by AlchemyControlBridge /
// UIEventHandler / the OLED on the RP2350.
namespace TuningPage {

static_assert(kHotSlots == tuning::kHotSlots, "voice buttons and hot favourites agree");
static_assert(kPadCount == tuning::kMaxLibrary, "one pad per library slot");
static_assert(kScaleKeys <= tuning::kMaxSetScales, "every scale button can reach a scale");

constexpr uint8_t kNoTuning = tuning::kEmptySlot;

// --- Which tuning is on which pad -------------------------------------------------------

// The library is laid out on the pads in library order, which is family-grouped: Equal, Just,
// Temperament, Indian, Xeno. Pads past the last tuning stay dark.
inline uint8_t padTuningId(uint8_t pad) noexcept {
    if (pad >= kPadCount || pad >= tuning::libraryCount()) return kNoTuning;
    return tuning::libraryAt(pad).id;
}

// The pad a tuning sits on, or kNoControl.
inline uint8_t padOfTuning(uint8_t tuningId) noexcept {
    const int index = tuning::libraryIndexOf(tuningId);
    return index >= 0 ? static_cast<uint8_t>(index) : kNoControl;
}

// First hot slot (0..3) holding this tuning, or kNoTuning.
inline uint8_t hotSlotOf(const tuning::Bank &bank, uint8_t tuningId) noexcept {
    for (uint8_t i = 0; i < tuning::kFavoriteSlots; ++i)
        if (bank.favorites[i] == tuningId) return i;
    return kNoTuning;
}

// LED hue (FastLED's 0..255 wheel) of each family, in tuning::Family order.
constexpr uint8_t kFamilyHue[tuning::kFamilyCount] = {160, 40, 205, 96, 0}; // blue, amber, magenta, green, red

inline uint8_t familyHue(uint8_t family) noexcept {
    return kFamilyHue[family < tuning::kFamilyCount ? family : 0];
}

// What one pad looks like, for the LED matrix: dark or lit in its family's colour, playing
// now, the A/B partner, and whether a voice button holds it as a hot favourite.
struct PadView {
    uint8_t tuningId = kNoTuning;
    bool exists = false;
    bool current = false;
    bool previous = false;
    uint8_t hotSlot = kNoTuning; // 0..3 when a voice button holds it
    uint8_t family = 0;
};

inline PadView padView(uint8_t pad, const tuning::Selection &selection,
                       const tuning::Bank &bank) noexcept {
    PadView view;
    view.tuningId = padTuningId(pad);
    view.exists = view.tuningId != kNoTuning;
    if (!view.exists) return view;
    view.family = static_cast<uint8_t>(tuning::resolve(view.tuningId).family);
    view.current = view.tuningId == selection.tuningId;
    view.previous = view.tuningId == bank.previousId && !view.current;
    view.hotSlot = hotSlotOf(bank, view.tuningId);
    return view;
}

// --- Acting on gestures ----------------------------------------------------------------

enum class Change : uint8_t {
    None,        // nothing happened (dark pad, already current, nothing to swap)
    Applied,     // a different tuning is now playing (scaleChanged says if its scale moved too)
    ScaleChosen, // a scale of the playing tuning was chosen
    Starred,     // a hot favourite was stored
    Unstarred,   // a hot favourite was emptied
    Empty,       // recalled a hot favourite that holds nothing
    NoScale,     // a scale button past the end of this tuning's scales
};

struct Result {
    Change change = Change::None;
    uint8_t tuningId = kNoTuning;      // the tuning concerned
    uint8_t slot = kNoTuning;          // the hot slot concerned, when there is one
    bool scaleChanged = false;         // the playing scale is not the one from before the gesture
    uint8_t scaleIndex = 0;            // the scale playing after the gesture
};

// Every action that can change the tuning goes through here, so the scale always follows.
inline Result appliedWithScale(tuning::Selection &selection, tuning::Bank &bank, uint8_t id,
                               uint8_t &scaleIndex) noexcept {
    Result r;
    r.tuningId = id;
    const uint8_t before = scaleIndex;
    if (tuning::applyTuningWithScale(selection, bank, id, scaleIndex)) r.change = Change::Applied;
    r.scaleChanged = scaleIndex != before;
    r.scaleIndex = scaleIndex;
    return r;
}

// Tap a pad: choose the tuning under it. The scale follows (see tuning/TuningScales.h).
inline Result padTap(uint8_t pad, tuning::Selection &selection, tuning::Bank &bank,
                     uint8_t &scaleIndex) noexcept {
    const uint8_t id = padTuningId(pad);
    if (id == kNoTuning) {
        Result r;
        r.scaleIndex = scaleIndex;
        return r;
    }
    return appliedWithScale(selection, bank, id, scaleIndex);
}

// Voice button tap: recall hot favourite `slot` (0..3).
inline Result hotRecall(uint8_t slot, tuning::Selection &selection, tuning::Bank &bank,
                        uint8_t &scaleIndex) noexcept {
    const uint8_t id = tuning::favoriteAt(bank, slot);
    if (id == tuning::kEmptySlot) {
        Result r;
        r.change = Change::Empty;
        r.slot = slot;
        r.scaleIndex = scaleIndex;
        return r;
    }
    Result r = appliedWithScale(selection, bank, id, scaleIndex);
    r.slot = slot;
    return r;
}

// Voice button hold: store the playing tuning in hot favourite `slot`, or clear the slot
// when it already holds it (hold again to undo).
inline Result hotStore(uint8_t slot, const tuning::Selection &selection,
                       tuning::Bank &bank) noexcept {
    Result r;
    r.slot = slot;
    r.tuningId = selection.tuningId;
    if (slot >= tuning::kFavoriteSlots) return r;
    if (tuning::favoriteAt(bank, slot) == selection.tuningId) {
        tuning::clearFavorite(bank, slot);
        r.change = Change::Unstarred;
    } else if (tuning::storeFavorite(bank, slot, selection.tuningId)) {
        r.change = Change::Starred;
    }
    return r;
}

// Button 7: A/B between the playing tuning and the one before it, each with the scale it had.
inline Result swapAB(tuning::Selection &selection, tuning::Bank &bank,
                     uint8_t &scaleIndex) noexcept {
    Result r;
    const uint8_t before = scaleIndex;
    if (tuning::swapWithPreviousWithScale(selection, bank, scaleIndex)) {
        r.change = Change::Applied;
        r.tuningId = selection.tuningId;
    }
    r.scaleChanged = scaleIndex != before;
    r.scaleIndex = scaleIndex;
    return r;
}

// Encoder detents: step through the whole library in pad order (family by family), wrapping.
inline Result encoderStep(int steps, tuning::Selection &selection, tuning::Bank &bank,
                          uint8_t &scaleIndex) noexcept {
    if (steps == 0) {
        Result r;
        r.scaleIndex = scaleIndex;
        return r;
    }
    return appliedWithScale(selection, bank, tuning::stepInLibrary(selection.tuningId, steps),
                            scaleIndex);
}

// Buttons 1-6: choose scale `slot` (0..5) of the playing tuning. A slot past the end of the
// tuning's scales is reported and changes nothing.
inline Result scaleButton(uint8_t slot, const tuning::Selection &selection,
                          uint8_t &scaleIndex) noexcept {
    Result r;
    r.tuningId = selection.tuningId;
    r.slot = slot;
    if (slot >= tuning::scaleSet(selection.tuningId).count) {
        r.change = Change::NoScale;
        r.scaleIndex = scaleIndex;
        return r;
    }
    const uint8_t chosen = tuning::scaleAtSlot(selection.tuningId, slot);
    r.change = Change::ScaleChosen;
    r.scaleChanged = chosen != scaleIndex;
    scaleIndex = chosen;
    r.scaleIndex = chosen;
    return r;
}

// --- Faders ------------------------------------------------------------------------------

// Fader 1 = tonic (Sa), 2 = A4 reference, 3 = scale, 4 unassigned (kept free).
enum class Fader : uint8_t { Tonic = 0, Reference, Scale, Unassigned };
constexpr uint8_t kFaderChannels = 4;

inline Fader faderForChannel(uint8_t channel) noexcept {
    return channel < 3 ? static_cast<Fader>(channel) : Fader::Unassigned;
}

// The firmware builds with -ffast-math, where std::isfinite may be folded away: inspect
// the bit pattern instead (exponent all ones = NaN or infinity).
inline bool finiteFloat(float value) noexcept {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

inline float clamp01(float x) noexcept { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

constexpr float kA4StepHz = 0.5f;
constexpr float kA4DetentHz[] = {415.0f, 432.0f, 440.0f, 442.0f};
constexpr float kA4DetentRadius = 0.75f;

inline uint8_t tonicForFader(float x) noexcept {
    const int tonic = static_cast<int>(clamp01(x) * 12.0f);
    return static_cast<uint8_t>(tonic > 11 ? 11 : tonic);
}
inline float tonicFaderPosition(uint8_t tonic) noexcept {
    return (static_cast<float>(tonic > 11 ? 11 : tonic) + 0.5f) / 12.0f;
}

// 415..466 Hz in half-hertz steps, with a magnetic detent at the pitches people actually
// use, so 440.0 (which keeps the legacy exact-MIDI path) is easy to land on.
inline float a4ForFader(float x) noexcept {
    const float span = tuning::kMaxA4Hz - tuning::kMinA4Hz;
    const int steps = static_cast<int>(clamp01(x) * span / kA4StepHz + 0.5f);
    float hz = tuning::kMinA4Hz + static_cast<float>(steps) * kA4StepHz;
    for (const float detent : kA4DetentHz) {
        const float d = hz > detent ? hz - detent : detent - hz;
        if (d <= kA4DetentRadius) {
            hz = detent;
            break;
        }
    }
    if (hz < tuning::kMinA4Hz) hz = tuning::kMinA4Hz;
    if (hz > tuning::kMaxA4Hz) hz = tuning::kMaxA4Hz;
    return hz;
}
inline float a4FaderPosition(float a4Hz) noexcept {
    return clamp01((a4Hz - tuning::kMinA4Hz) / (tuning::kMaxA4Hz - tuning::kMinA4Hz));
}

// The scale fader spreads over the playing tuning's own scales, not over all of them: slot
// count of the set, first at the bottom.
inline uint8_t scaleSlotForFader(float x, uint8_t tuningId) noexcept {
    const int count = tuning::scaleSet(tuningId).count;
    const int slot = static_cast<int>(clamp01(x) * static_cast<float>(count));
    return static_cast<uint8_t>(slot >= count ? count - 1 : slot);
}
inline float scaleFaderPosition(uint8_t tuningId, uint8_t scaleIndex) noexcept {
    const int count = tuning::scaleSet(tuningId).count;
    const int slot = tuning::scaleSlot(tuningId, scaleIndex);
    return ((slot < 0 ? 0 : static_cast<float>(slot)) + 0.5f) / static_cast<float>(count);
}

// Fader position (0..1) -> the setting it owns. True only when the setting changed, so an
// idle fader never queues a voice update. A non-finite position is ignored.
inline bool applyFader(Fader fader, float normalized, tuning::Selection &selection,
                       uint8_t &scaleIndex) noexcept {
    if (!finiteFloat(normalized)) return false;
    switch (fader) {
    case Fader::Tonic: {
        const uint8_t tonic = tonicForFader(normalized);
        if (tonic == selection.tonic) return false;
        selection.tonic = tonic;
        return true;
    }
    case Fader::Reference: {
        const float hz = a4ForFader(normalized);
        if (hz == selection.a4Hz) return false;
        selection.a4Hz = hz;
        return true;
    }
    case Fader::Scale: {
        const uint8_t scale =
            tuning::scaleAtSlot(selection.tuningId, scaleSlotForFader(normalized, selection.tuningId));
        if (scale == scaleIndex) return false;
        scaleIndex = scale;
        return true;
    }
    case Fader::Unassigned:
        break;
    }
    return false;
}

// Where each fader sits for the current settings (0..1), for re-arming its deadband.
inline float faderPositionFor(Fader fader, const tuning::Selection &selection,
                              uint8_t scaleIndex) noexcept {
    switch (fader) {
    case Fader::Tonic: return tonicFaderPosition(selection.tonic);
    case Fader::Reference: return a4FaderPosition(selection.a4Hz);
    case Fader::Scale: return scaleFaderPosition(selection.tuningId, scaleIndex);
    case Fader::Unassigned: break;
    }
    return 0.0f;
}

// --- OLED text (21 characters per line at text size 1) ------------------------------------

constexpr size_t kLine = tuning::kMaxNameLength + 1; // 21 characters and the terminator

// A scale's name: the full name when it fits `width` characters, else the short one.
inline const char *scaleLabel(uint8_t scaleIndex, size_t width) noexcept {
    const uint8_t index = scaleIndex < SCALES_COUNT ? scaleIndex : 0;
    const char *full = scaleNames[index];
    return std::strlen(full) <= width ? full : scaleShortNames[index];
}

// "TUNING 13/29 JUST": where the playing tuning sits in the library, and its family.
inline void formatHeader(const tuning::Selection &selection, char *out, size_t size) noexcept {
    if (!out || size == 0) return;
    const tuning::Tuning &t = tuning::resolve(selection.tuningId);
    const int position = tuning::libraryIndexOf(t.id);
    std::snprintf(out, size, "TUNING %d/%d %s", position + 1, static_cast<int>(tuning::libraryCount()),
                  tuning::familyShortName(t.family));
}

// The line naming the tonic and reference: "Sa=D  A4=440.0Hz" for Indian tunings (Sa moves
// with the tonic), "Root=D  A4=440.0Hz" for everything else.
inline void formatPitchLine(const tuning::Selection &selection, char *out, size_t size) noexcept {
    if (!out || size == 0) return;
    const tuning::Tuning &t = tuning::resolve(selection.tuningId);
    char a4[16];
    tuning::formatA4(selection.a4Hz, a4, sizeof(a4));
    std::snprintf(out, size, "%s=%s  A4=%s", t.naming == tuning::Naming::Sargam ? "Sa" : "Root",
                  tuning::tonicName(selection.tonic), a4);
}

// "3/5 Maqam Hijaz 24": the playing scale, where it sits among this tuning's scales.
inline void formatScaleLine(const tuning::Selection &selection, uint8_t scaleIndex, char *out,
                            size_t size) noexcept {
    if (!out || size == 0) return;
    const int count = tuning::scaleSet(selection.tuningId).count;
    const int slot = tuning::scaleSlot(selection.tuningId, scaleIndex);
    char prefix[12];
    const int used = slot >= 0 ? std::snprintf(prefix, sizeof(prefix), "%d/%d ", slot + 1, count)
                               : std::snprintf(prefix, sizeof(prefix), "-/%d ", count);
    const size_t width = (size > 0 ? size - 1 : 0) > static_cast<size_t>(used)
                             ? (size - 1) - static_cast<size_t>(used)
                             : 0;
    std::snprintf(out, size, "%s%s", prefix, scaleLabel(scaleIndex, width));
}

// Compact status for the normal screens: "5-LIMIT C" or "22-SHRUTI D A432.0". Standard
// pitch shows only the tuning, and 12-EDO on C at 440 shows nothing at all.
inline void formatStatus(const tuning::Selection &selection, char *out, size_t size) noexcept {
    if (!out || size == 0) return;
    out[0] = '\0';
    if (tuning::isStandard(selection)) return;
    const tuning::Tuning &t = tuning::resolve(selection.tuningId);
    int n = std::snprintf(out, size, "%s %s", t.shortName, tuning::tonicName(selection.tonic));
    if (n > 0 && static_cast<size_t>(n) < size && selection.a4Hz != tuning::kDefaultA4Hz) {
        char a4[16];
        tuning::formatA4(selection.a4Hz, a4, sizeof(a4));
        // "A432.0": drop the unit to keep the line short.
        char *hz = std::strstr(a4, "Hz");
        if (hz) *hz = '\0';
        std::snprintf(out + n, size - static_cast<size_t>(n), " A%s", a4);
    }
}

// The line the OLED shows after a gesture. A tuning that brought a new scale along says so:
// "Maqam>Rast 24" style, so the change of scale is never a surprise.
inline void formatNotice(const Result &result, char *out, size_t size) noexcept {
    if (!out || size == 0) return;
    out[0] = '\0';
    const tuning::Tuning *t = tuning::find(result.tuningId);
    switch (result.change) {
    case Change::Applied:
        if (!t) break;
        if (result.scaleChanged) {
            // "Tuning > scale" in as much of the scale's name as the row has room for.
            const size_t head = std::strlen(t->shortName);
            const size_t room = tuning::kMaxNameLength > head + 3 ? tuning::kMaxNameLength - head - 3 : 0;
            const char *scale = scaleLabel(result.scaleIndex, room);
            const bool spaced = head + 3 + std::strlen(scale) <= tuning::kMaxNameLength;
            std::snprintf(out, size, spaced ? "%s > %s" : "%s>%s", t->shortName, scale);
        } else {
            std::snprintf(out, size, "%s", t->name);
        }
        break;
    case Change::ScaleChosen:
        std::snprintf(out, size, "%s", scaleLabel(result.scaleIndex, tuning::kMaxNameLength));
        break;
    case Change::Starred:
        std::snprintf(out, size, "Saved to Hot %d", result.slot + 1);
        break;
    case Change::Unstarred:
        std::snprintf(out, size, "Cleared Hot %d", result.slot + 1);
        break;
    case Change::Empty:
        std::snprintf(out, size, "Hot %d is empty", result.slot + 1);
        break;
    case Change::NoScale:
        std::snprintf(out, size, "Only %d scales here",
                      static_cast<int>(tuning::scaleSet(result.tuningId).count));
        break;
    case Change::None:
        break;
    }
}

// The value line for the fader that was just moved: "Tonic D", "A4 432.0Hz", "Scale 2/5".
inline void formatFaderValue(Fader fader, const tuning::Selection &selection, uint8_t scaleIndex,
                             char *out, size_t size) noexcept {
    if (!out || size == 0) return;
    out[0] = '\0';
    switch (fader) {
    case Fader::Tonic:
        std::snprintf(out, size, "Tonic %s", tuning::tonicName(selection.tonic));
        break;
    case Fader::Reference: {
        char a4[16];
        tuning::formatA4(selection.a4Hz, a4, sizeof(a4));
        std::snprintf(out, size, "A4 %s", a4);
        break;
    }
    case Fader::Scale: {
        const int slot = tuning::scaleSlot(selection.tuningId, scaleIndex);
        std::snprintf(out, size, "Scale %d/%d", slot + 1,
                      static_cast<int>(tuning::scaleSet(selection.tuningId).count));
        break;
    }
    case Fader::Unassigned:
        break;
    }
}

} // namespace TuningPage
