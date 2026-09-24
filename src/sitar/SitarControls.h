#pragma once

#include "SitarParameters.h"
#include "SitarRagas.h"

#include <cstdint>

// SitarControls.h — the Sitar Explorer performance state and gesture policy
// (portable, Core 0; the instance lives in UIState, never a loose global).
// Musical role: the panel becomes a sitar. Two touch rows are the fingerboard
// (16 raga frets, Sa to Sa''), the row above it is the right hand (chikari and
// kharaj strokes, jhala, tanpura, shimmers) and the row above that is the
// exploration row — the five groups of sitar.h lanes plus randomize, defaults
// and the console dump. The knob walks all thirteen lanes and edits whichever
// one is focused; the faders stay on the four sitar macros.
// Technical role: pure decisions. Pad/button/encoder edges in, voice commands
// and state out — no hardware calls, no rpdsp, so the whole mapping is
// host-testable (tests/unit/test_sitar.cpp).
namespace Sitar
{

// --- The four string courses the mode plays ---------------------------------
// Sitar Explorer keeps a separate ring for each job a real sitar's strings do:
// the melody string (bent by every meend), the two chikari drone strings, and
// the kharaj bass string. One course cannot do two jobs — plucking a shared
// course would retune the note that is still ringing.
enum class Course : uint8_t
{
    Main = 0,
    ChikariLow,
    ChikariHigh,
    Kharaj,
    Count
};

constexpr uint8_t kCourseCount = static_cast<uint8_t>(Course::Count);

// --- Pad zones --------------------------------------------------------------
// Pads are linear 0..31, row by row, exactly as the LED matrix mirrors them.
constexpr uint8_t kStrokeFirstPad = 0;   // row 1: right hand
constexpr uint8_t kExploreFirstPad = 8;  // row 2: sitar.h exploration
constexpr uint8_t kFretFirstPad = 16;    // rows 3-4: the fingerboard

enum class PadRole : uint8_t { Stroke = 0, Explore, Fret };

constexpr PadRole roleForPad(uint8_t pad) noexcept
{
    if (pad >= kFretFirstPad)
        return PadRole::Fret;
    return pad >= kExploreFirstPad ? PadRole::Explore : PadRole::Stroke;
}

// Fret a fingerboard pad plays, 0..15 (two octaves of the raga).
constexpr uint8_t fretForPad(uint8_t pad) noexcept
{
    const uint8_t fret = static_cast<uint8_t>(pad - kFretFirstPad);
    return fret < kFretCount ? fret : static_cast<uint8_t>(kFretCount - 1);
}

// --- Right-hand strokes (row 1, pads 0..7) ----------------------------------
enum class Stroke : uint8_t
{
    ChikariPa = 0, // Strike the Pa chikari string
    ChikariSa,     // Strike the Sa' chikari string
    Jod,           // Both chikari strings together
    Kharaj,        // The bass string, two octaves down
    JhalaCycle,    // Cycle the clocked stroke pattern
    Tanpura,       // Toggle the clocked drone pulse
    Shimmer,       // A whisper that wakes the taraf bank only
    Damp,          // Palm every string
    Count
};

constexpr uint8_t kStrokeCount = static_cast<uint8_t>(Stroke::Count);

// Frequency a stroke sounds, in Hz, at the mode's Sa.
float strokeFrequency(Stroke stroke) noexcept;
const char *strokeName(Stroke stroke) noexcept;

// Course a stroke strikes; Damp and the two mode pads touch no string
// (Course::Count means "no course").
Course strokeCourse(Stroke stroke) noexcept;

// --- Exploration row (row 2, pads 8..15) ------------------------------------
enum class ExploreAction : uint8_t
{
    GroupString = 0,
    GroupJawari,
    GroupTaraf,
    GroupBody,
    GroupMeend,
    Randomize,
    Defaults,
    Dump,
    Count
};

constexpr uint8_t kExploreCount = static_cast<uint8_t>(ExploreAction::Count);

const char *exploreName(ExploreAction action) noexcept;

// Group a group-jump pad focuses; Count for the pads that are not jumps.
constexpr ParamGroup exploreGroup(ExploreAction action) noexcept
{
    switch (action)
    {
    case ExploreAction::GroupString: return ParamGroup::String;
    case ExploreAction::GroupJawari: return ParamGroup::Jawari;
    case ExploreAction::GroupTaraf: return ParamGroup::Taraf;
    case ExploreAction::GroupBody: return ParamGroup::Body;
    case ExploreAction::GroupMeend: return ParamGroup::Meend;
    default: break;
    }
    return ParamGroup::Count;
}

// --- Clocked strokes --------------------------------------------------------
// Jhala: which of the bar's 16 sixteenth-notes the chikari strings strike.
// This is the sitar's rhythmic engine — the transport's tempo fader becomes the
// speed of the drone, which is how the mode keeps Play/Stop meaningful.
enum class JhalaPattern : uint8_t { Off = 0, Eighths, Sixteenths, Jhala, Count };

const char *jhalaName(JhalaPattern pattern) noexcept;
constexpr uint8_t kJhalaCount = static_cast<uint8_t>(JhalaPattern::Count);

// True when this sixteenth of the bar carries a chikari stroke.
constexpr bool jhalaStrike(JhalaPattern pattern, uint8_t sixteenth) noexcept
{
    const uint8_t slot = static_cast<uint8_t>(sixteenth & 15u);
    switch (pattern)
    {
    case JhalaPattern::Off: return false;
    case JhalaPattern::Eighths: return (slot % 2u) == 0u;
    case JhalaPattern::Sixteenths: return true;
    case JhalaPattern::Jhala: return true;
    case JhalaPattern::Count: break;
    }
    return false;
}

// What one clocked sixteenth asks the audio side to play: the chikari pair plus
// the tanpura's bar pulse, which is three strokes at most.
constexpr uint8_t kClockStrokeParts = 3;

// What one clocked sixteenth asks the audio side to play (at most two strokes:
// a chikari and the tanpura's bar pulse).
struct ClockStrokes
{
    uint8_t count = 0;
    Course course[kClockStrokeParts] = {};
    float frequency[kClockStrokeParts] = {};
    float amplitude[kClockStrokeParts] = {};
};

// --- Pad results ------------------------------------------------------------
// What a finger on the panel asks for. Only the fields named by `kind` carry
// meaning, which keeps the firmware glue a flat switch instead of a guess.
struct PadResult
{
    enum class Kind : uint8_t
    {
        None = 0,
        Pluck,   // Fresh pluck of the melody string at `frequency`
        Slide,   // Meend: bend the ringing melody string to `frequency`
        Chikari, // Strike `course` (a chikari or the kharaj)
        Shimmer, // Whisper the melody string so the taraf bank answers
        Damp,    // Silence every course
        Jhala,   // Cycle the clocked stroke pattern
        Tanpura, // Toggle the clocked drone pulse
        Explore, // Row-2 action: see `explore`
    };

