#include "DistanceSensor.h"

// Global instance for backward compatibility with existing code
DistanceSensor distanceSensor;

DistanceSensor::DistanceSensor()
    : lastMeasurementTimeMs(0), currentDistanceMm(SensorConstants::DistanceSensor::INVALID_DISTANCE_MM),
      consecutiveInvalidReadings(0), lastRangeStatus(NO_RANGE_STATUS), sensorConnected(false)
{
}

bool DistanceSensor::begin()
{
  // Initialize I2C communication with standard settings
  Wire.begin();
  delay(SensorConstants::DistanceSensor::I2C_STABILIZATION_DELAY_MS);

  // Adafruit's begin() starts the bus, boots the sensor, and verifies its
  // model ID before returning.
  if (!vl53l1xSensor.begin(SensorConstants::DistanceSensor::I2C_ADDRESS, &Wire))
  {
    Serial.print("VL53L1X sensor initialization failed with error: ");
    Serial.println(vl53l1xSensor.vl_status);
    sensorConnected = false;
    return false;
  }

  // The Adafruit/ST driver exposes short and long presets. Use the long
  // preset for the existing medium-range application (the 55-700 mm window normalized in AppState).
  if (vl53l1xSensor.VL53L1X_SetDistanceMode(2) != VL53L1X_ERROR_NONE)
  {
    sensorConnected = false;
    return false;
  }

  // Adafruit's timing-budget setter takes milliseconds. The configured
  // 33 ms budget is one of the sensor's supported Long-mode values.
  if (!vl53l1xSensor.setTimingBudget(static_cast<uint16_t>(
          SensorConstants::DistanceSensor::TIMING_BUDGET_MICROSECONDS / 1000)))
  {
    sensorConnected = false;
    return false;
  }

  // Configure inter-measurement period for continuous operation
  if (vl53l1xSensor.VL53L1X_SetInterMeasurementInMs(static_cast<uint16_t>(
          SensorConstants::DistanceSensor::INTER_MEASUREMENT_PERIOD_MS)) !=
      VL53L1X_ERROR_NONE)
  {
    sensorConnected = false;
    return false;
  }

  // Start continuous measurement mode
  if (!vl53l1xSensor.startRanging())
  {
    sensorConnected = false;
    return false;
  }

  sensorConnected = true;
  Serial.println("VL53L1X distance sensor initialized successfully");
  return true;
}

void DistanceSensor::update()
{
  if (!sensorConnected)
  {
    return;
  }

  unsigned long currentTimeMs = millis();

  // Rate-limit updates to prevent excessive I2C communication
  if (currentTimeMs - lastMeasurementTimeMs < SensorConstants::DistanceSensor::READ_INTERVAL_MS)
  {
    return;
  }
  lastMeasurementTimeMs = currentTimeMs;

  // dataReady() performs one status check. Do not wait in the UI loop for a
  // measurement to complete.
  if (!vl53l1xSensor.dataReady())
  {
    return;
  }

  // Adafruit's distance() returns -1 for every status except 0, which also
  // discards sigma-fail readings: a real target with a noisier estimate,
  // typical of a hand near the top of the window. Read status and distance
  // directly so those still count. Signal fail and worse are rejected; they
  // can report a distance when nothing is there.
  uint8_t rangeStatus = NO_RANGE_STATUS;
  uint16_t distanceMm = 0;
  const bool readOk =
      vl53l1xSensor.VL53L1X_GetRangeStatus(&rangeStatus) == VL53L1X_ERROR_NONE &&
      vl53l1xSensor.VL53L1X_GetDistance(&distanceMm) == VL53L1X_ERROR_NONE;
  vl53l1xSensor.clearInterrupt();
  lastRangeStatus = readOk ? rangeStatus : NO_RANGE_STATUS;

  constexpr uint8_t kRangeValid = 0;
  constexpr uint8_t kRangeSigmaFail = 1;
  if (readOk && (rangeStatus == kRangeValid || rangeStatus == kRangeSigmaFail))
  {
    currentDistanceMm = distanceMm;
    consecutiveInvalidReadings = 0;
    return;
  }

  // One rejected measurement keeps the last distance. A run of them means
  // nothing is in view, so drop the distance rather than hold a hand
  // position that is no longer there.
  if (consecutiveInvalidReadings < SensorConstants::DistanceSensor::INVALID_READINGS_BEFORE_DROPOUT)
  {
    ++consecutiveInvalidReadings;
  }
  if (consecutiveInvalidReadings >= SensorConstants::DistanceSensor::INVALID_READINGS_BEFORE_DROPOUT)
  {
    currentDistanceMm = SensorConstants::DistanceSensor::INVALID_DISTANCE_MM;
  }
}

int DistanceSensor::getRawDistanceMm() const
{
  return currentDistanceMm;
}

uint8_t DistanceSensor::getLastRangeStatus() const
{
  return lastRangeStatus;
}

bool DistanceSensor::isConnected() const
{
  return sensorConnected;
}

// Backward compatibility function for legacy code integration
void updateDistanceSensor()
{
  distanceSensor.update();
}
