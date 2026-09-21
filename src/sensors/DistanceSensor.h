#ifndef DISTANCE_SENSOR_H
#define DISTANCE_SENSOR_H

#include <Arduino.h>
#include <Adafruit_VL53L1X.h>
#include <Wire.h>
#include "SensorConstants.h"

// DistanceSensor.h — VL53L1X hand-height sensor (Core 0, I2C0).
// Player view: a theremin-like gesture — hand height bends the held lane live
// and zones octaves. Poll update() from the Core 0 loop; it never blocks, and
// Core 1 (audio) must never touch this.
class DistanceSensor
{
public:
  /**
   * @brief Default constructor
   *
   * Initializes the distance sensor with default timing parameters.
   * Call begin() to initialize hardware communication.
   */
  DistanceSensor();

  // Start Short-mode ranging (55-700 mm window; ceilings read as no target).
  // Needs the shared I2C bus up and the sensor at 0x29.
  bool begin();

  // Poll once: at most one data-ready check per READ_INTERVAL_MS, never a wait.
  // Call from the Core 0 loop; never from Core 1 (audio).
  void update();

  // Last accepted hand height in mm (INVALID_DISTANCE_MM = nothing in view).
  int getRawDistanceMm() const;

  // ST range status of the last sample (0 = valid, 1 = noisy-but-usable).
  // NO_RANGE_STATUS = nothing read yet.
  uint8_t getLastRangeStatus() const;

  static constexpr uint8_t NO_RANGE_STATUS = 255;

  // False until begin() ranges the sensor.
  bool isConnected() const;

private:
  // Hardware interface
  Adafruit_VL53L1X vl53l1xSensor;

  // Timing control for non-blocking updates
  unsigned long lastMeasurementTimeMs;

  // Current distance measurement in millimeters
  int currentDistanceMm;

  // Rejected measurements since the last accepted one
  uint8_t consecutiveInvalidReadings;
  uint8_t lastRangeStatus;

  // Connection status tracking
  bool sensorConnected;
};

// Legacy global (existing call sites); prefer the instance.
extern DistanceSensor distanceSensor;

// Legacy poll entry; prefer distanceSensor.update().
void updateDistanceSensor();

#endif // DISTANCE_SENSOR_H
