// display_link.h — the round-display wire protocol, v1, hub side
// ---------------------------------------------------------------------------
// The Pico2Seq hub sends fixed-size PAGE_FRAME state packets to a PY32F030
// that drives a GC9A01 240x240 round panel over the main Wire bus (I2C,
// address 0x3E). The Pico decides WHAT is on screen — one page plus the
// fields that page shows; the PY32 decides HOW it is drawn — fonts, round
// layout, theme application, animation. The Pico never sends pixels:
// 240x240x16 bpp cannot fit in the PY32's 4 KB of SRAM, so only these state
// packets cross the wire.
//
// Conventions are borrowed from src/AlchemyUI/src/AlchemyProto.h, the tile
// link that shares the hub:
//   - SUM is a truncating uint8 sum of every preceding frame byte (no CRC
//     exists anywhere in this ecosystem).
//   - SEQ is a 4-bit counter that advances only when the page content
//     changed; compare it for equality, never magnitude, because it wraps
//     at 15. An identical-SEQ re-send (the 2 Hz heartbeat) is idempotent
//     on the panel side.
//   - The identity block reads like a tile's: WHOAMI 0x5A at register 0x00.
//
// This header is the single definition of the format. It is manually
// mirrored, byte-identical, into the PY32 repo — the two repos cannot share
// a file, which is exactly how AlchemyProto works today. Keep it
// self-contained (only <stdint.h> and <cstddef>) and byte-stable: any change
// here must land in both repos at once, guarded by the host suite
// (tests/unit/test_display_link.cpp).
//
// Everything in this header is pure C++: no Arduino, no Wire, no globals.

#pragma once

#include <stdint.h>
#include <cstddef>

namespace rdisplay {

// --- Identity and transport ---------------------------------------------------

inline constexpr uint8_t kDisplayAddress = 0x3E;  // main Wire bus, verified free
inline constexpr uint8_t kWhoAmIMagic = 0x5A;     // §5.1 convention: not a display if absent
inline constexpr uint8_t kTypeDisplay = 0x03;     // next free after slider 0x01 / button 0x02
inline constexpr uint8_t kProtoVerV1 = 0x01;

// --- Register map ---------------------------------------------------------------

enum Reg : uint8_t {
  kRegWhoAmI = 0x00,          // 0x5A
  kRegTypeId = 0x01,          // kTypeDisplay
  kRegProtoVer = 0x02,
  kRegFwVer = 0x03,           // 2 bytes: major, minor

  kRegStatus = 0x20,          // heartbeat / faults / last-applied SEQ
  kRegFrame = 0x21,           // write pointer for a PAGE_FRAME burst

