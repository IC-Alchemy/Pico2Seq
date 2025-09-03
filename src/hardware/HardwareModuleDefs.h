#pragma once

#include <cstdint>

enum class ModuleType : uint8_t {
    TOUCH_MATRIX = 0,
    DISTANCE_SENSOR = 1,
    MAGNETIC_ENCODER = 2,
    OLED_DISPLAY = 3,
    LED_MATRIX = 4,
    COUNT
};

struct ModuleCapabilities {
    static constexpr uint32_t REAL_TIME_INPUT = 0x01;
    static constexpr uint32_t PARAMETER_CONTROL = 0x02;
    static constexpr uint32_t VISUAL_FEEDBACK = 0x04;
    static constexpr uint32_t STEP_SEQUENCING = 0x08;
    static constexpr uint32_t FALLBACK_AVAILABLE = 0x10;
};