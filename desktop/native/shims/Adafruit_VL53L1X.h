#pragma once
// Desktop replacement for the Adafruit VL53L1X ToF distance sensor driver.
//
// begin() reports success so DistanceSensor's polling path stays live; the
// GUI stages fake measurements through p2s::host::setLidarSampleMm() and
// the driver-level status/distance reads consume them exactly like the I2C
// registers would.

#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include <cstdint>

#define VL53L1X_ERROR_NONE 0

namespace p2s
{
namespace host
{
// Stage a fake ranging result: distanceMm < 0 means "no target in view"
// (reported with a signal-fail style status so the dropout path runs).
void setLidarSampleMm(int32_t distanceMm);
}
}

class Adafruit_VL53L1X
{
public:
    int8_t vl_status = 0;

    bool begin(uint8_t = 0x29, TwoWire * = nullptr) { return true; }

    int8_t VL53L1X_SetDistanceMode(uint8_t) { return VL53L1X_ERROR_NONE; }
    bool setTimingBudget(uint16_t) { return true; }
    int8_t VL53L1X_SetInterMeasurementInMs(uint16_t) { return VL53L1X_ERROR_NONE; }
    bool startRanging() { return true; }
    void stopRanging() {}

    bool dataReady() const { return pending.load(std::memory_order_relaxed); }

    int8_t VL53L1X_GetRangeStatus(uint8_t *status)
    {
        if (status)
            *status = rangeStatus;
        return VL53L1X_ERROR_NONE;
    }

    int8_t VL53L1X_GetDistance(uint16_t *distance)
    {
        if (distance)
            *distance = static_cast<uint16_t>(distanceMm < 0 ? 0 : distanceMm);
        return VL53L1X_ERROR_NONE;
    }

    void clearInterrupt() { pending.store(false, std::memory_order_relaxed); }

private:
    friend void p2s::host::setLidarSampleMm(int32_t);

    static std::atomic<bool> pending;
    static int32_t distanceMm;
    static uint8_t rangeStatus;
};
