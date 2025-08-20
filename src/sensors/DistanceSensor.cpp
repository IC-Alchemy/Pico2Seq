#include "DistanceSensor.h"

// Global instance for backward compatibility with existing code
DistanceSensor distanceSensor;

DistanceSensor::DistanceSensor()
  : lastMeasurementTimeMs(0)
  , currentDistanceMm(SensorConstants::DistanceSensor::INVALID_DISTANCE_MM)
  , sensorConnected(false) {
}

bool DistanceSensor::begin() {
  // Initialize I2C communication with standard settings
  Wire.begin();
  delay(SensorConstants::DistanceSensor::I2C_STABILIZATION_DELAY_MS);

  // Initialize sensor hardware with configured I2C address
  vl53l1xSensor.initI2C(SensorConstants::DistanceSensor::I2C_ADDRESS, Wire);

  // Initialize sensor with error checking
  VL53L1_Error initStatus = vl53l1xSensor.initSensor();
  if (initStatus != VL53L1_ERROR_NONE) {
    Serial.print("VL53L1X sensor initialization failed with error: ");
    Serial.println(initStatus);
    sensorConnected = false;
    return false;
  }

  // Configure sensor for medium-range distance measurement
  VL53L1_Error configStatus = vl53l1xSensor.setDistanceMode(VL53L1_DISTANCEMODE_MEDIUM);
  if (configStatus != VL53L1_ERROR_NONE) {
    sensorConnected = false;
    return false;
  }

  // Set timing budget for measurement accuracy vs speed balance
  configStatus = vl53l1xSensor.setMeasurementTimingBudgetMicroSeconds(
    SensorConstants::DistanceSensor::TIMING_BUDGET_MICROSECONDS
  );
  if (configStatus != VL53L1_ERROR_NONE) {
    sensorConnected = false;
    return false;
  }

  // Configure inter-measurement period for continuous operation
  configStatus = vl53l1xSensor.setInterMeasurementPeriodMilliSeconds(
    SensorConstants::DistanceSensor::INTER_MEASUREMENT_PERIOD_MS
  );
  if (configStatus != VL53L1_ERROR_NONE) {
    sensorConnected = false;
    return false;
  }

  // Start continuous measurement mode
  configStatus = vl53l1xSensor.clearInterruptAndStartMeasurement();
  if (configStatus != VL53L1_ERROR_NONE) {
    sensorConnected = false;
    return false;
  }

  sensorConnected = true;
  Serial.println("VL53L1X distance sensor initialized successfully");
  return true;
}

void DistanceSensor::update() {
  if (!sensorConnected) {
    return;
  }

  unsigned long currentTimeMs = millis();

  // Rate-limit updates to prevent excessive I2C communication
  if (currentTimeMs - lastMeasurementTimeMs < SensorConstants::DistanceSensor::READ_INTERVAL_MS) {
    return;
  }
  lastMeasurementTimeMs = currentTimeMs;

  // Non-blocking measurement with timeout to prevent LED interference
  unsigned long measurementStartTimeMs = millis();
  VL53L1_Error dataReadyStatus = VL53L1_ERROR_NONE;

  // Wait for measurement data with timeout protection
  while ((millis() - measurementStartTimeMs) < SensorConstants::DistanceSensor::MEASUREMENT_TIMEOUT_MS) {
    dataReadyStatus = vl53l1xSensor.waitMeasurementDataReady();
    if (dataReadyStatus == VL53L1_ERROR_NONE) {
      break; // Measurement data is ready
    }
  }

  // Skip this reading cycle if timeout occurred
  if (dataReadyStatus != VL53L1_ERROR_NONE) {
    return;
  }

  // Retrieve measurement data from sensor
  VL53L1_Error measurementStatus = vl53l1xSensor.getRangingMeasurementData();
  if (measurementStatus != VL53L1_ERROR_NONE) {
    return;
  }

  // Initiate next measurement cycle for continuous operation
  vl53l1xSensor.clearInterruptAndStartMeasurement();

  // Store the new distance measurement
  currentDistanceMm = vl53l1xSensor.measurementData.RangeMilliMeter;
}

int DistanceSensor::getRawDistanceMm() const {
  return currentDistanceMm;
}

bool DistanceSensor::isConnected() const {
  return sensorConnected;
}

// Backward compatibility function for legacy code integration
void updateDistanceSensor() {
  distanceSensor.update();
}
