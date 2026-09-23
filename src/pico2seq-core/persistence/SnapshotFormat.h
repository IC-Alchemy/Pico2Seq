// SnapshotFormat: flash framing for saved songs (magic + version + size + CRC).
// A corrupt/torn write must fail loudly, never replay as a wrong song.
// Portable C++ — no Arduino/hardware includes here.
#ifndef PICO2SEQ_SNAPSHOT_FORMAT_H
#define PICO2SEQ_SNAPSHOT_FORMAT_H

#include <cstddef>
#include <cstdint>

namespace persistence
{

constexpr uint32_t SNAPSHOT_MAGIC = 0x50325331u; // 'P2S1': rejects non-song flash at once
// 3: ProjectSnapshot grew the per-voice sitar tails (2026-09-23). Old songs
// still load: v1 and v2 payloads are prefixes of v3 (see ProjectSnapshot).
constexpr uint16_t SNAPSHOT_FORMAT_VERSION = 3;
constexpr uint16_t SNAPSHOT_FORMAT_VERSION_V1 = 1;
constexpr uint16_t SNAPSHOT_FORMAT_VERSION_V2 = 2;

// IEEE CRC over the payload only; catches torn flash writes and bit rot.
uint32_t crc32(const uint8_t *data, size_t length) noexcept;

// 12-byte little-endian frame header; fixed size keeps flash offsets stable.
struct FrameHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t payloadSize;
    uint32_t crc32;
};

void writeFrameHeader(uint8_t out[12], uint32_t payloadSize, uint32_t payloadCrc) noexcept;

enum class FrameStatus { Ok, TooShort, BadMagic, BadVersion, BadSize, BadCrc };

// Reject in order: wrong file, wrong version, wrong size, short buffer, bad CRC.
FrameStatus readFrameHeader(const uint8_t header[12], const uint8_t *payload,
                            size_t payloadCapacity, uint16_t expectedPayloadSize,
                            uint16_t expectedVersion = SNAPSHOT_FORMAT_VERSION) noexcept;

// Peek the version first so the loader picks the v1 vs v2 payload size.
uint16_t frameVersion(const uint8_t header[12]) noexcept;

} // namespace persistence

#endif
