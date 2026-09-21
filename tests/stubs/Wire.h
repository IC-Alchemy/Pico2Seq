#pragma once
#include <stdint.h>
#include <cstddef>
#include <vector>

// Minimal I2C stub. The default behavior matches the original no-op stub
// (endTransmission() == 0, requestFrom() serves nothing, read() == 0). Host
// tests may additionally opt in to transaction recording, fault injection
// and an RX feed via the members below; production-shaped calls never touch
// them, so existing suites keep passing unchanged.
class TwoWire {
public:
    void begin() {}
    void begin(uint8_t) {}
    void setClock(uint32_t) {}
    void setTimeout(uint32_t, bool = true) {}

    void beginTransmission(uint8_t address) {
        txAddress = address;
        if (recordTransmissions) {
            transmissions.emplace_back();
            transmissions.back().address = address;
        }
    }
    size_t write(uint8_t value) {
        if (recordTransmissions && !transmissions.empty())
            transmissions.back().bytes.push_back(value);
        return 1;
    }
    size_t write(const uint8_t* data, size_t n) {
        if (recordTransmissions && !transmissions.empty())
            transmissions.back().bytes.insert(transmissions.back().bytes.end(),
                                              data, data + n);
        return n;
    }
    uint8_t endTransmission(bool = true) { return endTransmissionStatus; }

    // Serves up to `count` bytes from rxFeed, oldest first — what a slave's
    // TX payload would deliver on real hardware.
    uint8_t requestFrom(uint8_t, uint8_t count, bool = true) {
        uint8_t served = 0;
        while (served < count && !rxFeed.empty()) {
            rxQueue.push_back(rxFeed.front());
            rxFeed.erase(rxFeed.begin());
            ++served;
        }
        return served;
    }
    int read() {
        if (rxQueue.empty()) return 0;
        const int value = rxQueue.front();
        rxQueue.erase(rxQueue.begin());
        return value;
    }
    int available() { return static_cast<int>(rxQueue.size()); }
    int peek() { return rxQueue.empty() ? 0 : rxQueue.front(); }

    // --- Test-only extras (additive; unused by production-shaped calls) ---
    struct Transmission {
        uint8_t address = 0;
        std::vector<uint8_t> bytes;
    };
    bool recordTransmissions = false;          // capture begin/write/end
    uint8_t endTransmissionStatus = 0;         // programmable failure value
    std::vector<uint8_t> rxFeed;               // bytes requestFrom() hands out
    std::vector<Transmission> transmissions;   // captured transactions

    void clearTestState() {
        transmissions.clear();
        rxFeed.clear();
        rxQueue.clear();
        txAddress = 0;
    }

private:
    std::vector<uint8_t> rxQueue;
    uint8_t txAddress = 0;
};

inline TwoWire Wire;
inline TwoWire Wire1;
