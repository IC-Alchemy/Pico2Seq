// PresetLinkProtocol.cpp - frame encoder and byte-wise parser.
#include "PresetLinkProtocol.h"

#include "../pico2seq-core/persistence/LittleEndian.h"
#include "../pico2seq-core/persistence/SnapshotFormat.h"
#include <cstring>

namespace presetlink
{

size_t encodeFrame(uint8_t type, uint8_t seq, const uint8_t *payload, size_t length,
                   uint8_t *out, size_t capacity) noexcept
{
    if (length > kMaxPayload || capacity < kFrameOverhead + length || (length && !payload))
        return 0;
    out[0] = kSync0;
    out[1] = kSync1;
    out[2] = type;
    out[3] = seq;
    persistence::putLe16(out + 4, static_cast<uint16_t>(length));
    if (length)
        std::memcpy(out + 6, payload, length);
    persistence::Crc32 crc;
    crc.update(out + 2, 4 + length);
    persistence::putLe32(out + 6 + length, crc.value());
    return kFrameOverhead + length;
}

void FrameParser::reset() noexcept
{
    state_ = State::Sync0;
    have_ = 0;
}

FrameParser::Push FrameParser::push(uint8_t byte, uint32_t nowMs) noexcept
{
    // A frame abandoned half-way (cable pulled, host crashed) must not swallow the
    // start of the next one.
    if (state_ != State::Sync0 && nowMs - lastByteMs_ > kByteTimeoutMs)
        reset();
    lastByteMs_ = nowMs;

    switch (state_)
    {
    case State::Sync0:
        if (byte != kSync0)
            return Push::Console;
        state_ = State::Sync1;
        return Push::Pending;

    case State::Sync1:
        if (byte == kSync1)
        {
            state_ = State::Header;
            have_ = 0;
            return Push::Pending;
        }
        if (byte == kSync0)
            return Push::Pending; // A5 A5 5A: the second A5 is the real start
        state_ = State::Sync0;
        return Push::Console;

    case State::Header:
        header_[have_++] = byte;
        if (have_ < sizeof header_)
            return Push::Pending;
        frame_.type = header_[0];
        frame_.seq = header_[1];
        frame_.length = persistence::getLe16(header_ + 2);
        have_ = 0;
        if (frame_.length > kMaxPayload)
        {
            reset();
            return Push::Dropped;
        }
        state_ = frame_.length ? State::Payload : State::Crc;
        return Push::Pending;

    case State::Payload:
        payload_[have_++] = byte;
        if (have_ < frame_.length)
            return Push::Pending;
        have_ = 0;
        state_ = State::Crc;
        return Push::Pending;

    case State::Crc:
    {
        crcBytes_[have_++] = byte;
        if (have_ < sizeof crcBytes_)
            return Push::Pending;
        persistence::Crc32 crc;
        crc.update(header_, sizeof header_);
        crc.update(payload_, frame_.length);
        const uint32_t expected = persistence::getLe32(crcBytes_);
        reset();
        if (crc.value() != expected)
            return Push::Dropped;
        frame_.payload = payload_;
        return Push::Frame;
    }
    }
    return Push::Console;
}

} // namespace presetlink
