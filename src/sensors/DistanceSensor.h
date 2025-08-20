#ifndef DISTANCE_SENSOR_H
#define DISTANCE_SENSOR_H

#include <Arduino.h>
#include <Melopero_VL53L1X.h>
#include <Wire.h>
#include "SensorConstants.h"

/**
 * @class DistanceSensor
 * @brief VL53L1X distance sensor driver for real-time parameter control
 *
 * Lightweight driver that provides distance readings for real-time parameter
 * control in the PicoMudrasSequencer. Optimized for audio applications with
 * non-blocking measurement updates and configurable timing parameters.
 * 
 * The sensor operates in continuous measurement mode with a 20ms update interval
 * to provide smooth parameter control while avoiding interference with audio
 * processing on Core 0.
 * 
 * @note This class is designed for Core 1 operation (UI/sensor processing)
 * @warning Measurement timeouts are limited to 5ms to prevent LED blocking
 */
class DistanceSensor {
public:
  /**
   * @brief Default constructor
   * 
   * Initializes the distance sensor with default timing parameters.
   * Call begin() to initialize hardware communication.
   */
  DistanceSensor();

  /**
   * @brief Initialize VL53L1X sensor with optimized settings
   * 
   * Configures the sensor for medium-range distance measurement with
   * 20ms timing budget and 24ms inter-measurement period. Uses continuous
   * measurement mode for real-time parameter control applications.
   * 
   * @return true if initialization successful, false on hardware error
   * @note Requires I2C bus to be available and sensor connected at address 0x29
   */
  bool begin();

  /**
   * @brief Non-blocking sensor update with timeout protection
   * 
   * Polls the sensor for new distance measurements with a 5ms timeout to
   * prevent blocking LED matrix updates. Updates are limited to 20ms intervals
   * to balance responsiveness with system performance.
   * 
   * @note Call this function regularly from Core 1 main loop
   * @warning Do not call from Core 0 (audio processing core)
   */
  void update();

  /**
   * @brief Get the most recent distance measurement
   * 
   * Returns the last valid distance reading in millimeters. The reading
   * is updated by the update() function and represents the distance from
   * the sensor to the nearest object within the measurement range.
   * 
   * @return Distance in millimeters (74-1400mm typical range)
   * @note Returns the last valid reading even if sensor communication fails
   */
  int getRawDistanceMm() const;

  /**
   * @brief Check if sensor is providing valid readings
   * 
   * @return true if sensor is connected and providing measurements
   */
  bool isConnected() const;

private:
  // Hardware interface
  Melopero_VL53L1X vl53l1xSensor;

  // Timing control for non-blocking updates
  unsigned long lastMeasurementTimeMs;

  // Current distance measurement in millimeters
  int currentDistanceMm;

  // Connection status tracking
  bool sensorConnected;
};

// Global instance for backward compatibility with existing code
extern DistanceSensor distanceSensor;

/**
 * @brief Backward compatibility function for legacy code
 * 
 * Updates the global distance sensor instance. This function maintains
 * compatibility with existing code that expects a simple update function.
 * 
 * @deprecated Use distanceSensor.update() directly for new code
 */
void updateDistanceSensor();

#endif // DISTANCE_SENSOR_H
