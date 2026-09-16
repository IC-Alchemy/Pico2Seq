#pragma once
// Wire.h — a scriptable I2C bus for driving the real AlchemyTiles driver.
//
// Shadows tests/stubs/Wire.h (which is a no-op) for the pico2seq_tile_tests
// target only. Arduino.h still comes from tests/stubs.
//
// The point of this fake is that AlchemyTiles.cpp is compiled unmodified and
// talks to it exactly as it talks to hardware: pointer write terminated with
// STOP, then a separate read that auto-increments from the pointer. Tests
// attach FakeTile devices, make them misbehave (NACK, short read, corrupt
// checksum, silence), and assert on both what the driver decoded and how many
// transactions it spent getting there.

#include <stdint.h>

#include <cstddef>
#include <map>
#include <vector>

#include "AlchemyUI/src/AlchemyProto.h"

/**
 * One satellite on the fake bus: an identity block plus a live register file
 * whose frame the test edits directly.
 */
class FakeTile
{
public:
    FakeTile() = default;

    FakeTile(uint8_t typeId, uint8_t dataLen) : typeId_(typeId), dataLen_(dataLen)
    {
        for (uint8_t i = 0; i < alchemy::kIdentityReadLength; ++i) identity_[i] = 0;
        identity_[alchemy::kRegWhoAmI] = alchemy::kWhoAmIMagic;
        identity_[alchemy::kRegTypeId] = typeId;
        identity_[alchemy::kRegProtoVer] = alchemy::kProtoVerV2;
        identity_[alchemy::kRegFwVer] = 1;
        identity_[alchemy::kRegHwRev] = 1;
        identity_[alchemy::kRegAddrOffset] = 2;
        identity_[alchemy::kRegCaps] = alchemy::kCapAnalogIn | alchemy::kCapDigitalIn;
        identity_[alchemy::kRegDataLen] = dataLen;
        setState(0, 0);
    }

    /** Publish a new snapshot: SEQ, button level, and (slider tiles) faders. */
    void setState(uint8_t seq, uint8_t buttonLevel, const uint16_t *faders = nullptr)
    {
        seq_ = static_cast<uint8_t>(seq & alchemy::kStatusSeqMask);
        heartbeat_ = !heartbeat_;
        rebuildStatus();

        for (uint8_t i = 0; i < alchemy::kSliderDataLen; ++i) data_[i] = 0;
        const uint8_t offset = alchemy::buttonBlockOffset(typeId_);
        data_[offset] = buttonLevel;
        data_[offset + 1] = stickyPressed_;
        data_[offset + 2] = stickyReleased_;
        if (typeId_ == alchemy::kTypeSliderButton && faders != nullptr)
        {
            for (uint8_t ch = 0; ch < alchemy::kFadersPerTile; ++ch)
            {
                data_[ch * 2] = static_cast<uint8_t>(faders[ch] & 0xFF);
                data_[ch * 2 + 1] = static_cast<uint8_t>(faders[ch] >> 8);
            }
        }
    }

    /** Arm the sticky edge bytes for the next frame the driver reads. */
    void setEdges(uint8_t pressed, uint8_t released)
    {
        stickyPressed_ = pressed;
        stickyReleased_ = released;
        const uint8_t offset = alchemy::buttonBlockOffset(typeId_);
        data_[offset + 1] = pressed;
        data_[offset + 2] = released;
    }

    /**
     * The satellite's own sample sweep, as the real firmware runs it: every
     * SWEEP_INTERVAL_MS it republishes and HEARTBEAT toggles, whether or not
     * anything changed. Driven from the test's clock so a tile left alone
     * still looks alive rather than looking frozen.
     */
    void tick(uint32_t nowMilliseconds)
    {
        if (frozen_) return;
        if (nowMilliseconds - lastSweepMs_ < kSweepIntervalMs) return;
        lastSweepMs_ = nowMilliseconds;
        heartbeat_ = !heartbeat_;
        rebuildStatus();
    }

    // Failure injection.
    void setOffline(bool offline) { offline_ = offline; }      // NACKs everything
    void setShortRead(bool shortRead) { shortRead_ = shortRead; }
    void setCorruptChecksum(bool corrupt) { corrupt_ = corrupt; }
    /**
     * Stop sweeping while still answering the bus perfectly: every read still
     * returns a well-formed, checksum-correct frame, but STATUS and DATA are
     * frozen. This is the tile firmware's publish-stall — the slave ISR keeps
     * serving whichever buffer it last latched, so the failure is invisible to
     * anything that treats "a packet arrived" as liveness.
     */
    void setFrozen(bool frozen) { frozen_ = frozen; }

    [[nodiscard]] uint8_t dataLen() const { return dataLen_; }
    [[nodiscard]] bool offline() const { return offline_; }
    [[nodiscard]] bool shortReadArmed() const { return shortRead_; }

