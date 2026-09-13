#ifndef PICO2SEQ_SNAPSHOT_FORMAT_H
#define PICO2SEQ_SNAPSHOT_FORMAT_H

#include <cstddef>
#include <cstdint>

namespace persistence
{

constexpr uint32_t SNAPSHOT_MAGIC = 0x50325331u; // 'P2S1'
constexpr uint16_t SNAPSHOT_FORMAT_VERSION = 1;

// CRC-32/ISO-HDLC (the zlib/IEEE variant): poly 0xEDB88320, init/final 0xFFFFFFFF.
uint32_t crc32(const uint8_t *data, size_t length) noexcept;

struct FrameHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t payloadSize;
    uint32_t crc32;
};

void writeFrameHeader(uint8_t out[12], uint32_t payloadSize, uint32_t payloadCrc) noexcept;

enum class FrameStatus { Ok, TooShort, BadMagic, BadVersion, BadSize, BadCrc };

// Header and payload may live in DIFFERENT buffers (the loader reads them
// separately) — the CRC is computed over `payload` directly, never over
// bytes following the header. `payloadCapacity` must be >= the declared size.
FrameStatus readFrameHeader(const uint8_t header[12], const uint8_t *payload,
                            size_t payloadCapacity, uint16_t expectedPayloadSize) noexcept;

} // namespace persistence

#endif
