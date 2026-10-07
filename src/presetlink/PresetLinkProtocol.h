// PresetLinkProtocol - the byte-level wire format between Pico2Seq and the PC editor
// (Preset Studio) over the USB serial port.
// Musical role: the cable between the editor and the box; every message is checked, so a
// torn or foreign byte can never become a sound.
// Technical role: sync + type + sequence + length + payload + CRC-32 frames, and a
// byte-at-a-time parser that hands every byte that is NOT part of a frame back to the
// caller (the firmware's text console shares this port, and the debug log shares it too).
// Portable C++ - no Arduino/hardware includes here. Little-endian throughout.
//
//   0xA5 0x5A | type u8 | seq u8 | length u16 | payload[length] | crc32 u32
//
// The CRC covers type..payload. Sync bytes have the high bit set so they cannot occur in
// the ASCII log text. Requests carry a command in `type`; the reply echoes `seq` with the
// reply bit set, or is an error frame (type 0xFF). The protocol is stop-and-wait: one
// request in flight, so there is nothing to resynchronise beyond a timeout and a resend.
#ifndef PICO2SEQ_PRESET_LINK_PROTOCOL_H
#define PICO2SEQ_PRESET_LINK_PROTOCOL_H

#include <cstddef>
#include <cstdint>

namespace presetlink
{

inline constexpr uint8_t kSync0 = 0xA5;
inline constexpr uint8_t kSync1 = 0x5A;
inline constexpr uint8_t kProtocolVersion = 1;
inline constexpr size_t kMaxPayload = 288;           // one 256-byte record plus a short prefix
inline constexpr size_t kFrameOverhead = 2 + 1 + 1 + 2 + 4;
inline constexpr size_t kMaxFrame = kFrameOverhead + kMaxPayload;
// A half-received frame is dropped after this long without a byte.
inline constexpr uint32_t kByteTimeoutMs = 250;

namespace Command
{
inline constexpr uint8_t Hello = 0x01;       // -> device facts
inline constexpr uint8_t BankBegin = 0x02;   // [u16 count] start replacing the user bank
inline constexpr uint8_t BankPut = 0x03;     // [record] one preset of the new bank
inline constexpr uint8_t BankCommit = 0x04;  // make the new bank live
inline constexpr uint8_t BankAbort = 0x05;   // throw the half-sent bank away
inline constexpr uint8_t BankRead = 0x06;    // [u16 index] one stored record
inline constexpr uint8_t Audition = 0x07;    // [u8 voice][record] play a record on a voice now
inline constexpr uint8_t FactoryRead = 0x08; // [u8 index] a factory preset as a record
inline constexpr uint8_t VoiceRead = 0x09;   // [u8 voice] capture a voice's current sound
} // namespace Command

inline constexpr uint8_t kReplyBit = 0x80;
inline constexpr uint8_t kErrorType = 0xFF;

enum class ErrorCode : uint8_t
{
    UnknownCommand = 1,
    BadPayload,   // wrong length for the command
    BadState,     // e.g. BankPut without BankBegin
    Busy,         // an upload is in progress
    NoSpace,      // the bank would not fit on flash
    Storage,      // flash write/rename failed
    InvalidRecord, // aux = patchfields row (0xFF when not a field), detail = usercodec::Problem
    OutOfRange,   // index/voice does not exist
    SlotTaken,    // two records in one upload claim the same page/pad
    CountMismatch // BankCommit before all records arrived
};

// Error payload: which command failed, why, and two bytes of detail.
struct ErrorPayload
{
    uint8_t command;
    uint8_t code;   // ErrorCode
    uint8_t detail; // InvalidRecord: usercodec::Problem
    uint8_t aux;    // InvalidRecord: patchfields row, 0xFF if none
};
static_assert(sizeof(ErrorPayload) == 4, "wire layout");

// Hello reply. Describes the device so the editor can refuse a mismatched build before
// it sends anything: same layout version and record size, and the factory list it ships.
struct HelloReply
{
    uint8_t protocol;       // kProtocolVersion
    uint8_t flags;          // kHello* bits below
    uint16_t layoutVersion; // patchfields::kLayoutVersion
    uint16_t recordSize;    // sizeof(UserPresetRecord)
    uint16_t patchSize;     // sizeof(PatchSnapshot)
    uint8_t factoryCount;
    uint8_t userPageCount;
    uint8_t padsPerPage;
    uint8_t reserved;
    uint16_t maxPresets;
    uint16_t presetCount;
    uint32_t freeBytes;     // flash left for the bank file
    uint32_t bankBytes;     // size of the live bank file
    uint32_t tableHash;     // patchfields::tableHash()
};
static_assert(sizeof(HelloReply) == 28, "wire layout");
inline constexpr uint8_t kHelloTransportRunning = 1u << 0;
inline constexpr uint8_t kHelloUploading = 1u << 1;

// Serialises one frame into `out`; returns its length, or 0 if it would not fit.
size_t encodeFrame(uint8_t type, uint8_t seq, const uint8_t *payload, size_t length,
                   uint8_t *out, size_t capacity) noexcept;

class FrameParser
{
public:
    enum class Push : uint8_t
    {
        Console, // not part of any frame: the caller may treat it as console input
        Pending, // consumed as part of a frame that is not complete yet
        Frame,   // a complete, CRC-valid frame is available from frame()
        Dropped  // a frame candidate failed validation and was discarded
    };

    struct Frame
    {
        uint8_t type = 0;
        uint8_t seq = 0;
        uint16_t length = 0;
        const uint8_t *payload = nullptr; // valid until the next push()
    };

    Push push(uint8_t byte, uint32_t nowMs) noexcept;
    const Frame &frame() const noexcept { return frame_; }
    // Drops any partial frame (call after a timeout or reconnect).
    void reset() noexcept;
    // True while a frame is half-received.
    bool inFrame() const noexcept { return state_ != State::Sync0; }

private:
    enum class State : uint8_t { Sync0, Sync1, Header, Payload, Crc };
    State state_ = State::Sync0;
    uint8_t header_[4] = {};
    uint8_t crcBytes_[4] = {};
    uint8_t payload_[kMaxPayload] = {};
    uint16_t have_ = 0; // bytes collected in the current state
    uint32_t lastByteMs_ = 0;
    Frame frame_;
};

} // namespace presetlink

#endif
