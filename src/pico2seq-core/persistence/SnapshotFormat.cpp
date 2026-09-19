#include "SnapshotFormat.h"

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

void writeFrameHeader(uint8_t out[12], uint32_t payloadSize, uint32_t payloadCrc) noexcept
{
    out[0] = static_cast<uint8_t>(SNAPSHOT_MAGIC);
    out[1] = static_cast<uint8_t>(SNAPSHOT_MAGIC >> 8);
    out[2] = static_cast<uint8_t>(SNAPSHOT_MAGIC >> 16);
    out[3] = static_cast<uint8_t>(SNAPSHOT_MAGIC >> 24);
    out[4] = static_cast<uint8_t>(SNAPSHOT_FORMAT_VERSION);
    out[5] = static_cast<uint8_t>(SNAPSHOT_FORMAT_VERSION >> 8);
    out[6] = static_cast<uint8_t>(payloadSize);
    out[7] = static_cast<uint8_t>(payloadSize >> 8);
    for (int i = 0; i < 4; ++i)
        out[8 + i] = static_cast<uint8_t>(payloadCrc >> (8 * i));
}

uint16_t frameVersion(const uint8_t *header) noexcept
{
    return static_cast<uint16_t>(header[4] | (uint16_t(header[5]) << 8));
}

FrameStatus readFrameHeader(const uint8_t *header, const uint8_t *payload,
                            size_t payloadCapacity, uint16_t expectedPayloadSize,
                            uint16_t expectedVersion) noexcept
{
    const uint32_t magic = header[0] | (uint32_t(header[1]) << 8) | (uint32_t(header[2]) << 16) |
                           (uint32_t(header[3]) << 24);
    if (magic != SNAPSHOT_MAGIC)
        return FrameStatus::BadMagic;
    if (frameVersion(header) != expectedVersion)
        return FrameStatus::BadVersion;
    const uint16_t size = header[6] | (uint16_t(header[7]) << 8);
    if (size != expectedPayloadSize)
        return FrameStatus::BadSize;
    if (payloadCapacity < size)
        return FrameStatus::TooShort;
    const uint32_t expected = header[8] | (uint32_t(header[9]) << 8) | (uint32_t(header[10]) << 16) |
                              (uint32_t(header[11]) << 24);
    if (crc32(payload, size) != expected)
        return FrameStatus::BadCrc;
    return FrameStatus::Ok;
}

} // namespace persistence
