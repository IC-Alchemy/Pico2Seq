// AlchemyProto.h — the Alchemy Modular UI register protocol, v2, hub side
// ---------------------------------------------------------------------------
// One header holds the whole wire format so the hub client (AlchemyTiles) and
// the host test suite share a single definition of it. The bytes are specified
// in Docs/AlchemyModularUI_Spec0.5.md §5 and implemented by the PY32F030 tile
// firmware (AlchemyModularInterface/SliderModule); where the spec and the
// firmware disagree the firmware wins, because it is the thing that answers.
//
// Notable firmware-confirmed details baked in here:
//   - SEQ (STATUS bits 4-7) is a 4-bit counter that advances only when the
//     DATA block changed. HEARTBEAT (bit 0) toggles on every sample sweep, so
//     an idle-but-alive tile shows a changing STATUS byte with a frozen SEQ.
//     Compare SEQ for equality, never for magnitude: it wraps at 15.
//   - SUM is the truncating uint8 sum of STATUS plus every DATA byte.
//   - The button bytes inside DATA are identical for both tile types:
//     current level bitmap, sticky pressed-since-last-read, sticky
//     released-since-last-read. The sticky bytes clear only after a master's
//     read cursor has passed them, so a full-frame read never loses an edge
//     and a STATUS-only read never consumes one.
//   - Reads past the defined block return 0x00 and never NACK mid-transaction,
//     so an over-read is safe and distinguishable from an empty bus.
//
// Everything in this header is pure C++: no Arduino, no Wire, no globals. The
// host suite exercises it directly (tests/test_alchemy_proto.cpp).

#ifndef ALCHEMY_UI_PROTO_H
#define ALCHEMY_UI_PROTO_H

#include <cstdint>

namespace alchemy {

// --- Magic, versions, module types -------------------------------------------

inline constexpr std::uint8_t kWhoAmIMagic = 0x5A;  // §5.1: not a module if absent
inline constexpr std::uint8_t kProtoVerV2 = 0x02;

inline constexpr std::uint8_t kTypeSliderButton = 0x01;  // 4 faders + 4 buttons, built
inline constexpr std::uint8_t kTypeButton4 = 0x02;       // 4 buttons, defined

// Platform address registry, §4.3: three strap offsets per type.
inline constexpr std::uint8_t kAddrSliderBase = 0x08;    // 0x08..0x0A
inline constexpr std::uint8_t kAddrButtonBase = 0x0B;    // 0x0B..0x0D
inline constexpr std::uint8_t kAddrOffsetsPerType = 3;

// --- Register map -------------------------------------------------------------

enum Reg : std::uint8_t {
  kRegWhoAmI = 0x00,   // 0x5A
  kRegTypeId = 0x01,
  kRegProtoVer = 0x02,
  kRegFwVer = 0x03,    // 2 bytes: major, minor
  kRegHwRev = 0x05,
  kRegAddrOffset = 0x06,  // 0..2 as read from the strap
  kRegCaps = 0x07,     // 2 bytes LE, §5.3
  kRegUid = 0x09,      // 12 bytes
  kRegDeclaredMa = 0x15,  // 2 bytes LE
  kRegLedCount = 0x17,
  kRegLedTier = 0x18,
  kRegDataLen = 0x19,  // length of the DATA block at 0x21

  kRegStatus = 0x20,   // frame begins here
  kRegData = 0x21,     // DATA_LEN bytes; SUM follows at kRegData + DATA_LEN

  kRegCfgRate = 0x40,
  kRegCfgFilter = 0x41,
  kRegCfgDebounce = 0x42,