    /** Serve `count` bytes from `reg`, as the slave's auto-incrementing read. */
    void readInto(uint8_t reg, uint8_t count, std::vector<uint8_t> &out) const
    {
        for (uint8_t i = 0; i < count; ++i)
        {
            out.push_back(byteAt(static_cast<uint8_t>(reg + i)));
        }
        if (corrupt_ && count > 1) out[1] = static_cast<uint8_t>(out[1] ^ 0x01);
    }

private:
    static constexpr uint32_t kSweepIntervalMs = 4; // SWEEP_INTERVAL_MS on the tile

    void rebuildStatus()
    {
        status_ = static_cast<uint8_t>(seq_ << alchemy::kStatusSeqShift);
        if (heartbeat_) status_ |= alchemy::kStatusHeartbeat;
    }

    [[nodiscard]] uint8_t byteAt(uint8_t reg) const
    {
        if (reg < alchemy::kIdentityReadLength) return identity_[reg];
        if (reg == alchemy::kRegStatus) return status_;
        if (reg >= alchemy::kRegData && reg < alchemy::kRegData + dataLen_)
        {
            return data_[reg - alchemy::kRegData];
        }
        if (reg == alchemy::regSum(dataLen_))
        {
            return alchemy::frameSum(status_, data_, dataLen_);
        }
        return 0; // over-reads pad with zeroes and never NACK
    }

    uint8_t typeId_ = alchemy::kTypeSliderButton;
    uint8_t dataLen_ = alchemy::kSliderDataLen;
    uint8_t identity_[alchemy::kIdentityReadLength] = {0};
    uint8_t data_[alchemy::kSliderDataLen] = {0};
    uint8_t status_ = 0;
    uint8_t seq_ = 0;
    uint8_t stickyPressed_ = 0;
    uint8_t stickyReleased_ = 0;
    uint32_t lastSweepMs_ = 0;
    bool heartbeat_ = false;
    bool offline_ = false;
    bool shortRead_ = false;
    bool corrupt_ = false;
    bool frozen_ = false;
};

class TwoWire
{
public:
    void begin() {}
    void begin(uint8_t) {}
    void setClock(uint32_t) {}

    void beginTransmission(uint8_t address)
    {
        txAddress_ = address;
        txBytes_.clear();
    }

    size_t write(uint8_t value)
    {
        txBytes_.push_back(value);
        return 1;
    }

    size_t write(const uint8_t *data, size_t n)
    {
        for (size_t i = 0; i < n; ++i) txBytes_.push_back(data[i]);
        return n;
    }

    uint8_t endTransmission(bool = true)
    {
        ++pointerWrites_;
        FakeTile *tile = find(txAddress_);
        if (tile == nullptr || tile->offline()) return 2; // NACK on address
        if (!txBytes_.empty()) pointer_[txAddress_] = txBytes_.front();
        return 0;
    }

    uint8_t requestFrom(uint8_t address, uint8_t count, bool = true)
    {
        ++blockReads_;
        lastReadLength_ = count;
        rx_.clear();
        rxCursor_ = 0;
        FakeTile *tile = find(address);
        if (tile == nullptr || tile->offline()) return 0;
        const uint8_t served = tile->shortReadArmed() ? static_cast<uint8_t>(count / 2)
                                                      : count;
        tile->readInto(pointer_[address], served, rx_);
        pointer_[address] = static_cast<uint8_t>(pointer_[address] + served);
        return served;
    }

    int read()
    {
        if (rxCursor_ >= rx_.size()) return 0;
        return rx_[rxCursor_++];
    }

    int available() { return static_cast<int>(rx_.size() - rxCursor_); }
    int peek() { return rxCursor_ < rx_.size() ? rx_[rxCursor_] : 0; }

    // --- Test harness ---------------------------------------------------------

    void attach(uint8_t address, const FakeTile &tile) { tiles_[address] = tile; }
    FakeTile &tile(uint8_t address) { return tiles_[address]; }

    /** Run every attached tile's own sample sweep up to `nowMilliseconds`. */
    void tickAll(uint32_t nowMilliseconds)
    {
        for (auto &entry : tiles_) entry.second.tick(nowMilliseconds);
    }

    [[nodiscard]] bool hasTile(uint8_t address) const
    {
        return tiles_.find(address) != tiles_.end();
    }

    void resetCounters()
    {
        pointerWrites_ = 0;
        blockReads_ = 0;
    }

    [[nodiscard]] uint32_t pointerWrites() const { return pointerWrites_; }
    [[nodiscard]] uint32_t blockReads() const { return blockReads_; }
    [[nodiscard]] uint8_t lastReadLength() const { return lastReadLength_; }

private:
    FakeTile *find(uint8_t address)
    {
        auto it = tiles_.find(address);
        return it == tiles_.end() ? nullptr : &it->second;
    }

    std::map<uint8_t, FakeTile> tiles_;
    std::map<uint8_t, uint8_t> pointer_;
    std::vector<uint8_t> txBytes_;
    std::vector<uint8_t> rx_;
    size_t rxCursor_ = 0;
    uint8_t txAddress_ = 0;
    uint8_t lastReadLength_ = 0;
    uint32_t pointerWrites_ = 0;
    uint32_t blockReads_ = 0;
};

inline TwoWire Wire;
