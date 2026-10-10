// SnapshotFormat: CRC + little-endian frame codec (see header for layout).
// Bit-by-bit IEEE CRC keeps flash dependency-free; payloads are small.
#include "SnapshotFormat.h"
#include "LittleEndian.h"

namespace persistence
{

uint32_t crc32(const uint8_t *data, size_t length) noexcept
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

void Crc32::update(const uint8_t *data, size_t length) noexcept
{
    uint32_t crc = state_;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    state_ = crc;
}

void writeFrameHeader(uint8_t out[12], uint32_t payloadSize, uint32_t payloadCrc) noexcept
{
    putLe32(out, SNAPSHOT_MAGIC);
    putLe16(out + 4, SNAPSHOT_FORMAT_VERSION);
    putLe16(out + 6, static_cast<uint16_t>(payloadSize));
    putLe32(out + 8, payloadCrc);
}

uint16_t frameVersion(const uint8_t *header) noexcept
{
    return getLe16(header + 4);
}

FrameStatus readFrameHeader(const uint8_t *header, const uint8_t *payload,
                            size_t payloadCapacity, uint16_t expectedPayloadSize,
                            uint16_t expectedVersion) noexcept
{
    if (getLe32(header) != SNAPSHOT_MAGIC)
        return FrameStatus::BadMagic;
    if (frameVersion(header) != expectedVersion)
        return FrameStatus::BadVersion;
    const uint16_t size = getLe16(header + 6);
    if (size != expectedPayloadSize)
        return FrameStatus::BadSize;
    if (payloadCapacity < size)
        return FrameStatus::TooShort;
    if (crc32(payload, size) != getLe32(header + 8))
        return FrameStatus::BadCrc;
    return FrameStatus::Ok;
}

} // namespace persistence
