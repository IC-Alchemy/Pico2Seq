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

    _sensor.initI2C(_cfg.i2cAddress, wire);

    VL53L1_Error initStatus = _sensor.initSensor();
    if (initStatus != VL53L1_ERROR_NONE)
    {
        _connected = false;
        return false;
    }

    _connected = applyConfig();
    return _connected;
}

bool DistanceSensor::applyConfig()
{
    VL53L1_Error status = VL53L1_ERROR_NONE;

    switch (_cfg.distanceMode)
    {
        case DistanceMode::Short:  status = _sensor.setDistanceMode(VL53L1_DISTANCEMODE_SHORT);  break;
        case DistanceMode::Medium: status = _sensor.setDistanceMode(VL53L1_DISTANCEMODE_MEDIUM); break;
        case DistanceMode::Long:   status = _sensor.setDistanceMode(VL53L1_DISTANCEMODE_LONG);   break;
    }
    if (status != VL53L1_ERROR_NONE)
        return false;

    status = _sensor.setMeasurementTimingBudgetMicroSeconds(_cfg.timingBudgetMicros);
    if (status != VL53L1_ERROR_NONE)
        return false;

    status = _sensor.setInterMeasurementPeriodMilliSeconds(_cfg.interMeasurementPeriodMs);
    if (status != VL53L1_ERROR_NONE)
        return false;

    status = _sensor.clearInterruptAndStartMeasurement();
    return status == VL53L1_ERROR_NONE;
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

    // Query the ready bit once. waitMeasurementDataReady() spins until a
    // range completes, which makes a nominally non-blocking control pass
    // stall for an unbounded interval.
    if (_sensor.getMeasurementDataReady() != VL53L1_ERROR_NONE ||
        !_sensor.dataReady)
        return;

    VL53L1_Error measurementStatus = _sensor.getRangingMeasurementData();
    if (measurementStatus != VL53L1_ERROR_NONE)
        return;

    // Initiate the next measurement cycle for continuous operation.
    _sensor.clearInterruptAndStartMeasurement();

    _currentDistanceMm = _sensor.measurementData.RangeMilliMeter;
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
