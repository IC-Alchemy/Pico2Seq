/*
  BasicReading
  ------------
  Print the distance measured by a VL53L1X time-of-flight sensor in
  millimeters, every 20 ms.

  Wiring:
    VL53L1X  ->  Arduino
    VCC      ->  3.3V
    GND      ->  GND
    SDA      ->  SDA
    SCL      ->  SCL
*/

#include <DistanceSensor.h>

DistanceSensor sensor;

void setup()
{
    Serial.begin(115200);
    // Wait briefly for a serial monitor to be opened.
    while (!Serial) { delay(10); }

    if (!sensor.begin())
    {
        Serial.println("VL53L1X not found. Check wiring and address.");
        while (true) { delay(1000); }
    }

    Serial.println("VL53L1X ready.");
}

void loop()
{
    sensor.update();

    if (sensor.getRawDistanceMm() >= 0)
    {
        Serial.print("Distance: ");
        Serial.print(sensor.getRawDistanceMm());
        Serial.println(" mm");
    }

    delay(20);
}