    Kind kind = Kind::None;
    Course course = Course::Main;
    uint8_t fret = 0;
    float frequency = 0.0f;
    float amplitude = 1.0f;
    // A jod stroke catches both chikari strings at once; when the second
    // course is Course::Count there is nothing more to strike.
    Course secondCourse = Course::Count;
    float secondFrequency = 0.0f;
    float secondAmplitude = 0.0f;
    ExploreAction explore = ExploreAction::GroupString;
};

// --- Button results ---------------------------------------------------------
// One tile-button press, decoded. Hold-to-reset is polled separately so a
// randomize tap never becomes a defaults wipe (same split as the randomize
// tile in the sequencer modes).
struct ButtonIntent
{
    enum class Kind : uint8_t
    {
        None = 0,
        StrokeUp,     // Jod up the neck: pluck low, meend to the top fret
        StrokeDown,   // Meend back to the bottom and rest the string
        RagaNext,
        RagaPrevious,
        JhalaCycle,
        TanpuraToggle,
        FocusNext,    // Walk the thirteen sitar.h lanes
        FocusPrevious,
        Randomize,
        Defaults,
        Dump,         // Print the sitar.h legend and every current value
        Exit,
    };

    Kind kind = Kind::None;
};

// --- Stroke request emitted by the high-level button intents ----------------
// A jod stroke is two commands in order: a pluck and a meend. The firmware glue
// sends them in this order, one per queue slot.
struct StrokeRequest
{
    bool pluck = false;
    bool slide = false;
    float frequency = 0.0f;
    float amplitude = 0.85f;
    bool damp = false;
};

// --- The mode's state -------------------------------------------------------
struct Controls
{
    bool active = false;
    // Swallows the entry chord's own release, like every other mode here.
    bool waitRelease = true;

    uint8_t ragaIndex = 0;
    Param focus = Param::Brightness;
    // Normalized travel per lane; the instrument's DSP targets are always this
    // value mapped through engineeringValue().
    float value[kParamCount] = {};

