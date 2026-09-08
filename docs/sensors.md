# Sensors Module

## Overview

The sensors module provides real-time physical input and parameter modulation for the Pico2Seq synthesizer using three primary hardware input systems:

1. **TMAG5273 Magnetic Velocity Encoder** (Velocity Encoder board): High-precision rotary input with velocity-sensitive acceleration for parameter adjustment and step editing.
2. **VL53L1X Distance Sensor**: Time-of-Flight (ToF) infrared optical distance measurement for hands-free parameter modulation and real-time step recording.
3. **MPR121 Capacitive Touch Matrix**: 32-pad capacitive touch grid configured as two 16-step sequencer voice banks for step programming, voice selection, and note input.

All sensor acquisition and processing runs exclusively on **Core 0** inside a 1 ms non-blocking control slice (`ControlIO::scanControls()`, called from `Application::update()`), ensuring zero interference or jitter with Core 1 real-time I2S audio synthesis (48 kHz stereo).

---

## Hardware Bus Architecture and Pinouts

The Pico2Seq hardware separates sensors, displays, and control surfaces across two independent hardware I2C peripheral blocks on the RP2350 microcontroller:

| Bus | RP2350 Pins | Clock Speed | Connected Devices | I2C Addresses | Purpose |
|---|---|---|---|---|---|
| **Wire** (I2C0) | GP4 (SDA)<br>GP5 (SCL) | 100 kHz (Standard)<sup>1</sup> | TMAG5273A Magnetic Encoder<br>VL53L1X Distance Sensor<br>MPR121 Touch Matrix<br>SH1106 OLED Display | `0x35` (`TMAG5273::ADDRESS_A`)<br>`0x29` (VL53L1X)<br>`0x5A` (MPR121)<br>`0x3C` (OLED) | Primary sensor acquisition & display bus |

<sup>1</sup> `Wire` itself has no `setClock()` call by default (arduino-pico default). Setting `PICO2SEQ_I2C_FASTMODE=1` (opt-in, default OFF — the 2026-09-07 bench run showed OLED glitches/freezes at 400 kHz on this rig) raises the whole bus to 400 kHz via `Wire.setClock()` in `ControlIO::beginMainBusAndLeds()`. The OLED library's own frame pushes always run at 400 kHz regardless (its `preclk` constructor argument, `src/OLED/oled.cpp`); see `docs/oled.md`.
| **Wire1** (I2C1) | GP14 (SDA)<br>GP15 (SCL) | 100 kHz (Standard Mode) | Alchemy Modular UI Tiles:<br>- SliderModule (Slot 0)<br>- ButtonModule8 (Slot 1) | `0x08` (SliderModule)<br>`0x0B` (ButtonModule8) | Dedicated control surface tile bus (400 kHz stalls tile transfers on this rig) |
| **GPIO** | GP7 (Input Pullup) | N/A | Hardware Mode Strap Switch | N/A | Selects Param Mode (LOW) vs Utility Mode (HIGH) for Alchemy tiles |

### Interrupt & Hardware Pin Map

- `PIN_WIRE_SDA` = `4` (GP4)
- `PIN_WIRE_SCL` = `5` (GP5)
- `PIN_ALCHEMY_WIRE1_SDA` = `14` (GP14)
- `PIN_ALCHEMY_WIRE1_SCL` = `15` (GP15)
- `PIN_ALCHEMY_MODE_SWITCH` = `7` (GP7, `INPUT_PULLUP`)
- Legacy touch-IRQ pin: **removed**. GP10–GP12 are now owned by the I2S audio output (BCLK/LRCK/DATA).

---

## Real-Time Scheduling and Update Rates

The firmware implements a strict dual-core separation:

