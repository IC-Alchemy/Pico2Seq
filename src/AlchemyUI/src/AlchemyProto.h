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

// Button tile (TYPE 0x02): DATA_LEN 3. It carries the same three button
// fields as a slider tile, but they begin at its own DATA offset 0 — the
// slider's offsets 8..10 are past the end of a button tile's DATA block.
inline constexpr std::uint8_t kButtonDataLen = 3;
// Both tile types carry a full 8-bit button bitmap in the same 3 DATA bytes:
// ButtonModule8 drives all eight bits; the SliderModule drives only bits 0-3
// and bits 4-7 read 0. Sizing the button arrays by the bitmap width (not by
// the slider's physical count) lets one code path serve both tiles.
inline constexpr std::uint8_t kButtonsPerTile = 8;

/**
 * Offset of the common level/pressed/released button block within DATA. Slider
 * tiles prefix it with four fader words; a dedicated Button tile does not, so
 * its block starts at DATA offset 0.
 */
inline constexpr std::uint8_t buttonBlockOffset(std::uint8_t typeId) {
  return typeId == kTypeSliderButton ? kDataBtnLevel : 0;
}

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

// --- The standardized state packet --------------------------------------------
//
// One compact snapshot of a satellite's whole control state, 11 bytes:
//
//   byte 0     sequence counter
//   byte 1     button bits (level bitmap, bit n = button n down)
//   bytes 2-3  slider 0, uint16 LE
//   bytes 4-5  slider 1
//   bytes 6-7  slider 2
//   bytes 8-9  slider 3
//   byte 10    status/error flags
//
// This is the hub's single representation of "what the satellite currently
// reads", independent of which register layout the tile that produced it
// serves. A v2 tile's STATUS+DATA+SUM frame maps onto it (decodeFrame below);
// a future PY32 that serves this block natively needs no hub-side decode
// change at all.
//
// What the packet deliberately does NOT carry: the sticky pressed/released
// edge bytes. Those are events, not state — they are true exactly once, for
// the one read that consumed them. Caching an edge and replaying it is the
// precise failure the last-known-good machinery exists to prevent, so
// decodeFrame hands edges back separately and SatelliteLink never stores
// them.
//
// SEQ is an equality-only field. It is 8 bits wide here so a native producer
// can use the full range, but a v2 tile fills it from a 4-bit counter; either
// way "changed" means "not equal", never "greater than".

inline constexpr std::uint8_t kStatePacketLen = 11;

enum PacketOffset : std::uint8_t {
  kPacketSeq = 0,
  kPacketButtons = 1,
  kPacketSliders = 2,  // 4 x uint16 LE
  kPacketStatus = 10,
};

inline constexpr std::uint8_t kPacketSlidersPerTile = 4;

// Byte 10 carries the STATUS flag bits only; SEQ has its own byte, so the
// v2 STATUS byte's seq nibble is masked off on the way in.
inline constexpr std::uint8_t kPacketStatusFlagsMask = 0x0F;

struct StatePacket {
  std::uint8_t seq = 0;
  std::uint8_t buttons = 0;
  std::uint16_t sliders[kPacketSlidersPerTile] = {0, 0, 0, 0};
  std::uint8_t status = 0;  // kStatusHeartbeat / kStatusLocalFault / kStatusNotReady
};

/** Serialize a packet into the 11 wire bytes. */
inline void encodeStatePacket(const StatePacket& packet, std::uint8_t* out) {
  out[kPacketSeq] = packet.seq;
  out[kPacketButtons] = packet.buttons;
  for (std::uint8_t ch = 0; ch < kPacketSlidersPerTile; ++ch) {
    const std::uint8_t i = static_cast<std::uint8_t>(kPacketSliders + ch * 2);
    out[i] = static_cast<std::uint8_t>(packet.sliders[ch] & 0xFF);
    out[i + 1] = static_cast<std::uint8_t>(packet.sliders[ch] >> 8);
  }
  out[kPacketStatus] =
      static_cast<std::uint8_t>(packet.status & kPacketStatusFlagsMask);
}

/** Parse the 11 wire bytes back into a packet. */
inline StatePacket decodeStatePacket(const std::uint8_t* in) {
  StatePacket out;
  out.seq = in[kPacketSeq];
  out.buttons = in[kPacketButtons];
  for (std::uint8_t ch = 0; ch < kPacketSlidersPerTile; ++ch) {
    const std::uint8_t i = static_cast<std::uint8_t>(kPacketSliders + ch * 2);
    out.sliders[ch] =
        static_cast<std::uint16_t>(in[i] | (in[i + 1] << 8));
  }
  out.status = static_cast<std::uint8_t>(in[kPacketStatus] & kPacketStatusFlagsMask);
  return out;
}

/** The satellite reported a fault of its own in this packet. */
inline bool packetLocalFault(const StatePacket& packet) {
  return (packet.status & kStatusLocalFault) != 0;
}

/** The satellite has not completed its first sample sweep yet. */
inline bool packetNotReady(const StatePacket& packet) {
  return (packet.status & kStatusNotReady) != 0;
}

/**
 * One satellite read, decoded: the cached state as a StatePacket plus the
 * transient sticky edges that came with it.
 */
struct DecodedFrame {
  StatePacket packet{};
  ButtonBlock edges{};  // sticky pressed/released — consumed once, never cached
  bool valid = false;   // checksum passed and the frame was long enough
};

/**
 * Map a v2 STATUS+DATA+SUM frame, read in one transaction starting at
 * kRegStatus, onto the canonical packet.
 *
 * `frame` is the (1 + dataLen + 1) bytes as they came off the bus. A frame
 * whose checksum fails, or that is too short for the block its TYPE_ID
 * promises, decodes to valid == false and must not be trusted for any field:
 * a corrupt frame that happens to carry a plausible fader word is exactly
 * the garbage the link layer refuses to propagate.
 *
 * A tile type with no faders (kTypeButton4) leaves the slider words at 0 —
 * callers get their fader values from the slider tile's own link.
 */
inline DecodedFrame decodeFrame(std::uint8_t typeId, const std::uint8_t* frame,
                                std::uint8_t dataLen) {
  DecodedFrame out;
  const std::uint8_t buttonOffset = buttonBlockOffset(typeId);
  if (dataLen < buttonOffset + kButtonDataLen) return out;
  if (!frameChecksumOk(frame, dataLen)) return out;

  const std::uint8_t status = frame[0];
  const std::uint8_t* data = frame + 1;

  out.packet.seq = decodeStatus(status).seq;
  out.packet.status = static_cast<std::uint8_t>(status & kPacketStatusFlagsMask);
  out.packet.buttons = data[buttonOffset];
  out.edges = decodeButtonBlock(data[buttonOffset], data[buttonOffset + 1],
                                data[buttonOffset + 2]);
  if (typeId == kTypeSliderButton && dataLen >= kSliderDataLen) {
    for (std::uint8_t ch = 0; ch < kPacketSlidersPerTile; ++ch) {
      out.packet.sliders[ch] = decodeFader(data, ch);
    }
  }
  out.valid = true;
  return out;
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
