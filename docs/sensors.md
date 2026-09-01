# Sensors Module

## Overview

The sensors module provides real-time parameter control for the Pico2Seq synthesizer using two complementary sensor types:

- **TMAG5273 Magnetic Encoder** (Velocity Encoder board): Precise rotary control with velocity-sensitive scaling for parameter adjustment
- **VL53L1X Distance Sensors**: Time-of-flight distance measurement for hands-free parameter modulation

The module is designed for Core 1 operation to avoid interference with Core 0 audio processing, with non-blocking updates and one data-ready poll per update to ensure system stability.

## Key Components

### EncoderManager
High-level management system for the magnetic encoder. Handles parameter increment calculation, boundary checking, and integration with the synthesizer's voice and delay parameters.

### MagEncoder
Low-level driver for the magnetic encoder, from the `src/VelocityEncoder` library (Git submodule). One driver wraps either a TI TMAG5273 or an AMS AS5600 behind a single API; Pico2Seq configures it for the **TMAG5273** fitted on the Velocity Encoder board (I2C address 0x22, the TMAG5273B default). Provides 1/16-degree angle resolution (5760 counts per revolution) with velocity-sensitive scaling and multi-turn wrap-around handling.

### DistanceSensor
Driver for VL53L1X time-of-flight distance sensor. Uses the Adafruit_VL53L1X library for reliable distance measurements in the 74-1400mm range.

### SensorConstants
Centralized configuration file containing all sensor-related constants, hardware addresses, timing parameters, and calibration values.

## Public Classes and Structures

### EncoderManager Functions

#### Core Parameter Management
```cpp
void applyIncrementToParameter(EncoderBaseValues* baseValues, EncoderParameterMode param, float increment);
void updateEncoderBaseValues(UIState& uiState);
void updateEncoderStepParameterValues(UIState& uiState);
void applyEncoderBaseValues(VoiceState *voiceState, uint8_t voiceId);
void applyEncoderDelayValues();
void applyEncoderSlideTimeValues();
```

#### Parameter Range and Validation
```cpp
float getParameterMinValue(EncoderParameterMode param);
float getParameterMaxValue(EncoderParameterMode param);
float getEncoderBaseValueRange(EncoderParameterMode param);
float clampEncoderBaseValue(EncoderParameterMode param, float value);
```

#### Step Parameter Editing
```cpp
ParamId convertEncoderParameterToParamId(EncoderParameterMode encoderParam);
float getParameterMinValueForParamId(ParamId paramId);
float getParameterMaxValueForParamId(ParamId paramId);
String formatParameterValueForDisplay(ParamId paramId, float value);
```

#### System Management
```cpp
void resetEncoderBaseValues(UIState& uiState, bool currentVoiceOnly = true);
void initEncoderBaseValues();
```

### MagEncoder Methods (selected)
```cpp
class MagEncoder {
public:
    enum class Sensor { AS5600, TMAG5273 };
    explicit MagEncoder(Sensor sensor);              // Pico2Seq uses Sensor::TMAG5273
    bool begin(TwoWire &wire = Wire);                // Initialize I2C + verify/configure sensor
    void update();                                   // Throttled (5ms) sensor read
    uint16_t getRawAngle() const;                    // Native counts (0-5759 on TMAG5273)
    float getNormalizedAngle() const;                // Get normalized angle (0.0-1.0)
    int32_t getCumulativePosition() const;           // Multi-turn position with wrap-around
    float getAngularSpeed() const;                   // Filtered angular speed in °/s
    float getParameterIncrement(float minVal, float maxVal, uint8_t maxRotations = 4) const;
    float getPositionPercentage(uint8_t maxRotations = 4) const;
    bool isConnected() const;                        // Check sensor connection
    void resetCumulativePosition(int32_t position = 0);
    TMAG5273 &tmag();                                // Direct access to the TMAG5273 driver
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
    static constexpr int MAX_DISTANCE_HEIGHT_MM = 1400;
    static constexpr int MIN_DISTANCE_HEIGHT_MM = 74;
    static constexpr int INVALID_DISTANCE_MM = -1;
}
```

