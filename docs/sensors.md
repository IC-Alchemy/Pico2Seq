# Sensors Module

## Overview

The sensors module provides real-time parameter control for the Pico2Seq synthesizer using two complementary sensor types:

- **AS5600 Magnetic Encoders**: Precise rotary control with velocity-sensitive scaling for parameter adjustment
- **VL53L1X Distance Sensors**: Time-of-flight distance measurement for hands-free parameter modulation

The module is designed for Core 1 operation to avoid interference with Core 0 audio processing, with non-blocking updates and timeout protection to ensure system stability.

## Key Components

### AS5600Manager
High-level management system for AS5600 magnetic encoders. Handles parameter increment calculation, boundary checking, and integration with the synthesizer's voice and delay parameters.

### AS5600Sensor
Low-level driver for AS5600 magnetic encoder. Provides 12-bit angle resolution with velocity-sensitive scaling and wrap-around handling.

### DistanceSensor
Driver for VL53L1X time-of-flight distance sensor. Uses the Melopero_VL53L1X library for reliable distance measurements in the 74-1400mm range.

### SensorConstants
Centralized configuration file containing all sensor-related constants, hardware addresses, timing parameters, and calibration values.

## Public Classes and Structures

### AS5600Manager Functions

#### Core Parameter Management
```cpp
void applyIncrementToParameter(AS5600BaseValues* baseValues, AS5600ParameterMode param, float increment);
void updateAS5600BaseValues(UIState& uiState);
void updateAS5600StepParameterValues(UIState& uiState);
void applyAS5600BaseValues(VoiceState *voiceState, uint8_t voiceId);
void applyAS5600DelayValues();
void applyAS5600SlideTimeValues();
```

#### Parameter Range and Validation
```cpp
float getParameterMinValue(AS5600ParameterMode param);
float getParameterMaxValue(AS5600ParameterMode param);
float getAS5600BaseValueRange(AS5600ParameterMode param);
float clampAS5600BaseValue(AS5600ParameterMode param, float value);
```

#### Step Parameter Editing
```cpp
ParamId convertAS5600ParameterToParamId(AS5600ParameterMode as5600Param);
float getParameterMinValueForParamId(ParamId paramId);
float getParameterMaxValueForParamId(ParamId paramId);
String formatParameterValueForDisplay(ParamId paramId, float value);
```

#### System Management
```cpp
void resetAS5600BaseValues(UIState& uiState, bool currentVoiceOnly = true);
void initAS5600BaseValues();
```

### AS5600Sensor Methods
```cpp
class AS5600Sensor {
public:
    bool begin();                                    // Initialize sensor hardware
    void update();                                   // Update sensor readings
    uint16_t getRawAngle() const;                    // Get raw 12-bit angle (0-4095)
    float getNormalizedAngle() const;                // Get normalized angle (0.0-1.0)
    int32_t getCumulativePosition() const;           // Get cumulative position with wrap-around
    float getAngularSpeed() const;                   // Get angular speed in °/s
    float getParameterIncrement(float minVal, float maxVal, uint8_t maxRotations = 4) const;
    float getPositionPercentage(uint8_t maxRotations = 4) const;
    bool isConnected() const;                        // Check sensor connection
    void resetCumulativePosition(int32_t position = 0);
};
```

### DistanceSensor Methods
```cpp
class DistanceSensor {
public:
    bool begin();                    // Initialize VL53L1X sensor
    void update();                   // Non-blocking sensor update
    int getRawDistanceMm() const;    // Get distance in millimeters
    bool isConnected() const;        // Check sensor connection
};
```

### Enums and Structs

#### FlashSpeedZone
```cpp
enum class FlashSpeedZone : uint8_t {
    Normal = 0,   // Normal operation range
    Warning = 1,  // Warning zone (approaching limits)
    Critical = 2  // Critical zone (at boundaries)
};
```

#### FlashSpeedConfig
```cpp
struct FlashSpeedConfig {
    float speedMultiplier;  // Flash speed multiplier (1.0x to 3.0x)
    float thresholdStart;   // Zone start threshold (0.0-1.0)
    float thresholdEnd;     // Zone end threshold (0.0-1.0)
};
```

## Important Constants and Configuration

