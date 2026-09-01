#ifndef DistanceSensor_h
#define DistanceSensor_h

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_VL53L1X.h>

/**
 * DistanceSensor
 * --------------
 * Arduino driver for the ST VL53L1X time-of-flight distance sensor, with
 * non-blocking updates and tunable timing built in.
 *
 * Wraps the Adafruit VL53L1X I2C driver and provides:
 *   - Continuous-mode distance readings in millimeters.
 *   - A non-blocking update() with a configurable polling interval and one
 *     data-ready check per call, so an incomplete measurement never stalls the
 *     loop it runs in.
 *   - Connection detection, so a sketch can degrade gracefully when no sensor
 *     is fitted.
 *
 * Every number that shapes the measurement cadence is exposed in Config, so the
 * library carries no dependency on any particular application. It was factored
 * out of a polyphonic step sequencer, where it maps hand height over the sensor
 * onto a synthesis parameter — but it is a plain distance source.
 *
 * Wire must be available on the target board; begin() calls Wire.begin() unless
 * you pass an explicit bus. Builds on any Arduino core that provides a working
 * TwoWire implementation.
 */
class DistanceSensor
{
public:
    /** Distance ranging preset. Medium uses the Adafruit/ST long preset. */
    enum class DistanceMode
    {
        Short,  // ~1.3 m, better ambient-light immunity, faster
        Medium, // uses the long preset (~4 m), balanced for this application
        Long    // ~4 m, maximum range, slower
    };

    /**
     * Tunable timing and bus parameters.
     *
     * The defaults are the values used in real-time parameter-control duty on a
     * Raspberry Pi Pico 2: a 20 ms timing budget with a 24 ms inter-measurement
     * period, polled every 20 ms. The measurementTimeoutMs field is retained
     * for source compatibility but is not used by the non-blocking Adafruit
     * data-ready path. Override any other field in the constructor to retune
     * without rewriting the driver.
     */
    struct Config
    {
        uint8_t       i2cAddress                = 0x29;   // VL53L1X default I2C address
        DistanceMode  distanceMode              = DistanceMode::Medium;
        uint32_t      timingBudgetMicros        = 20000;  // Measurement accuracy vs. speed
        uint32_t      interMeasurementPeriodMs  = 24;     // Continuous-mode period
        uint32_t      readIntervalMs            = 20;     // Minimum ms between update() reads
        uint32_t      measurementTimeoutMs      = 5;      // Compatibility field; not used
        uint32_t      i2cStabilizationDelayMs   = 50;     // Settle delay after Wire.begin()
    };

    /** Construct a sensor with default timing. */
    DistanceSensor();

    /** Construct a sensor with custom timing. */
    explicit DistanceSensor(const Config &config);

    /**
     * Initialize the default Wire bus and the sensor. Configures continuous
     * measurement mode and starts the first reading. Returns true on success,
     * false on any hardware or configuration error (also reachable via
     * isConnected() afterwards).
     */
    bool begin();

    /**
     * Initialize an explicit I2C bus (e.g. Wire1) and the sensor. The bus is
     * started with Wire.begin() internally. Useful on boards that expose more
     * than one I2C peripheral.
     */
    bool begin(TwoWire &wire);

    /**
     * Poll the sensor for a fresh distance reading. Non-blocking: reads are
     * rate-limited to readIntervalMs and capped by measurementTimeoutMs, so a
     * call returns promptly even when no sample is ready. Call this regularly
     * from loop().
     */
    void update();

    /**
     * Most recent distance in millimeters. Returns the last valid reading even
     * if the sensor later drops off the bus. Typical useful range is ~74–1400 mm
     * in medium mode; the sensor reports the value it measured.
     */
    int getRawDistanceMm() const;

    /** True if the sensor was detected and configured at the last begin(). */
    bool isConnected() const;

    /** Access the active configuration. */
    const Config &getConfig() const;

private:
    Config _cfg;

    // Hardware interface
    Adafruit_VL53L1X _sensor;
    bool             _connected;

    // Timing control for non-blocking updates
    unsigned long _lastMeasurementTimeMs;

    // Current distance measurement in millimeters
    int _currentDistanceMm;

    // Sentinel returned before the first successful reading.
    static constexpr int INVALID_DISTANCE_MM = -1;

    // Applies _cfg to the sensor over the already-started bus. Returns false on
    // any configuration error.
    bool applyConfig();
};

#endif // DistanceSensor_h