  kRegLedBrightness = 0x50,
  kRegSoftCmd = 0x7F,
};

/** SUM register address for a tile with the given DATA_LEN. */
inline constexpr std::uint8_t regSum(std::uint8_t dataLen) {
  return static_cast<std::uint8_t>(kRegData + dataLen);
}

// Capability bits, §5.3. Only the input bits exist on v1 hardware.
enum Caps : std::uint16_t {
  kCapAnalogIn = 1u << 0,
  kCapDigitalIn = 1u << 1,
};

// --- STATUS -------------------------------------------------------------------

inline constexpr std::uint8_t kStatusHeartbeat = 0x01;  // toggles every sweep
inline constexpr std::uint8_t kStatusLocalFault = 0x02;
inline constexpr std::uint8_t kStatusNotReady = 0x04;   // until first sweep
inline constexpr std::uint8_t kStatusSeqShift = 4;
inline constexpr std::uint8_t kStatusSeqMask = 0x0F;

struct FrameStatus {
  bool heartbeat = false;
  bool localFault = false;
  bool notReady = false;
  std::uint8_t seq = 0;  // 4-bit rolling counter, wraps
};

inline FrameStatus decodeStatus(std::uint8_t status) {
  FrameStatus out;
  out.heartbeat = (status & kStatusHeartbeat) != 0;
  out.localFault = (status & kStatusLocalFault) != 0;
  out.notReady = (status & kStatusNotReady) != 0;
  out.seq = static_cast<std::uint8_t>((status >> kStatusSeqShift) & kStatusSeqMask);
  return out;
}

/**
 * True when the two SEQ samples differ. Equality, not ordering: the counter is
 * 4 bits and wraps, so "newer" is undefined across the wrap.
 */
inline bool seqChanged(std::uint8_t a, std::uint8_t b) { return a != b; }

// --- DATA blocks --------------------------------------------------------------

// Slider 4x + Button 4x tile (TYPE 0x01): DATA_LEN 11.
inline constexpr std::uint8_t kSliderDataLen = 11;
inline constexpr std::uint8_t kDataFaders = 0;     // 4 x uint16 LE
inline constexpr std::uint8_t kDataBtnLevel = 8;   // bit n = 1 while pressed
inline constexpr std::uint8_t kDataBtnPressed = 9;   // sticky, clear on read
inline constexpr std::uint8_t kDataBtnReleased = 10;  // sticky, clear on read
inline constexpr std::uint8_t kFadersPerTile = 4;

// Button 4x tile (TYPE 0x02): DATA_LEN 3, same button block, same offsets.
inline constexpr std::uint8_t kButtonDataLen = 3;
inline constexpr std::uint8_t kButtonsPerTile = 4;

/** The button sub-block both tile types share. */
struct ButtonBlock {
  std::uint8_t level = 0;   // bit n set = button n currently pressed
  std::uint8_t pressed = 0;   // pressed at least once since the last full read
  std::uint8_t released = 0;  // released at least once since the last full read
};

inline ButtonBlock decodeButtonBlock(std::uint8_t level, std::uint8_t pressed,
                                      std::uint8_t released) {
  return ButtonBlock{level, pressed, released};
}

/** Raw 12-bit fader reading for channel 0..3 from a slider tile DATA block. */
inline std::uint16_t decodeFader(const std::uint8_t* data, std::uint8_t channel) {
  const std::uint8_t i = static_cast<std::uint8_t>(kDataFaders + channel * 2);
  return static_cast<std::uint16_t>(data[i] | (data[i + 1] << 8));
}

// --- Frame checksum -----------------------------------------------------------

/** Truncating uint8 sum of STATUS plus every DATA byte — the SUM register. */
inline std::uint8_t frameSum(std::uint8_t status, const std::uint8_t* data,
                             std::uint8_t len) {
  std::uint16_t sum = status;
  for (std::uint8_t i = 0; i < len; ++i) {
    sum = static_cast<std::uint16_t>(sum + data[i]);
  }
  return static_cast<std::uint8_t>(sum);
}

/**
 * Verify a STATUS+DATA+SUM frame read in one transaction. `frame` is the
 * (1 + dataLen + 1) bytes read starting at kRegStatus.
 */
inline bool frameChecksumOk(const std::uint8_t* frame, std::uint8_t dataLen) {
  const std::uint8_t sum = frame[1 + dataLen];
  return frameSum(frame[0], frame + 1, dataLen) == sum;
}

// --- Identity -----------------------------------------------------------------

// Bytes to read from kRegWhoAmI to cover the whole identity block (0x00..0x19).
inline constexpr std::uint8_t kIdentityReadLength = 0x1A;

struct Identity {
  bool valid = false;         // WHO_AM_I and PROTO_VER matched
  std::uint8_t typeId = 0;
  std::uint8_t protoVer = 0;
  std::uint8_t fwMajor = 0;
  std::uint8_t fwMinor = 0;
  std::uint8_t hwRev = 0;
  std::uint8_t addrOffset = 0;
  std::uint16_t caps = 0;
  std::uint8_t dataLen = 0;
};

/**
 * Decode an identity block read in one transaction from kRegWhoAmI. A tile
 * whose magic or protocol version does not match is not an Alchemy module (or
 * not one this hub can talk to) and decodes to valid == false.
 */
inline Identity decodeIdentity(const std::uint8_t* raw, std::uint8_t length) {
  Identity out;
  if (length < kIdentityReadLength) return out;
  if (raw[kRegWhoAmI] != kWhoAmIMagic) return out;
  if (raw[kRegProtoVer] != kProtoVerV2) return out;
  out.valid = true;
  out.typeId = raw[kRegTypeId];
  out.protoVer = raw[kRegProtoVer];
  out.fwMajor = raw[kRegFwVer];
  out.fwMinor = raw[kRegFwVer + 1];
  out.hwRev = raw[kRegHwRev];
  out.addrOffset = raw[kRegAddrOffset];
  out.caps = static_cast<std::uint16_t>(raw[kRegCaps] | (raw[kRegCaps + 1] << 8));
  out.dataLen = raw[kRegDataLen];
  return out;
}

}  // namespace alchemy

#endif  // ALCHEMY_UI_PROTO_H