### VL53L1X Distance Sensor Constants
```cpp
namespace DistanceSensor {
    static constexpr uint8_t I2C_ADDRESS = 0x29;
    static constexpr uint8_t I2C_STABILIZATION_DELAY_MS = 50;
    static constexpr unsigned long READ_INTERVAL_MS = 20;
    static constexpr unsigned long TIMING_BUDGET_MICROSECONDS = 20000;
    static constexpr unsigned long INTER_MEASUREMENT_PERIOD_MS = 24;
    static constexpr unsigned long MEASUREMENT_TIMEOUT_MS = 5;
    static constexpr int MAX_DISTANCE_HEIGHT_MM = 1400;
    static constexpr int MIN_DISTANCE_HEIGHT_MM = 74;
    static constexpr int INVALID_DISTANCE_MM = -1;
}
```

### AS5600 Magnetic Encoder Constants
```cpp
namespace MagneticEncoder {
    static constexpr float PARAMETER_MIN_VALUE = 0.0f;
    static constexpr float PARAMETER_MAX_VALUE = 1.0f;
    static constexpr float NOTE_PARAMETER_MAX = 21.0f;
    static constexpr float DELAY_TIME_MIN_SAMPLES = 120.0f;
    static constexpr float DELAY_FEEDBACK_MAX = 0.91f;
    static constexpr float MINIMUM_INCREMENT_THRESHOLD = 0.0005f;
    static constexpr float PARAMETER_RANGE_SCALE_FACTOR = 0.75f;

    // Flash speed zone thresholds
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
    static constexpr float DEFAULT_DELAY_TIME_SAMPLES = 48000.0f * 0.2f;
    static constexpr float DEFAULT_DELAY_FEEDBACK = 0.55f;
    static constexpr float DEFAULT_VOICE_PARAMETER = 0.0f;
}
```

### System Constants
```cpp
namespace System {
    static constexpr float SAMPLE_RATE_HZ = 48000.0f;
    static constexpr uint8_t MAX_VOICES = 4;
    static constexpr unsigned long SENSOR_UPDATE_INTERVAL_MS = 1;
    static constexpr int FILTER_FREQUENCY_MIN_HZ = 100;
    static constexpr int FILTER_FREQUENCY_MAX_HZ = 9710;
}
```

## Example Usage

### Basic Sensor Initialization
```cpp
// Initialize AS5600 magnetic encoder
if (!as5600Sensor.begin()) {
    Serial.println("AS5600 initialization failed!");
}

// Initialize VL53L1X distance sensor
if (!distanceSensor.begin()) {
    Serial.println("VL53L1X initialization failed!");
}
```

### Main Loop Sensor Updates
```cpp
void loop() {
    // Update sensors (non-blocking)
    as5600Sensor.update();
    distanceSensor.update();

    // Update parameter values based on encoder input
    updateAS5600BaseValues(uiState);

    // Apply sensor values to synthesis parameters
    if (uiState.currentAS5600Parameter <= AS5600ParameterMode::Decay) {
        // Voice parameters
        applyAS5600BaseValues(voiceState, currentVoiceId);
    } else {
        // Global parameters (delay, slide)
        applyAS5600DelayValues();
        applyAS5600SlideTimeValues();
    }
}
```

### Step Parameter Editing
```cpp
// Enable step editing mode
uiState.selectedStepForEdit = stepIndex;
uiState.currentEditParameter = ParamId::Velocity;

// The encoder will now control the specific step parameter
// instead of the global voice parameters
```

### Reading Sensor Values
```cpp
// AS5600 encoder
if (as5600Sensor.isConnected()) {
    float angle = as5600Sensor.getNormalizedAngle();      // 0.0 to 1.0
    float speed = as5600Sensor.getAngularSpeed();         // degrees per second
    int32_t position = as5600Sensor.getCumulativePosition(); // cumulative rotations
}

// VL53L1X distance sensor
if (distanceSensor.isConnected()) {
    int distance = distanceSensor.getRawDistanceMm();     // distance in mm
    if (distance > 0) {  // Valid reading
        // Use distance for parameter modulation
    }
}
```

## Dependencies and Integration Points

### Hardware Dependencies
- **AS5600 Magnetic Encoder**: I2C address 0x36, 12-bit resolution
- **VL53L1X Distance Sensor**: I2C address 0x29, 74-1400mm range

### Software Dependencies
- **Melopero_VL53L1X**: Arduino library for VL53L1X sensor
- **Arduino Wire library**: I2C communication
- **UIState**: User interface state management
- **VoiceState**: Voice synthesis parameters
- **VoiceManager**: Voice management system
- **Sequencer**: Step sequencer integration

