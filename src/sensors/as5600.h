
#ifndef AS5600_SENSOR_H
#define AS5600_SENSOR_H

#include <Arduino.h>
#include <Wire.h>

/**
 * AS5600 12-bit magnetic encoder with velocity-sensitive parameter control
 * Continuous scaling: 1280x dynamic range (0.001 - 1.28)
 */
class AS5600Sensor {
public:
    AS5600Sensor();
    bool begin();
    void update();

    uint16_t getRawAngle() const;
    float getNormalizedAngle() const;
    int32_t getCumulativePosition() const;
    float getAngularSpeed() const;

    float getParameterIncrement(float minVal, float maxVal, uint8_t maxRotations = 4) const;
    float getPositionPercentage(uint8_t maxRotations = 4) const;
    const char* getCurrentVelocityZone() const;

    bool isConnected() const;
    void resetCumulativePosition(int32_t position = 0);
    void validateSmoothScaling() const;

    // Legacy compatibility
    float mapToParameterRange(float minVal, float maxVal, uint8_t maxRotations = 4) const;
private:
    static constexpr uint8_t AS5600_ADDRESS = 0x36;
    static constexpr uint8_t AS5600_RAW_ANGLE_H = 0x0C;
    static constexpr uint8_t AS5600_RAW_ANGLE_L = 0x0D;
    static constexpr uint8_t AS5600_ANGLE_H = 0x0E;
    static constexpr uint8_t AS5600_ANGLE_L = 0x0F;
    static constexpr float RAW_TO_NORMALIZED = 1.0f / 4095.0f;

    unsigned long lastReadTime;
    uint16_t rawAngle, lastRawAngle;
    bool sensorConnected;
    int32_t cumulativePosition;
    int16_t lastPosition;
    float angularSpeed;
    unsigned long lastSpeedTime;

    // Optimized velocity scaling: enhanced low/high speed responsiveness
    // Based on measured speeds: 97.7°/s (slow) to 2331.2°/s (fast)
    static constexpr float MIN_VELOCITY_SPEED = 90.0f;    // Below measured minimum
    static constexpr float MAX_VELOCITY_SPEED = 2400.0f;  // Above measured maximum
    static constexpr float MIN_SCALE = 0.008f;            // Increased from 0.001f for better low-speed precision
    static constexpr float MAX_SCALE = 3.2f;              // Increased from 1.28f for better high-speed responsiveness

    // Curve parameters: optimized for low-speed precision and high-speed responsiveness
    static constexpr int CURVE_TYPE = 3;
    static constexpr float CURVE_EXPONENT = 1.8f;         // Reduced from 2.2f for more linear low-end
    static constexpr float CURVE_OFFSET = 0.02f;          // Reduced from 0.1f
    static constexpr float VELOCITY_SMOOTHING = 0.08f;    // Reduced from 0.12f for more responsiveness
    static constexpr float RAW_TO_DEGREES = 360.0f / 4096.0f;
    static constexpr unsigned long READ_INTERVAL_MS = 5;

    uint16_t readRegister16(uint8_t reg) const;
    bool checkConnection();
    void updateCumulativePosition();
    void updateAngularSpeed(unsigned long currentTime);
    float calculateVelocityScale(float absSpeed) const;

private:
    float normalizeSpeed(float absSpeed) const;
    float applyVelocityCurve(float normalizedSpeed) const;
    float smoothVelocity(float curvedSpeed) const;

    mutable float lastCurvedSpeed = 0.0f;
    // Helper function to calculate adaptive filter coefficient
    float calculateAdaptiveAlpha(float speedMagnitude) const;
};

// Global instance for easy access
extern AS5600Sensor as5600Sensor;

#endif // AS5600_SENSOR_H