  kRegStatsFramesOk = 0x24,   // frames accepted (SUM ok + SEQ new)
  kRegStatsSumRejects = 0x25, // frames dropped on a bad SUM
  kRegStatsFramesRendered = 0x26,
  kRegStatsLastSeq = 0x27,    // SEQ of the frame now on the panel
};

// --- STATUS ---------------------------------------------------------------------

inline constexpr uint8_t kStatusHeartbeat = 0x01;  // toggles on every sweep
inline constexpr uint8_t kStatusLocalFault = 0x02;
inline constexpr uint8_t kStatusNotReady = 0x04;   // until the first applied frame
inline constexpr uint8_t kStatusSeqShift = 4;
inline constexpr uint8_t kStatusSeqMask = 0x0F;

/**
 * SEQ is 4-bit, wraps at 15, and is compared for equality only — never
 * magnitude (AlchemyProto convention). "Newer" is undefined across the wrap.
 */
inline bool seqChanged(uint8_t a, uint8_t b) { return a != b; }

// --- Pages ------------------------------------------------------------------------

// 0 is reserved ("no page"); the values are wire-stable, do not renumber.
enum class PageId : uint8_t {
  VoiceEditor = 1,
  ModeBanner,
  Notice,
  HeldParam,
  SettingsToggles,
  SettingsPresets,
  GateLength,
  StepEnv,
  ParamEdit,
  Status,
};

// --- Theme ------------------------------------------------------------------------

/** RGB888 -> RGB565, the format the panel's 16-bpp framebuffer wants. */
inline uint16_t toRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/**
 * Seven RGB565 words, 14 bytes on the wire, little-endian: the four voice
 * hues, then playhead accent, backdrop base and text accent, in this order.
 */
struct ThemeBlock {
  uint16_t voiceHue[4];
  uint16_t playheadAccent;
  uint16_t backgroundBase;
  uint16_t textAccent;
};

static_assert(sizeof(ThemeBlock) == 14, "theme block is exactly seven RGB565 words");

/** The i-th theme word in wire order (0..6). Keeps the access defined where a
 * pointer walk past voiceHue's array bounds would not be. */
inline uint16_t themeWord(const ThemeBlock& theme, size_t index) {
  switch (index) {
    case 0:
    case 1:
    case 2:
    case 3: return theme.voiceHue[index];
    case 4: return theme.playheadAccent;
    case 5: return theme.backgroundBase;
    default: return theme.textAccent;
  }
}

inline void setThemeWord(ThemeBlock& theme, size_t index, uint16_t value) {
  switch (index) {
    case 0:
    case 1:
    case 2:
    case 3: theme.voiceHue[index] = value; break;
    case 4: theme.playheadAccent = value; break;
    case 5: theme.backgroundBase = value; break;
    default: theme.textAccent = value; break;
  }
}

// --- Page bodies -------------------------------------------------------------------
// One fixed-size body per PageId, packed, little-endian (both ends are LE).
// Strings are NUL-terminated where they fit; a host-side formatter owns the
// bytes, the panel only lays them out.

#pragma pack(push, 1)

/** PageId::VoiceEditor — the focused voice's parameter row. */
struct VoiceEditorBody {
  uint8_t voice;
  uint8_t changed;        // voiceEditor.changed[voice], 0 or 1
  char paramName[12];     // VoiceEdit::name
  char paramValue[16];    // VoiceEdit::format
};
static_assert(sizeof(VoiceEditorBody) == 30, "VoiceEditorBody wire size");

/** PageId::ModeBanner — the transient alchemy mode banner. */
struct ModeBannerBody {
  uint8_t kind;           // 0 = PARAM banner, 1 = UTIL
};
static_assert(sizeof(ModeBannerBody) == 1, "ModeBannerBody wire size");

/** PageId::Notice — the transient OLED notice, mirrored for the round panel. */
struct NoticeBody {
  uint8_t kind;           // mirrors UIState::OledNoticeKind's numeric value
  uint8_t voice;
  uint16_t value;         // oledNoticeValue (delay notices)
  char word[12];          // "RANDOMIZED", "SAVED", ...
  char sub[12];           // per-voice or delay-time suffix
};
static_assert(sizeof(NoticeBody) == 28, "NoticeBody wire size");

/**
 * PageId::HeldParam — the parameter under the hand, held or selected.
 * PageId::ParamEdit reuses this body (same fields, same gate data).
 */
struct HeldParamBody {
  uint8_t paramId;
  uint8_t voice;
  uint8_t mode;           // 0 = BASE view, 1 = LIVE, 2 = STEP
  int8_t step;            // -1 when no step is selected
  int16_t distanceMm;     // -1 when the distance sensor has no reading
  uint8_t handPresent;    // 0 or 1
  char value[16];         // MusicalValues::format
  char base[16];          // MusicalValues::baseStep, empty outside BASE view
};
static_assert(sizeof(HeldParamBody) == 39, "HeldParamBody wire size");

/** PageId::SettingsToggles — the voice-parameter settings row. */
struct SettingsTogglesBody {
  uint8_t voice;
  char name[12];          // voiceParameterNoticeName
  char value[12];         // voiceParameterNoticeValue
};
static_assert(sizeof(SettingsTogglesBody) == 25, "SettingsTogglesBody wire size");

/** PageId::SettingsPresets — the preset selection row. */
struct SettingsPresetsBody {
  uint8_t selectedVoice;
  uint8_t count;          // VoicePresets::getPresetCount
  uint8_t presetIdx[4];   // voicePresetIndices
  uint8_t changed[4];     // preset changed flags, 0 or 1
  char selName[16];       // VoicePresets::getPresetName(selectedVoice)
};
static_assert(sizeof(SettingsPresetsBody) == 26, "SettingsPresetsBody wire size");

/** PageId::GateLength — the gate-length editing page. */
struct GateLengthBody {
  uint8_t voice;
  uint8_t length;         // 1..64 steps
};
static_assert(sizeof(GateLengthBody) == 2, "GateLengthBody wire size");

/** PageId::StepEnv — the ADSR lanes of one step. */
struct StepEnvBody {
  uint8_t voice;
  uint8_t step;
  uint8_t lastLane;       // uiState.envFaderLane, the fader under the hand
  uint8_t laneValue[4];   // Attack / Decay / Sustain / Release
  uint8_t laneFollows[4]; // 1 when lane i follows the patch (LANE_FOLLOWS_PATCH)
};
static_assert(sizeof(StepEnvBody) == 11, "StepEnvBody wire size");

/**
 * PageId::Status — the default page: the whole instrument at a glance.
 * gateBits bit i = step i's gate is on; only steps below the gate length
 * are defined, the rest read 0.
 */
struct StatusBody {
  uint16_t bpmX10;
  uint8_t playing;
  uint8_t currentStep;
  int8_t scaleIdx;
  uint8_t shuffleIdx;
  uint8_t encTargetId;
  uint8_t pad;
  uint8_t presetIdx[4];
  uint8_t changed[4];
  uint8_t gateBits[8];
  char encValue[16];
};
static_assert(sizeof(StatusBody) == 40, "StatusBody wire size");

#pragma pack(pop)

// --- PAGE_FRAME --------------------------------------------------------------------
// frame = header(4) + theme(14) + body + SUM(1). The largest page body is
// STATUS at 40 bytes, so the largest frame on the wire is 59 bytes; 64 is the
// I2C buffer budget nothing may exceed.

inline constexpr uint8_t kMaxBodyBytes = 40;   // sizeof(StatusBody)
inline constexpr uint8_t kMaxFrameBytes = 64;  // header(4) + theme(14) + body + SUM(1)

struct PageFrame {
  uint8_t protoVer;
  uint8_t seq;            // 4-bit, advances only when the page content changed
  PageId page;
  uint8_t themeIdx;       // uiState.currentThemeIndex
  ThemeBlock theme;
  uint8_t body[kMaxBodyBytes];
  uint8_t bodyLen;        // bytes of body[] on the wire, sizeof(Body) for the page
};

// --- Frame checksum ------------------------------------------------------------------

/** Truncating uint8 sum over every byte — the same rule as AlchemyProto's
 * frameSum. No CRC in this ecosystem. */
inline uint8_t frameSum(const uint8_t* bytes, size_t n) {
  uint8_t sum = 0;
  for (size_t i = 0; i < n; ++i) {
    sum = static_cast<uint8_t>(sum + bytes[i]);
  }
  return sum;
}

/**
 * Serialize one PAGE_FRAME: protoVer, seq, pageId, themeIdx, the theme as
 * seven little-endian words, the body, then a trailing SUM over everything
 * before it. Returns the byte count written, or 0 when `cap` is too small or
 * bodyLen exceeds kMaxBodyBytes. The wire does not carry bodyLen — the
 * receiver derives it from the transaction length.
 */
inline size_t serializeFrame(const PageFrame& frame, uint8_t* out, size_t cap) {
  if (frame.bodyLen > kMaxBodyBytes) return 0;
  const size_t headerBytes = 4;  // protoVer, seq, pageId, themeIdx
  const size_t sumBytes = 1;
  const size_t need = headerBytes + sizeof(ThemeBlock) + sumBytes + frame.bodyLen;
  if (cap < need) return 0;

  out[0] = frame.protoVer;
  out[1] = frame.seq;
  out[2] = static_cast<uint8_t>(frame.page);
  out[3] = frame.themeIdx;
  size_t o = headerBytes;
  const size_t themeWords = sizeof(ThemeBlock) / sizeof(uint16_t);
  for (size_t i = 0; i < themeWords; ++i) {
    const uint16_t word = themeWord(frame.theme, i);
    out[o++] = static_cast<uint8_t>(word & 0xFF);   // little-endian
    out[o++] = static_cast<uint8_t>(word >> 8);
  }
  for (size_t i = 0; i < frame.bodyLen; ++i) {
    out[o++] = frame.body[i];
  }
  out[o] = frameSum(out, o);
  return need;
}

/**
 * Decode and verify a PAGE_FRAME read back from the wire — what the host
 * tests and the PY32 mirror both use. The frame must be at least header +
 * theme + SUM + one body byte long (no page body is empty), exactly header +
 * theme + bodyLen + SUM long, carry kProtoVerV1 and a matching SUM. bodyLen
 * is derived from the length. On success the body tail past bodyLen is
 * zeroed, so callers can compare whole buffers. On failure `out` is untouched.
 */
inline bool decodeFrame(const uint8_t* in, size_t n, PageFrame& out) {
  const size_t headerBytes = 4;
  const size_t sumBytes = 1;
  if (n < headerBytes + sizeof(ThemeBlock) + sumBytes + 1) return false;
  const size_t bodyLen = n - headerBytes - sizeof(ThemeBlock) - sumBytes;
  if (bodyLen > kMaxBodyBytes) return false;
  if (in[0] != kProtoVerV1) return false;
  if (frameSum(in, n - 1) != in[n - 1]) return false;

  out.protoVer = in[0];
  out.seq = in[1];
  out.page = static_cast<PageId>(in[2]);
  out.themeIdx = in[3];
  size_t o = headerBytes;
  const size_t themeWords = sizeof(ThemeBlock) / sizeof(uint16_t);
  for (size_t i = 0; i < themeWords; ++i) {
    setThemeWord(out.theme, i,
                 static_cast<uint16_t>(in[o] | (in[o + 1] << 8)));  // little-endian
    o += 2;
  }
  for (size_t i = 0; i < bodyLen; ++i) {
    out.body[i] = in[o + i];
  }
  for (size_t i = bodyLen; i < kMaxBodyBytes; ++i) {
    out.body[i] = 0;
  }
  out.bodyLen = static_cast<uint8_t>(bodyLen);
  return true;
}

}  // namespace rdisplay