```
Core 1 (Real-Time Audio):
  AudioEngine::renderNextBuffer() @ 48 kHz stereo I2S (GP10 BCLK, GP11 LRCK, GP12 DATA)
  -> 0% I2C / sensor involvement -> Never blocks, no dynamic allocations

Core 0 (UI, Sensors, Matrix, MIDI):
  Application::update()  (Core 0 loop(), src/app/Application.cpp):
    ControlIO::pollHeldButtons()  -> Promotes long-press states (randomize reset, gate seq length)
    processClockEvents()          -> Drains uClock ISR-staged step ticks (SpscQueue)
    ControlIO::scanControls(nowMs)   (1 ms tick, kControlIntervalMs):
      +-- Matrix_scan()          -> 1 ms call, self-throttled to 4 ms in Matrix.cpp
      +-- alchemyBridge.update() -> 1 ms Alchemy tile polling (Wire1 @ 100 kHz)
      +-- magEncoder.update()    -> 1 ms poll (5 ms internal throttle in driver)
      +-- updateEncoderBaseValues()-> Applies rotary increments to active params
      +-- distanceSensor.update()-> 1 ms poll (20 ms non-blocking read interval)
      +-- performanceInput.observeDistance(rawMm) -> AppState::PerformanceInput
      +-- updateParametersForStep()-> Live recording into selected step
    ControlIO::refreshDisplays(nowMs) (20 ms tick, kDisplayIntervalMs):
      +-- updateStepLEDs() / ledMatrix.show()
      +-- display.update() (SH1106 OLED @ 0x3C, commitFrame-gated)
```

---

## Key Components

### 1. TMAG5273 Magnetic Velocity Encoder

The magnetic encoder subsystem consists of two architectural layers:

- **`MagEncoder` (`src/VelocityEncoder/src/MagEncoder.h/.cpp`)**: Portable, low-level C++ driver wrapping TI TMAG5273 (and legacy AS5600) behind a unified interface. Pico2Seq configures `MagEncoder::Sensor::TMAG5273` with `TMAG5273::ADDRESS_A` on Wire at address `0x35`. Features include:
  - 1/16-degree angular resolution (5760 counts per revolution).
  - Multi-turn cumulative position tracking with wrap-around handling.
  - Dynamic velocity-sensitive acceleration (400x dynamic range: 0.008x to 3.2x scaling factor).
  - Adaptive low-pass speed filtering.
- **`EncoderManager` (`src/sensors/EncoderManager.h/.cpp`)**: High-level parameter management subsystem bridging encoder delta increments to the synthesizer data model. Handles:
  - Per-voice base parameter updates across all four voices (`applyEncoderBaseValues`, one base-value set per voice).
  - Global effects modulation (`applyEncoderDelayValues`, `applyEncoderSlideTimeValues`).
  - Individual step parameter editing (`updateEncoderStepParameterValues`).
  - Bidirectional "Shift and Scale" mapping.
  - Dynamic boundary proximity flash zones (`FlashSpeedZone`).

### 2. VL53L1X Distance Sensor

- **`DistanceSensor` (`src/sensors/DistanceSensor.h/.cpp`)**: Non-blocking driver wrapping `Adafruit_VL53L1X` on Wire at address `0x29`.
  - Continuous measurement mode with medium distance mode.
  - 20 ms timing budget (`TIMING_BUDGET_MICROSECONDS = 20000`).
  - 24 ms inter-measurement period (`INTER_MEASUREMENT_PERIOD_MS = 24`).
  - 20 ms update polling interval (`READ_INTERVAL_MS = 20`).
  - Operational measurement range: 74 mm to 1400 mm (`MIN_DISTANCE_HEIGHT_MM` to `MAX_DISTANCE_HEIGHT_MM`).
  - Hand-distance calibration lives in `AppState::PerformanceInput` (`src/app/AppState.h`): `observeDistance(rawMm)` stores `distanceAboveMinimumMm = raw − 74` when raw is inside 74–1400 mm, else 0 (so 0–1326 mm above minimum). For step recording, `recordingValue()` normalizes by dividing by `MAX_DISTANCE_HEIGHT_MM` (1400) — **not** by (max − min) — and clamps to 0..1; this is deliberate instrument calibration, host-tested in `tests/unit/test_app_runtime.cpp` (`[recording]`).
  - Non-blocking single-poll guarantee: `update()` checks `dataReady()` once and returns immediately without stalling the control loop.

### 3. MPR121 Capacitive Touch Matrix

