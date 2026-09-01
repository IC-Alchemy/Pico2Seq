#include "DistanceSensor.h"

// Global instance for backward compatibility with existing code
DistanceSensor distanceSensor;

DistanceSensor::DistanceSensor()
    : lastMeasurementTimeMs(0), currentDistanceMm(SensorConstants::DistanceSensor::INVALID_DISTANCE_MM), sensorConnected(false)
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
  // preset for the existing medium-range application (74-1400 mm).
  if (vl53l1xSensor.VL53L1X_SetDistanceMode(2) != VL53L1X_ERROR_NONE)
  {
    sensorConnected = false;
    return false;
  }

  // Adafruit's timing-budget setter takes milliseconds. The configured
  // 20 ms budget is one of the sensor's supported values.
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

  // Retrieve measurement data from sensor
  const int16_t distanceMm = vl53l1xSensor.distance();
  vl53l1xSensor.clearInterrupt();

  if (distanceMm < 0)
  {
    return;
  }

  // Store the new distance measurement
  currentDistanceMm = distanceMm;
}

int DistanceSensor::getRawDistanceMm() const
{
  return currentDistanceMm;
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
