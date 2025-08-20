#include "as5600.h"

// Global instance for easy access
AS5600Sensor as5600Sensor;

AS5600Sensor::AS5600Sensor()
    : lastReadTime(0)
    , rawAngle(0)
    , lastRawAngle(0)
    , sensorConnected(false)
    , cumulativePosition(0)
    , lastPosition(0)
    , angularSpeed(0.0f)
    , lastSpeedTime(0)
{
}

bool AS5600Sensor::begin() {
    Wire.begin();
    delay(50);

    sensorConnected = checkConnection();

    if (sensorConnected) {
        Serial.println("AS5600 magnetic encoder initialized successfully");
        update();
        lastRawAngle = rawAngle;
        lastPosition = static_cast<int16_t>(rawAngle);
        lastSpeedTime = millis();
    } else {
        Serial.println("[ERROR] AS5600 magnetic encoder not found!");
    }

    return sensorConnected;
}

void AS5600Sensor::update() {
    if (!sensorConnected) return;

    unsigned long currentTime = millis();
    if (currentTime - lastReadTime < READ_INTERVAL_MS) return;

    lastReadTime = currentTime;
    lastRawAngle = rawAngle;
    rawAngle = readRegister16(AS5600_ANGLE_H);

    updateCumulativePosition();
    updateAngularSpeed(currentTime);
}

void AS5600Sensor::updateCumulativePosition() {
    int16_t currentPosition = static_cast<int16_t>(rawAngle);
    int16_t delta = currentPosition - lastPosition;

    // Handle 12-bit wrap-around
    if (delta > 2048) delta -= 4096;
    else if (delta < -2048) delta += 4096;

    cumulativePosition += delta;
    lastPosition = currentPosition;
}
void AS5600Sensor::updateAngularSpeed(unsigned long currentTime) {
    // Skip if no previous measurement
    if (lastSpeedTime == 0) {
        lastSpeedTime = currentTime;
        angularSpeed = 0.0f;
        return;
    }

    // Calculate time delta and ensure it's meaningful
    unsigned long deltaTime = currentTime - lastSpeedTime;
    if (deltaTime < 8) return; // Too small time delta for accurate measurement

    // Calculate angle delta with wrap-around handling
    int16_t angleDelta = static_cast<int16_t>(rawAngle) - static_cast<int16_t>(lastRawAngle);
    
    // Handle 12-bit wrap-around (4096 values)
    if (angleDelta > 2048) angleDelta -= 4096;
    else if (angleDelta < -2048) angleDelta += 4096;

    // Convert to degrees per second
    float instantSpeed = (angleDelta * RAW_TO_DEGREES) / (deltaTime / 1000.0f);

    // Apply adaptive low-pass filter based on speed magnitude
    float speedMagnitude = abs(instantSpeed);
    float alpha = calculateAdaptiveAlpha(speedMagnitude);
    
    angularSpeed = (alpha * instantSpeed) + ((1.0f - alpha) * angularSpeed);

    // Apply noise gate for very low speeds
    if (abs(angularSpeed) < 1.0f) angularSpeed *= 0.5f;

    lastSpeedTime = currentTime;
}

// Helper function to calculate adaptive filter coefficient
float AS5600Sensor::calculateAdaptiveAlpha(float speedMagnitude) const {
    // Less smoothing for faster movements
    if (speedMagnitude < 30.0f) return 0.3f;
    if (speedMagnitude < 70.0f) return 0.4f;
    return 0.6f;
}

float AS5600Sensor::getParameterIncrement(float minVal, float maxVal, uint8_t maxRotations) const {
    // Validate input
    const float totalRange = maxVal - minVal;
    if (totalRange <= 0.0f) return 0.0f;

    // Calculate base increment per encoder step
    const float baseIncrement = totalRange / (4096.0f * maxRotations);
    
    // Apply velocity scaling
    const float velocityScale = calculateVelocityScale(abs(angularSpeed));

    // Calculate angle delta with wrap-around handling
    int16_t angleDelta = static_cast<int16_t>(rawAngle) - static_cast<int16_t>(lastRawAngle);
    
    // Handle 12-bit wrap-around
    if (angleDelta > 2048) angleDelta -= 4096;
    else if (angleDelta < -2048) angleDelta += 4096;

    // Return scaled increment
    return angleDelta * baseIncrement * velocityScale;
}

float AS5600Sensor::normalizeSpeed(float absSpeed) const {
    if (absSpeed <= MIN_VELOCITY_SPEED) return 0.0f;
    float normalized = (absSpeed - MIN_VELOCITY_SPEED) / (MAX_VELOCITY_SPEED - MIN_VELOCITY_SPEED);
    return std::min(normalized, 1.0f);
}

