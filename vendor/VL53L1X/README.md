# VL53L1X

A hand in the air is still a distance reading. This is the Arduino driver we
ship with our **Pico2Seq** step sequencer — built around the **ST VL53L1X**
time-of-flight sensor, and tuned for instruments you play rather than things
you program.

It does the basics you would expect from a VL53L1X library, and one thing you
probably would not: its reads never block. A sensor read that takes too long is
cut off, not waited for. That is the whole reason this driver exists.

## Features

- **Continuous-mode distance** in millimeters, polled on your schedule.
- **Non-blocking updates** with a configurable polling interval and a hard
  timeout, so a stalled I2C read never stalls the loop it runs in. Built for the
  side of an embedded system that is not allowed to hang.
- **Short / medium / long distance presets**, mapping to the sensor's native
  distance modes.
- **Fully configurable timing** — timing budget, inter-measurement period, and
  polling cadence — via `DistanceSensor::Config`.
- **Connection detection**, so a sketch can degrade gracefully when no sensor is
  fitted.
- **Configurable I2C bus** (`begin(Wire1)`), for boards with more than one
  peripheral.

## Hardware

| VL53L1X pin | Connect to |
|---|---|
| VCC | 3.3V (check your breakout's regulator) |
| GND | GND |
| SDA | board SDA |
| SCL | board SCL |

The VL53L1X's default I2C address is **0x29**. Pass a different value in the
`Config` if your breakout has been readdressed.

## Installation

This driver depends on the **Melopero VL53L1X** library, which wraps the ST
ultra-lite driver. Install it first from the Arduino IDE Library Manager
(search for "Melopero VL53L1X"); it is declared in `library.properties`, so the
IDE will offer to install it automatically when you add this library.

Then add this folder via **Sketch → Include Library → Add .ZIP Library…** on a
zip of the folder, or drop the `VL53L1X/` folder into your `libraries/`
directory. The library is architecture-independent (`architectures=*`) and
builds on any core that provides a working `TwoWire`.

## Quick start

```cpp
#include <DistanceSensor.h>

DistanceSensor sensor;

void setup()
{
    Serial.begin(115200);
    if (!sensor.begin())
    {
        Serial.println("VL53L1X not found.");
        while (true) { delay(1000); }
    }
}

void loop()
{
    sensor.update();
    Serial.println(sensor.getRawDistanceMm());
    delay(20);
}
```

## Real-time parameter control

The headline use case: map how far a hand is above the sensor onto a parameter
range. `update()` is cheap and non-blocking, so it is safe to call every loop
iteration alongside other work.

```cpp
DistanceSensor sensor;

// Map the 74–1400 mm medium-mode range onto a 0.0–1.0 parameter.
float mapDistanceToParameter(int minMm, int maxMm)
{
    int mm = sensor.getRawDistanceMm();
    float t = constrain((mm - minMm) / float(maxMm - minMm), 0.0f, 1.0f);
    return t;
}

void loop()
{
    sensor.update();
    float parameter = mapDistanceToParameter(74, 1400);
    // ...apply `parameter` to a synth, a servo, a UI...
}
```

The timing numbers that shape the measurement cadence are tunable via
`DistanceSensor::Config`:

```cpp
DistanceSensor::Config cfg;
cfg.distanceMode             = DistanceMode::Long;  // ~4 m max range
cfg.timingBudgetMicros       = 33000;               // longer budget, more range
cfg.interMeasurementPeriodMs = 40;                  // continuous-mode period
cfg.readIntervalMs           = 40;                  // poll cadence
cfg.measurementTimeoutMs     = 8;                   // hard cap on waiting

DistanceSensor sensor(cfg);
```

These defaults are the cadence we use in our own instruments. They are a
starting point, not a verdict — every sensor lives in a different enclosure,
and every loop has a different budget.

## API reference

### `DistanceSensor::DistanceMode`

| Value | Notes |
|---|---|
| `Short` | ~1.3 m, better ambient-light immunity, faster |
| `Medium` | ~3 m, balanced (default) |
| `Long` | ~4 m, maximum range, slower |

### `DistanceSensor::Config`

A struct of tunables with sensible defaults. See the snippet above.

### Setup / polling

| Method | Description |
|---|---|
| `bool begin()` | Initialize the default `Wire` bus and the sensor. Returns false if not present or configuration failed. |
| `bool begin(TwoWire &wire)` | Same, on an explicit bus (e.g. `Wire1`). |
| `void update()` | Poll for a fresh reading, rate-limited and timeout-capped. Call every loop. |
| `bool isConnected()` | Returns whether `begin()` configured the sensor successfully. |

### Read-only state

| Method | Description |
|---|---|
| `int getRawDistanceMm()` | Most recent distance in millimeters (last valid reading on sensor loss). |
| `const Config &getConfig()` | The active configuration. |

## Examples

- **BasicReading** — print the distance in millimeters every 20 ms.
- **ParameterControl** — map hand height over the sensor onto a 0.0–1.0
  parameter range, the way the sequencer does.

## License

MIT, see [LICENSE](LICENSE).
