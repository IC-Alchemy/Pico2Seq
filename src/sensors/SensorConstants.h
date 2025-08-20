#ifndef SENSOR_CONSTANTS_H
#define SENSOR_CONSTANTS_H

/**
 * @file SensorConstants.h
 * @brief Centralized constants for sensor management and calibration
 * 
 * This file contains all sensor-related constants including timing parameters,
 * calibration values, hardware addresses, and parameter ranges for both
 * VL53L1X distance sensor and AS5600 magnetic encoder.
 */

namespace SensorConstants {

  // ======================
  // VL53L1X Distance Sensor Constants
  // ======================
  
  namespace DistanceSensor {
    // Hardware configuration
    static constexpr uint8_t I2C_ADDRESS = 0x29;
    static constexpr uint8_t I2C_STABILIZATION_DELAY_MS = 50;
    
    // Timing parameters
    static constexpr unsigned long READ_INTERVAL_MS = 20;
    static constexpr unsigned long TIMING_BUDGET_MICROSECONDS = 20000;  // 20ms timing budget
    static constexpr unsigned long INTER_MEASUREMENT_PERIOD_MS = 24;    // 24ms between measurements
    static constexpr unsigned long MEASUREMENT_TIMEOUT_MS = 5;          // 5ms timeout to avoid blocking LEDs
    
    // Distance measurement ranges (in millimeters)
    static constexpr int MAX_DISTANCE_HEIGHT_MM = 1400;  // Maximum useful distance
    static constexpr int MIN_DISTANCE_HEIGHT_MM = 74;    // Minimum useful distance
    static constexpr int INVALID_DISTANCE_MM = -1;       // Invalid reading indicator
  }

  // ======================
  // AS5600 Magnetic Encoder Constants
  // ======================
  
  namespace MagneticEncoder {
    // Parameter range constants
    static constexpr float PARAMETER_MIN_VALUE = 0.0f;
    static constexpr float PARAMETER_MAX_VALUE = 1.0f;
    static constexpr float NOTE_PARAMETER_MAX = 21.0f;    // Scale array indices (0-21)
    
    // Delay parameter ranges
    static constexpr float DELAY_TIME_MIN_SAMPLES = 120.0f;     // 10ms minimum delay at 48kHz
    static constexpr float DELAY_FEEDBACK_MAX = 0.91f;          // Maximum 91% feedback to prevent excessive feedback
    
    // Increment sensitivity and thresholds
    static constexpr float MINIMUM_INCREMENT_THRESHOLD = 0.0005f;  // Ignore tiny increments to prevent noise
    static constexpr float PARAMETER_RANGE_SCALE_FACTOR = 0.75f;   // 75% of full range for sequencer parameters
    
    // Flash speed zone thresholds for boundary proximity feedback
    static constexpr float NORMAL_ZONE_START = 0.0f;
    static constexpr float NORMAL_ZONE_END = 0.65f;
    static constexpr float WARNING_ZONE_START = 0.65f;
    static constexpr float WARNING_ZONE_END = 0.8375f;
    static constexpr float CRITICAL_ZONE_START = 0.8375f;
    static constexpr float CRITICAL_ZONE_END = 1.0f;
    
    // Flash speed multipliers
    static constexpr float NORMAL_FLASH_SPEED = 1.0f;
    static constexpr float WARNING_FLASH_SPEED = 2.0f;
    static constexpr float CRITICAL_FLASH_SPEED = 3.0f;
    
    // Default parameter values
    static constexpr float DEFAULT_DELAY_TIME_SAMPLES = 48000.0f * 0.2f;  // 200ms default delay at 48kHz
    static constexpr float DEFAULT_DELAY_FEEDBACK = 0.55f;                // 55% default feedback
    static constexpr float DEFAULT_VOICE_PARAMETER = 0.0f;                // Neutral position for voice parameters
  }

  // ======================
  // Sensor System Constants
  // ======================
  
  namespace System {
    // Audio system parameters for sensor integration
    static constexpr float SAMPLE_RATE_HZ = 48000.0f;
    static constexpr uint8_t MAX_VOICES = 4;
    
    // Sensor update timing
    static constexpr unsigned long SENSOR_UPDATE_INTERVAL_MS = 1;  // 1ms sensor update rate
    
    // Parameter mapping constants
    static constexpr int FILTER_FREQUENCY_MIN_HZ = 100;
    static constexpr int FILTER_FREQUENCY_MAX_HZ = 9710;
  }
}

#endif // SENSOR_CONSTANTS_H