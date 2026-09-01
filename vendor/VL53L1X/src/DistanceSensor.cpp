#include "DistanceSensor.h"

DistanceSensor::DistanceSensor()
    : _cfg(),
      _connected(false),
      _lastMeasurementTimeMs(0),
      _currentDistanceMm(INVALID_DISTANCE_MM)
{
}

DistanceSensor::DistanceSensor(const Config &config)
    : _cfg(config),
      _connected(false),
      _lastMeasurementTimeMs(0),
      _currentDistanceMm(INVALID_DISTANCE_MM)
{
}

bool DistanceSensor::begin()
{
    return begin(Wire);
}

bool DistanceSensor::begin(TwoWire &wire)
{
    // Start the bus and let it settle before talking to the sensor.
    wire.begin();
    delay(_cfg.i2cStabilizationDelayMs);

    // Adafruit's begin() starts the bus, boots the sensor, and verifies the
    // model ID before returning.
    if (!_sensor.begin(_cfg.i2cAddress, &wire))
    {
        _connected = false;
        return false;
    }

    _connected = applyConfig();
    return _connected;
}

bool DistanceSensor::applyConfig()
{
    // The Adafruit/ST driver exposes short and long presets. Medium maps to
    // long so the existing 74-1400 mm application range is retained.
    uint16_t mode;
    switch (_cfg.distanceMode)
    {
        case DistanceMode::Short:  mode = 1; break;
        case DistanceMode::Medium: // falls through: long covers the rig range
        case DistanceMode::Long:   mode = 2; break;
        default:                   mode = 2; break;
    }
    if (_sensor.VL53L1X_SetDistanceMode(mode) != VL53L1X_ERROR_NONE)
        return false;

    // The Adafruit setter takes whole milliseconds.
    const uint16_t budgetMs = static_cast<uint16_t>(
        _cfg.timingBudgetMicros < 1000 ? 1 : _cfg.timingBudgetMicros / 1000);
    if (!_sensor.setTimingBudget(budgetMs))
        return false;

    if (_sensor.VL53L1X_SetInterMeasurementInMs(
            static_cast<uint16_t>(_cfg.interMeasurementPeriodMs)) !=
        VL53L1X_ERROR_NONE)
        return false;

    return _sensor.startRanging();
}

void DistanceSensor::update()
{
    if (!_connected)
        return;

    unsigned long currentTimeMs = millis();

    // Rate-limit updates to prevent excessive I2C communication.
    if (currentTimeMs - _lastMeasurementTimeMs < _cfg.readIntervalMs)
        return;
    _lastMeasurementTimeMs = currentTimeMs;

    // One data-ready check per update keeps this path cooperative.
    if (!_sensor.dataReady())
        return;

    const int16_t distanceMm = _sensor.distance();
    _sensor.clearInterrupt();

    if (distanceMm >= 0)
        _currentDistanceMm = distanceMm;
}

int DistanceSensor::getRawDistanceMm() const
{
    return _currentDistanceMm;
}

bool DistanceSensor::isConnected() const
{
    return _connected;
}

const DistanceSensor::Config &DistanceSensor::getConfig() const
{
    return _cfg;
}