### Magnetic Encoder Constants
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
    static constexpr int FILTER_FREQUENCY_MIN_HZ = 150;
    static constexpr int FILTER_FREQUENCY_MAX_HZ = 8000;
}
```

## Example Usage

### Basic Sensor Initialization
```cpp
// Initialize TMAG5273 magnetic encoder (global defined in EncoderManager.cpp)
if (!magEncoder.begin()) {
    Serial.println("TMAG5273 initialization failed!");
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
    magEncoder.update();
    distanceSensor.update();

    // Update parameter values based on encoder input
    updateEncoderBaseValues(uiState);

    // Apply sensor values to synthesis parameters
    if (uiState.currentEncoderParameter <= EncoderParameterMode::Decay) {
        // Voice parameters
        applyEncoderBaseValues(voiceState, currentVoiceId);
    } else {
        // Global parameters (delay, slide)
        applyEncoderDelayValues();
        applyEncoderSlideTimeValues();
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
// Magnetic encoder
if (magEncoder.isConnected()) {
    float angle = magEncoder.getNormalizedAngle();      // 0.0 to 1.0
    float speed = magEncoder.getAngularSpeed();         // degrees per second
    int32_t position = magEncoder.getCumulativePosition(); // cumulative rotations
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
- **TMAG5273 Magnetic Encoder** (Velocity Encoder board): I2C address 0x22, 1/16-degree resolution
- **VL53L1X Distance Sensor**: I2C address 0x29, 74-1400mm range

### Software Dependencies
- **VelocityEncoder library** (`src/VelocityEncoder`, Git submodule): MagEncoder + TMAG5273 drivers
- **Adafruit_VL53L1X**: Arduino library for VL53L1X sensor
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

The magnetic encoder implements velocity-sensitive scaling so slow turns give fine control while fast turns cover range:

- **Low Speed (≤ 250°/s)**: Enhanced precision
- **Mid Speed (250-1500°/s)**: Standard curve
- **High Speed (≥ 1500°/s)**: Enhanced responsiveness for rapid control

**Dynamic Range**: 400x (0.008x to 3.2x scaling factor)

**Adaptive Filtering**: Low-pass filter coefficients adjust based on rotation speed for smooth control. Tuning constants live in `MagEncoder::Config` (minVelDps 90, maxVelDps 2400, minScale 0.008, maxScale 3.2, curveExponent 1.8, velocitySmoothing 0.08).

## "Shift and Scale" Mapping Algorithm

The system uses a "Shift and Scale" algorithm to combine encoder base values with sequencer step values:

```cpp
float shiftAndScale(float seqValue, float encoderOffset) {
    if (encoderOffset >= 0.0f) {
        // Positive offset sets minimum value
        return encoderOffset + (seqValue * (1.0f - encoderOffset));
    } else {
        // Negative offset reduces maximum value
        return seqValue * (1.0f + encoderOffset);
    }
}
```

This approach avoids dead zones by scaling the sequencer output within the dynamic range defined by the encoder offset.

## Troubleshooting

### TMAG5273 Issues
- **No Response**: Check I2C connection and address 0x22 (TMAG5273B default on the Velocity Encoder board)
- **Erratic Readings**: Verify on-axis diametric magnet placement and magnet strength
- **Slow Response**: Tune the velocity curve via `MagEncoder::Config`
- **Parameter Limits**: Verify min/max values in SensorConstants.h

### VL53L1X Issues
- **Initialization Failure**: Check I2C address 0x29 and wiring
- **Invalid Readings**: Ensure target is within 74-1400mm range
- **Stalled Reads**: Check I2C wiring, pull-ups, and the Wire timeout configuration
- **LED Interference**: Check for electrical noise from LED matrix

### System Integration Issues
- **Audio Glitches**: Ensure sensors run on Core 1 only
- **Parameter Conflicts**: Verify UI state management for voice selection
- **Memory Issues**: Check for buffer overflows in cumulative position tracking

### Performance Tuning
- **Update Frequency**: Adjust `MagEncoder::Config::readIntervalMs` for responsiveness vs. performance balance
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
- **Magnet Dependency**: The TMAG5273 requires a proper on-axis diametric magnet for accurate angle readings

### Missing Components
- **None identified**: All referenced classes and functions are implemented
- **External Libraries**: Requires Adafruit_VL53L1X library installation; VelocityEncoder is vendored as a submodule (`git submodule update --init --recursive`)

## Files
- `src/sensors/SensorConstants.h` - Centralized sensor constants
- `src/sensors/EncoderManager.h` - High-level encoder management interface
- `src/sensors/EncoderManager.cpp` - Encoder management implementation + global `magEncoder` instance
- `src/sensors/DistanceSensor.h` - Distance sensor interface
- `src/sensors/DistanceSensor.cpp` - Distance sensor implementation
- `src/VelocityEncoder/src/MagEncoder.h/.cpp` - Magnetic encoder driver library (submodule)

## Integration Checklist
- [ ] Install Adafruit_VL53L1X library
- [ ] Initialize VelocityEncoder submodule (`git submodule update --init --recursive`)
- [ ] Connect Velocity Encoder board (TMAG5273) to I2C bus (address 0x22)
- [ ] Connect VL53L1X to I2C bus (address 0x29)
- [ ] Verify on-axis diametric magnet installation on the TMAG5273
- [ ] Test sensor initialization in setup()
- [ ] Verify non-blocking updates in main loop
- [ ] Test parameter control responsiveness
- [ ] Verify Core 1 operation (not Core 0)
