// LittleEndian: byte-wise integer packing for the flash and serial formats.
// Why not memcpy or a cast: song files, the user preset bank and the preset-link frames are
// specified as little-endian, and the PC editor reads the same bytes. Packing byte by byte
// keeps the format independent of the host's endianness and of struct alignment, so the
// host tests and the RP2350 agree on the bytes by construction.
// Portable C++ - no Arduino/hardware includes here.
#ifndef PICO2SEQ_LITTLE_ENDIAN_H
#define PICO2SEQ_LITTLE_ENDIAN_H

#include <cstdint>

namespace persistence
{

inline void putLe16(uint8_t *out, uint16_t value) noexcept
{
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
}

inline void putLe32(uint8_t *out, uint32_t value) noexcept
{
    putLe16(out, static_cast<uint16_t>(value));
    putLe16(out + 2, static_cast<uint16_t>(value >> 16));
}

inline uint16_t getLe16(const uint8_t *in) noexcept
{
    return static_cast<uint16_t>(in[0] | (uint16_t(in[1]) << 8));
}

inline uint32_t getLe32(const uint8_t *in) noexcept
{
    return getLe16(in) | (uint32_t(getLe16(in + 2)) << 16);
}

} // namespace persistence

#endif