### Integration Points
- **Voice Parameters**: Velocity, filter cutoff, attack/decay times
- **Delay Effects**: Delay time and feedback control
- **Slide Parameters**: Note transition smoothing
- **Sequencer Steps**: Individual step parameter editing
- **OLED Display**: Parameter value formatting
- **LED Matrix**: Visual feedback for parameter boundaries

## Velocity-Sensitive Scaling

The AS5600 encoder implements advanced velocity-sensitive scaling with three distinct ranges:

- **Low Speed (≤ 250°/s)**: Enhanced precision with quadratic curves
- **Mid Speed (250-1500°/s)**: Standard exponential scaling
- **High Speed (≥ 1500°/s)**: Enhanced responsiveness for rapid control

**Dynamic Range**: 1280x (0.008x to 3.2x scaling factor)

**Adaptive Filtering**: Low-pass filter coefficients adjust based on rotation speed for smooth control.

## "Shift and Scale" Mapping Algorithm

The system uses a "Shift and Scale" algorithm to combine encoder base values with sequencer step values:

```cpp
float shiftAndScale(float seqValue, float as5600Offset) {
    if (as5600Offset >= 0.0f) {
        // Positive offset sets minimum value
        return as5600Offset + (seqValue * (1.0f - as5600Offset));
    } else {
        // Negative offset reduces maximum value
        return seqValue * (1.0f + as5600Offset);
    }
}
```

This approach avoids dead zones by scaling the sequencer output within the dynamic range defined by the encoder offset.

## Troubleshooting

### AS5600 Issues
- **No Response**: Check I2C connection and address 0x36
- **Erratic Readings**: Verify magnetic encoder placement and magnet strength
- **Slow Response**: Check velocity scaling parameters in AS5600Sensor.cpp
- **Parameter Limits**: Verify min/max values in SensorConstants.h

### VL53L1X Issues
- **Initialization Failure**: Check I2C address 0x29 and wiring
- **Invalid Readings**: Ensure target is within 74-1400mm range
- **Timeout Errors**: Verify MEASUREMENT_TIMEOUT_MS (currently 5ms)
- **LED Interference**: Check for electrical noise from LED matrix

### System Integration Issues
- **Audio Glitches**: Ensure sensors run on Core 1 only
- **Parameter Conflicts**: Verify UI state management for voice selection
- **Memory Issues**: Check for buffer overflows in cumulative position tracking

### Performance Tuning
- **Update Frequency**: Adjust READ_INTERVAL_MS for responsiveness vs. performance balance
- **Velocity Thresholds**: Tune MINIMUM_INCREMENT_THRESHOLD to reduce noise
- **Flash Zones**: Adjust boundary proximity thresholds for visual feedback

## Blocking Issues

### Resolved Dependencies
- ✅ All required header files present
- ✅ I2C addresses properly defined
- ✅ External library references documented

### Known Limitations
- **Single Distance Sensor**: Currently supports one VL53L1X sensor
- **Fixed I2C Addresses**: No runtime address configuration
- **Core 1 Requirement**: Sensors must run on UI core to avoid audio interference
- **Magnet Dependency**: AS5600 requires proper magnet installation for accurate readings

### Missing Components
- **None identified**: All referenced classes and functions are implemented
- **External Libraries**: Requires Melopero_VL53L1X library installation

## Files Read
- `src/sensors/SensorConstants.h` - Centralized sensor constants
- `src/sensors/AS5600Manager.h` - High-level encoder management interface
- `src/sensors/DistanceSensor.h` - Distance sensor interface
- `src/sensors/as5600.h` - Low-level AS5600 driver interface
- `src/sensors/AS5600Manager.cpp` - Encoder management implementation
- `src/sensors/as5600.cpp` - AS5600 driver implementation
- `src/sensors/DistanceSensor.cpp` - Distance sensor implementation

## Integration Checklist
- [ ] Install Melopero_VL53L1X library
- [ ] Connect AS5600 to I2C bus (address 0x36)
- [ ] Connect VL53L1X to I2C bus (address 0x29)
- [ ] Verify magnet installation on AS5600
- [ ] Test sensor initialization in setup()
- [ ] Verify non-blocking updates in main loop
- [ ] Test parameter control responsiveness
- [ ] Verify Core 1 operation (not Core 0)