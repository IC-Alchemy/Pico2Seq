#pragma once
#include <stdint.h>

class TwoWire;

#define VL53L1X_ERROR_NONE 0

// Minimal host stub for Adafruit's VL53L1X driver: only the calls
// src/sensors/DistanceSensor.cpp makes. A test sets the fake* fields, calls
// distanceSensor.begin()/update(), and the real driver code digests them.
class Adafruit_VL53L1X {
public:
    int16_t vl_status = 0;

    // Test-controllable fake sensor state.
    bool fakePresent = true;
    bool fakeDataReady = true;
    uint16_t fakeDistanceMm = 0;
    uint8_t fakeRangeStatus = 0;

    bool begin(uint8_t, TwoWire*, int32_t = 0) { return fakePresent; }
    int VL53L1X_SetDistanceMode(uint8_t) { return VL53L1X_ERROR_NONE; }
    bool setTimingBudget(uint16_t) { return true; }
    int VL53L1X_SetInterMeasurementInMs(uint16_t) { return VL53L1X_ERROR_NONE; }
    bool startRanging() { return true; }
    bool dataReady() { return fakeDataReady; }
    int VL53L1X_GetRangeStatus(uint8_t* status) {
        *status = fakeRangeStatus;
        return VL53L1X_ERROR_NONE;
    }
    int VL53L1X_GetDistance(uint16_t* distance) {
        *distance = fakeDistanceMm;
        return VL53L1X_ERROR_NONE;
    }
    void clearInterrupt() {}
};
