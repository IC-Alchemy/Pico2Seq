#ifndef DistanceMap_h
#define DistanceMap_h

#include <cmath>
#include <cstdint>

/**
 * DistanceMap
 * -----------
 * Maps a VL53L1X millimetre reading onto a normalized 0.0–1.0 parameter, with
 * the clamp + EMA smoothing the ParameterControl example does inline in its
 * loop. Factored out so an engine and the host test suite can share one
 * definition of "hand height becomes a parameter value".
 *
 * Arduino-free on purpose: plain C++17, no Wire, no Arduino.h, so it compiles
 * in the host doctest suite unchanged.
 */
class DistanceMap
{
public:
    struct Config
    {
        int minMm = 74;     // useful range in medium distance mode
        int maxMm = 1400;
        float smoothing = 0.2f;  // EMA factor per update; smaller = smoother, laggier
        bool invert = false;     // false: farther = 1.0; true: closer = 1.0
    };

    DistanceMap() = default;
    explicit DistanceMap(const Config &config) : _cfg(config) {}

    /**
     * Feed one raw reading and get the smoothed parameter value. Invalid
     * readings (negative mm) are ignored and leave the smoother untouched, so
     * a sensor dropout holds the last value instead of snapping to zero.
     */
    float update(int mm)
    {
        if (mm >= 0)
        {
            const float span = static_cast<float>(_cfg.maxMm - _cfg.minMm);
            float target = (static_cast<float>(mm) - static_cast<float>(_cfg.minMm)) / span;
            if (target < 0.0f) target = 0.0f;
            if (target > 1.0f) target = 1.0f;
            if (_cfg.invert) target = 1.0f - target;

            if (!_valid)
            {
                _value = target;  // first reading: no ramp from an arbitrary zero
                _valid = true;
            }
            else
            {
                _value += (target - _value) * _cfg.smoothing;
            }
        }
        return _value;
    }

    /** Last smoothed value without feeding a new reading. */
    float value() const { return _value; }

    /** False until the first valid reading has been fed in. */
    bool valid() const { return _valid; }

    const Config &config() const { return _cfg; }

private:
    Config _cfg;
    float _value = 0.0f;
    bool _valid = false;
};

#endif  // DistanceMap_h
