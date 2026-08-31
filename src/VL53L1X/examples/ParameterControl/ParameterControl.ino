/*
  ParameterControl
  ----------------
  Map how far a hand is above the VL53L1X sensor onto a 0.0–1.0 parameter,
  the way the Pico2Seq sequencer drives a synthesis parameter from hand height.

  The example applies a little smoothing so the output doesn't jump on every
  sample, and prints both the raw millimeters and the mapped, smoothed value.

  Wiring:
    VL53L1X  ->  Arduino
    VCC      ->  3.3V
    GND      ->  GND
    SDA      ->  SDA
    SCL      ->  SCL
*/

#include <DistanceSensor.h>

// The useful range in medium distance mode (~74–1400 mm). Narrow this to match
// the physical reach of whatever the sensor is watching.
const int MIN_MM = 74;
const int MAX_MM = 1400;

// EMA smoothing factor: smaller = smoother but laggier.
const float SMOOTHING = 0.2f;

DistanceSensor sensor;
float smoothed = 0.0f;

void setup()
{
    Serial.begin(115200);
    while (!Serial) { delay(10); }

    if (!sensor.begin())
    {
        Serial.println("VL53L1X not found. Check wiring and address.");
        while (true) { delay(1000); }
    }
}

void loop()
{
    sensor.update();

    int mm = sensor.getRawDistanceMm();
    if (mm < 0)
    {
        // No valid reading yet (sensor still warming up or disconnected).
        return;
    }

    // Raw distance -> normalized 0.0–1.0 across [MIN_MM, MAX_MM].
    float target = constrain((mm - MIN_MM) / float(MAX_MM - MIN_MM), 0.0f, 1.0f);

    // Low-pass filter so the output doesn't jump on every reading.
    smoothed += (target - smoothed) * SMOOTHING;

    Serial.print("mm=");
    Serial.print(mm);
    Serial.print("  parameter=");
    Serial.println(smoothed, 3);

    delay(20);
}