    JhalaPattern jhala = JhalaPattern::Eighths;
    bool tanpura = false;

    // Fingerboard: the finger's current fret while it is down, and when each
    // fret and stroke last sounded. The LED renderer fades from these stamps
    // (wall-clock, never frame-counted).
    bool fingerDown = false;
    uint8_t fingerFret = 0;
    uint32_t fretStampMs[kFretCount] = {};
    uint32_t strokeStampMs[kStrokeCount] = {};
    uint32_t exploreStampMs[kExploreCount] = {};
    uint32_t lastFretChangeMs = 0;

    // Clocked stroke bookkeeping: the last bar slot the transport reported, so
    // a jhala/tanpura stroke fires once per sixteenth even if the loop drains
    // a burst of steps after a stall.
    uint8_t lastClockSlot = 0xFF;

    // RNG for the randomize pad; deterministic so tests can pin the result.
    uint32_t randomState = 0x9E3779B9u;

    // --- Lifecycle ---
    // Enter: the performer's own fingers must leave the panel first, and the
    // lanes start at sitar.h's defaults so the mode opens on a playable sitar.
    void enter() noexcept;
    void exit() noexcept;
    // True while the mode owns the panel; the glue checks this everywhere the
    // sequencer modes would otherwise claim a pad, a button or a fader.
    bool ownsPanel() const noexcept { return active; }

    // Timestamps the panel rows so the LED renderer can flash a pad without
    // owning any state of its own: presses stamp themselves here, the clock
    // stamps the drone row, and the mode's own actions (randomize, defaults,
    // palm) stamp the pads that have no finger on them.
    void stampStroke(Stroke stroke, uint32_t now) noexcept;
    void stampExplore(ExploreAction action, uint32_t now) noexcept;

    // --- Pads ---
    PadResult padPressed(uint8_t pad, uint32_t now) noexcept;
    PadResult padReleased(uint8_t pad, uint32_t now) noexcept;

    // --- Tile buttons (bits 0..6; Shift is a level) ---
    ButtonIntent buttonPressed(uint8_t bit, bool shift) noexcept;
    // Hold poll: a long press of the randomize bit restores sitar.h defaults.
    ButtonIntent pollHeld(uint8_t bits, uint32_t now) noexcept;

    // --- Knob ---
    // Adds one poll's worth of encoder travel (the driver's velocity-scaled
    // increment, exactly as the sequencer modes apply it) to the focused lane.
    bool nudgeFocused(float travel) noexcept;

    // --- Lanes ---
    void setValue(Param id, float normalized) noexcept;
    void setGroup(ParamGroup group) noexcept;
    void randomize() noexcept;
    void restoreDefaults() noexcept;
    // Hand height as a pluck force: a hovering hand plays softer, and no hand
    // at all falls back to a firm stroke so a bare panel still speaks. The glue
    // refreshes this every control pass from the distance sensor.
    void observeHand(bool handPresent, float hand01) noexcept;
    float pluckForce() const noexcept { return pluckForce_; }

    // --- Clock ---
    ClockStrokes clockStep(uint8_t sixteenthInBar, uint32_t nowMs) noexcept;
    // The two jod gestures the buttons ask for, as ordered commands.
    StrokeRequest strokeUp() const noexcept;
    StrokeRequest strokeDown() const noexcept;

    // Frequency of a fingerboard fret in the current raga.
    float fretFrequency(uint8_t fret) const noexcept;
    const Raga &currentRaga() const noexcept
    {
        return raga(ragaIndex < kRagaCount ? ragaIndex : 0);
    }

    // --- Tile tiles: edges and holds in one call ---
    // Decoded from raw tile levels, the same shape as VoiceEdit::Controls::poll:
    // edge-triggered, and waitRelease swallows the entry chord. At most one
    // button action and one raga pick can happen per pass — the tile bus only
    // carries one transaction per control slice anyway.
    struct PollResult
    {
        ButtonIntent button{};
        int8_t raga = -1; // 0..3 when a voice button picked a raga
    };
    PollResult poll(uint8_t buttons, uint8_t voices, bool shift, uint32_t now) noexcept;

private:
    float nextRandom01() noexcept;
    float pluckForce_ = 0.85f;
    bool holdArmed = false;
    uint32_t holdStartedMs = 0;
    // Raw tile levels from the previous pass, for edge detection.
    uint8_t previousButtons_ = 0;
    uint8_t previousVoices_ = 0;
};

} // namespace Sitar