- **`Matrix` (`src/matrix/Matrix.h/.cpp`)**: 32-electrode capacitive touch matrix driver using `Adafruit_MPR121` on Wire at address `0x5A`.
  - Autoconfig enabled with conservative touch/release thresholds: `touchSensor.setThresholds(55, 22)` (in `ControlIO::beginTouchPads()`).
  - Called every 1 ms from `ControlIO::scanControls()`, but self-throttled to 4 ms internally (`SCAN_INTERVAL_MS` — `touched()` is an I2C register read and the MPR121's internal electrode refresh is slower than ~1 kHz).
  - Drives 32 dedicated step pads across two 16-step voice banks resolved via `ControlSurface::PadBank::resolve(buttonIndex, selectedVoiceIndex)`:
    - Indices 0–15: Voice A steps 0–15.
    - Indices 16–31: Voice B steps 0–15.
  - Dispatches `MatrixButtonEvent` (pressed/released) to `matrixEventHandler()`.

### 4. SensorConstants

- **`SensorConstants.h` (`src/sensors/SensorConstants.h`)**: Centralized configuration header providing strongly typed `constexpr` constants across namespaces `DistanceSensor`, `MagneticEncoder`, and `System`.

---

## Public Classes and API Reference

### EncoderManager API (`src/sensors/EncoderManager.h`)

#### Parameter Modification & Application
```cpp
void applyIncrementToParameter(EncoderBaseValues* baseValues, EncoderParameterMode param, float increment);
void updateEncoderBaseValues(UIState& uiState);
void updateEncoderStepParameterValues(UIState& uiState);
void applyEncoderBaseValues(VoiceState *voiceState, uint8_t voiceId);
void applyEncoderDelayValues();
void applyEncoderSlideTimeValues();
```

#### Range Validation & Clamping
```cpp
float getParameterMinValue(EncoderParameterMode param);
float getParameterMaxValue(EncoderParameterMode param);
float getEncoderBaseValueRange(EncoderParameterMode param);
float clampEncoderBaseValue(EncoderParameterMode param, float value);
```

#### Step Parameter Conversions & Formatting
```cpp
ParamId convertEncoderParameterToParamId(EncoderParameterMode encoderParam);
float getParameterMinValueForParamId(ParamId paramId);
float getParameterMaxValueForParamId(ParamId paramId);
String formatParameterValueForDisplay(ParamId paramId, float value);
```

#### Lifecycle & State Initialization
```cpp
void resetEncoderBaseValues(UIState& uiState, bool currentVoiceOnly = true);
void initEncoderBaseValues();

extern MagEncoder magEncoder;
extern float delayTarget;
extern float feedbackAmmount;
extern const size_t MAX_DELAY_SAMPLES;
```

---

### MagEncoder API (`src/VelocityEncoder/src/MagEncoder.h`)

```cpp
class MagEncoder {
public:
    enum class Sensor { AS5600, TMAG5273 };
    MagEncoder();
explicit MagEncoder(Sensor sensor);
explicit MagEncoder(const Config &config); // i2cAddress and response tuning via Config
    bool begin(TwoWire &wire = Wire);                // Initialize I2C and verify sensor identity
    void update();                                   // Throttled sensor read (5ms interval)
    uint16_t getRawAngle() const;                    // Native counts (0-5759 on TMAG5273)
    float getNormalizedAngle() const;                // Normalized position [0.0, 1.0]
    int32_t getCumulativePosition() const;           // Multi-turn position with wrap tracking
    float getAngularSpeed() const;                   // Filtered angular speed in °/s
    float getParameterIncrement(float minVal, float maxVal, uint8_t maxRotations = 4) const;
    float getPositionPercentage(uint8_t maxRotations = 4) const;
    bool isConnected() const;                        // Sensor health check
    void resetCumulativePosition(int32_t position = 0);
    TMAG5273 &tmag();                                // Direct access to underlying TMAG5273 driver
};
```

---

### DistanceSensor API (`src/sensors/DistanceSensor.h`)

```cpp
class DistanceSensor {
public:
    DistanceSensor();
    bool begin();                    // Configures Adafruit_VL53L1X on Wire @ 0x29
    void update();                   // Non-blocking single data-ready poll (20ms interval)
    int getRawDistanceMm() const;    // Returns raw measurement in mm (74-1400mm)
    bool isConnected() const;        // Connection status flag
};

extern DistanceSensor distanceSensor;
void updateDistanceSensor();         // Legacy helper
```

---

### Matrix API (`src/matrix/Matrix.h`)

```cpp
#define MATRIX_BUTTON_COUNT 32

typedef enum {
    MATRIX_BUTTON_PRESSED,
    MATRIX_BUTTON_RELEASED
} MatrixButtonEventType;

typedef struct {
    uint8_t buttonIndex;
    MatrixButtonEventType type;
} MatrixButtonEvent;

void Matrix_init(Adafruit_MPR121 *sensor);
void Matrix_scan();
bool Matrix_getButtonState(uint8_t idx);
void Matrix_setEventHandler(void (*handler)(const MatrixButtonEvent &));
void Matrix_setRisingEdgeHandler(void (*handler)(uint8_t buttonIndex));
void Matrix_printState();
```

---

## Configuration Constants (`SensorConstants.h`)

### VL53L1X Constants (`SensorConstants::DistanceSensor`)
```cpp
static constexpr uint8_t I2C_ADDRESS = 0x29;
static constexpr uint8_t I2C_STABILIZATION_DELAY_MS = 50;
static constexpr unsigned long READ_INTERVAL_MS = 20;
static constexpr unsigned long TIMING_BUDGET_MICROSECONDS = 20000;
static constexpr unsigned long INTER_MEASUREMENT_PERIOD_MS = 24;
static constexpr int MAX_DISTANCE_HEIGHT_MM = 1400;
static constexpr int MIN_DISTANCE_HEIGHT_MM = 74;
static constexpr int INVALID_DISTANCE_MM = -1;
```

### Magnetic Encoder Constants (`SensorConstants::MagneticEncoder`)
```cpp
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

// Defaults
static constexpr float DEFAULT_DELAY_TIME_SAMPLES = 48000.0f * 0.2f; // 200ms
static constexpr float DEFAULT_DELAY_FEEDBACK = 0.55f;
static constexpr float DEFAULT_VOICE_PARAMETER = 0.0f;
```

### System Constants (`SensorConstants::System`)
```cpp
static constexpr float SAMPLE_RATE_HZ = 48000.0f;
static constexpr uint8_t MAX_VOICES = 4;
static constexpr unsigned long SENSOR_UPDATE_INTERVAL_MS = 1;
static constexpr int FILTER_FREQUENCY_MIN_HZ = 150;
static constexpr int FILTER_FREQUENCY_MAX_HZ = 8000;
```

---

## Velocity-Sensitive Rotary Scaling

The magnetic encoder driver applies nonlinear velocity scaling so slow rotations afford fine single-step tuning while swift turns traverse entire parameter sweeps:

- **Low Speed (≤ 250°/s)**: Sub-unity scaling (down to 0.008x) for ultra-fine micro-adjustments.
- **Mid Speed (250–1500°/s)**: Standard proportional curve (1.0x baseline).
- **High Speed (≥ 1500°/s)**: Accelerated curve (up to 3.2x) for rapid value transitions.

Tuning parameters configured in `MagEncoder::Config`:
- `minVelDps` = 90.0°/s
- `maxVelDps` = 2400.0°/s
- `minScale` = 0.008f
- `maxScale` = 3.2f
- `curveExponent` = 1.8f
- `velocitySmoothing` = 0.08f

---

## "Shift and Scale" Parameter Mapping

To combine encoder base offsets with dynamic sequencer step tracks without clipping or introducing dead zones, `applyEncoderBaseValues` implements bidirectional "Shift and Scale":

```cpp
float shiftAndScale(float seqValue, float encoderOffset) {
    if (encoderOffset >= 0.0f) {
        // Positive offset elevates minimum floor; scales remaining upper span
        return encoderOffset + (seqValue * (1.0f - encoderOffset));
    } else {
        // Negative offset compresses upper ceiling; preserves minimum floor
        return seqValue * (1.0f + encoderOffset);
    }
}
```

---

## Example Initialization and Control Loop

The firmware's boot and loop are split across `src/app/` (PR #17 refactor);
`Pico2Seq.ino` is a thin shell that calls `Application::begin()` /
`Application::update()` on Core 0. The sensor-relevant slice:

```cpp
// Boot — Application::begin() (Core 0), in order:
ControlIO::beginMainBusAndLeds();    // Wire.setSDA(4)/setSCL(5)/begin(),
                                     // optional 400 kHz via PICO2SEQ_I2C_FASTMODE,
                                     // FreezeWatchdog arm, LED matrix begin(100)
ControlIO::beginPerformanceSensors();// distanceSensor.begin() [VL53L1X @ 0x29],
                                     // magEncoder.begin() [TMAG5273A @ 0x35],
                                     // initEncoderBaseValues()
ControlIO::beginTouchPads();         // MPR121 @ 0x5A, autoconfig, thresholds 55/22
ControlIO::beginMatrixAndTiles();    // Matrix_init, event handler, Wire1 tiles @ 100 kHz

// Runtime — Application::update() (Core 0), every pass:
ControlIO::pollHeldButtons();        // long-press promotion
processClockEvents();                // drains uClock ISR step queue
ControlIO::scanControls(nowMs);      // 1 ms tick:
                                     //   Matrix_scan() (4 ms self-throttle)
                                     //   alchemyBridge.update(nowMs, ...)
                                     //   magEncoder.update(); updateEncoderBaseValues(uiState)
                                     //   distanceSensor.update();
                                     //   AppState::performanceInput.observeDistance(
                                     //       distanceSensor.getRawDistanceMm());
                                     //   if (uiState.selectedStepForEdit != -1)
                                     //       updateParametersForStep(uiState.selectedStepForEdit);
ControlIO::refreshDisplays(nowMs);   // 20 ms tick: updateStepLEDs + display.update()
```

Live parameter recording flows through `AppState::PerformanceInput`
(`src/app/AppState.h`): `observeDistance()` tracks the hand height above the
74 mm minimum, and `recordingValue()` (division by the 1400 mm maximum,
clamped 0..1) feeds `updateParametersForStep()` in `src/app/StepPlayback.cpp`
when a parameter button is held and a step is selected for editing.

---

## Troubleshooting Guide

### TMAG5273 Magnetic Encoder
- **Device Not Detected (`0x35`)**: Check Wire connections (GP4/GP5), 3.3V power, and verify the Velocity Encoder board I2C pull-ups. Pico2Seq is configured for a TMAG5273A; use the matching `TMAG5273::ADDRESS_*` constant if a different factory-programmed part is fitted.
- **Erratic Angle Readings**: Confirm an on-axis diametric magnet is centered directly over the TMAG5273 package with ~1–3mm air gap.
- **Sluggish Response**: Check if `magEncoder.update()` is called regularly every 1 ms (it self-throttles reads to a 5 ms minimum interval).

### VL53L1X Distance Sensor
- **Initialization Fails (`0x29`)**: Verify I2C bus address and 50 ms stabilization delay (`I2C_STABILIZATION_DELAY_MS`).
- **Reading Stalls at -1**: Target out of range (< 74 mm or > 1400 mm) or optical cover glass is occluded.
- **Jitter or False Triggers**: Optical noise from high-brightness WS2812B LEDs or ambient infrared sunlight.

### MPR121 Capacitive Touch Matrix
- **Matrix Unresponsive (`0x5A`)**: Check wiring to MPR121 breakout and confirm address jumper is pulled to GND (`0x5A`).
- **Stuck Touches**: Verify `touchSensor.setThresholds(55, 22)` is applied and autoconfig is enabled. Avoid grounding pads during boot.

---

## Related Documentation
- `docs/ButtonHandlers.md`: Alchemy tile control surface and MPR121 dual-surface architecture.
- `docs/matrix.md`: MPR121 32-pad touch grid layout and bank resolution.
- `docs/architecture.md`: Dual-core audio/UI separation architecture.
- `docs/alchemyui-tmag5273-migration.md`: Historical migration log for TMAG5273 and AlchemyUI.