float AS5600Sensor::applyVelocityCurve(float normalizedSpeed) const {
    float curvedSpeed;

    if (normalizedSpeed <= 0.35f) {
        // Low speed range: Applying a quadratic curve for finer control.
        float lowSpeedNormalized = normalizedSpeed / 0.35f;
        float curved = powf(lowSpeedNormalized, 2.0f);
        curvedSpeed = curved * 0.525f; // Scale to match the previous curve's endpoint
    } else if (normalizedSpeed >= 0.75f) {
        // High speed range: Enhanced responsiveness.
        float highSpeedBoost = (normalizedSpeed - 0.65f) / 0.35f;
        curvedSpeed = 0.3f + (highSpeedBoost * 0.7f) + (highSpeedBoost * highSpeedBoost * 0.5f);
    } else {
        // Mid range: Standard exponential curve.
        float midNormalized = (normalizedSpeed - 0.2f) / 0.45f;
        curvedSpeed = 0.3f + (powf(midNormalized, CURVE_EXPONENT) * 0.4f);
    }

    return std::min(curvedSpeed, 1.0f);
}

float AS5600Sensor::smoothVelocity(float curvedSpeed) const {
    static constexpr float INV_SMOOTHING = 1.0f - VELOCITY_SMOOTHING;
    lastCurvedSpeed = (VELOCITY_SMOOTHING * curvedSpeed) + (INV_SMOOTHING * lastCurvedSpeed);
    return lastCurvedSpeed;
}

float AS5600Sensor::calculateVelocityScale(float absSpeed) const {
    if (absSpeed <= MIN_VELOCITY_SPEED) return MIN_SCALE;

    float normalizedSpeed = normalizeSpeed(absSpeed);
    float curvedSpeed = applyVelocityCurve(normalizedSpeed);
    float smoothedSpeed = smoothVelocity(curvedSpeed);

    return MIN_SCALE + (smoothedSpeed * (MAX_SCALE - MIN_SCALE));
}
uint16_t AS5600Sensor::getRawAngle() const {
    return rawAngle;
}

float AS5600Sensor::getNormalizedAngle() const {
    return static_cast<float>(rawAngle) * RAW_TO_NORMALIZED;
}

int32_t AS5600Sensor::getCumulativePosition() const {
    return cumulativePosition;
}

float AS5600Sensor::getAngularSpeed() const {
    return angularSpeed;
}



float AS5600Sensor::getPositionPercentage(uint8_t maxRotations) const {
    float percentage = static_cast<float>(cumulativePosition) / (4096.0f * maxRotations);
    return max(0.0f, min(percentage, 1.0f));
}

const char* AS5600Sensor::getCurrentVelocityZone() const {
    float currentScale = calculateVelocityScale(abs(angularSpeed));

    // Simplified zone reporting based on scale ranges
    if (currentScale <= 0.5f) return "LOW";
    else if (currentScale <= 1.5f) return "MID";
    else return "HIGH";
}

// Legacy compatibility
float AS5600Sensor::mapToParameterRange(float minVal, float maxVal, uint8_t maxRotations) const {
    float normalized = static_cast<float>(cumulativePosition) / (4096.0f * maxRotations);
    normalized = max(0.0f, min(normalized, 1.0f));
    return minVal + normalized * (maxVal - minVal);
}

void AS5600Sensor::resetCumulativePosition(int32_t position) {
    cumulativePosition = position;
    lastPosition = static_cast<int16_t>(rawAngle);
}

bool AS5600Sensor::isConnected() const {
    return sensorConnected;
}

uint16_t AS5600Sensor::readRegister16(uint8_t reg) const {
    Wire.beginTransmission(AS5600_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return 0;

    Wire.requestFrom(AS5600_ADDRESS, (uint8_t)2);
    if (Wire.available() < 2) return 0;

    uint16_t result = Wire.read() << 8;
    result |= Wire.read();

    return result & 0x0FFF; // 12-bit mask
}

bool AS5600Sensor::checkConnection() {
    Wire.beginTransmission(AS5600_ADDRESS);
    return (Wire.endTransmission() == 0);
}



void AS5600Sensor::validateSmoothScaling() const {
    Serial.println("\n=== AS5600 Optimized Scaling Validation ===");
    Serial.println("Enhanced for low-speed precision and high-speed responsiveness");

    // Test critical speed ranges
    const float testSpeeds[] = {50.0f, 97.7f, 250.0f, 500.0f, 1000.0f, 1500.0f, 2000.0f, 2331.2f};
    const int numTests = sizeof(testSpeeds) / sizeof(testSpeeds[0]);

    Serial.println("Speed(°/s) | Scale | Range");
    Serial.println("-----------|-------|------");

    for (int i = 0; i < numTests; i++) {
        float scale = calculateVelocityScale(testSpeeds[i]);
        Serial.print(testSpeeds[i], 1);
        Serial.print("     | ");
        Serial.print(scale, 3);
        Serial.print("  | ");

        if (testSpeeds[i] < 500.0f) Serial.println("Low-Speed Enhanced");
        else if (testSpeeds[i] > 1500.0f) Serial.println("High-Speed Enhanced");
        else Serial.println("Mid-Range Standard");
    }

    float lowScale = calculateVelocityScale(250.0f);   // Low speed test
    float highScale = calculateVelocityScale(2000.0f); // High speed test
    float dynamicRange = highScale / lowScale;
    Serial.print("Low-High Dynamic Range: ");
    Serial.print(dynamicRange, 1);
    Serial.println("x");
    Serial.println("=== Validation Complete ===\n");
}

