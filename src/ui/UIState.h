#ifndef UI_STATE_H
#define UI_STATE_H

#include <Arduino.h>

// Project headers (grouped)
#include "../sequencer/SequencerDefs.h"            // ParamId, SequencerConstants
#include "../sensors/as5600.h"                    // AS5600ParameterMode
#include "../hardware/HardwareModuleDefs.h"       // ModuleType
#include "HardwareStatus.h"                       // HardwareStatus

/**
 * @file UIState.h
 * @brief Centralized UI state container for the PicoMudrasSequencer.
 *
 * The UIState struct centralizes all UI-related runtime state so it can be
 * passed explicitly to UI and handler functions, reducing globals and improving
 * readability and testability.
 */

/* Forward declarations */
class HardwareRegistry;

struct UIState
{
    // ---------------------------------------------------------------------
    // Compile-time constants
    // ---------------------------------------------------------------------
    static constexpr int NUM_RANDOMIZE = 4;
    static constexpr int MAX_VOICES = 4;

    // ---------------------------------------------------------------------
    // Parameter button states (indexed by ParamId)
    // ---------------------------------------------------------------------
    bool parameterButtonHeld[PARAM_ID_COUNT] = { false };

    // ---------------------------------------------------------------------
    // Mode / selection states
    // ---------------------------------------------------------------------
    bool delayOn = true;
    bool modGateParamSeqLengthsMode = false;
    bool slideMode = false;

    // Selected voice (0..3). legacy isVoice2Mode kept for backward compatibility.
    uint8_t selectedVoiceIndex = 0;
    bool isVoice2Mode = false;

    // Editing / UI selection
    int selectedStepForEdit = -1;
    ParamId currentEditParameter = ParamId::Count; // Count means "none"
    int currentThemeIndex = 0;
    AS5600ParameterMode currentAS5600Parameter = AS5600ParameterMode::Velocity;

    // ---------------------------------------------------------------------
    // Timing / debounce state
    // ---------------------------------------------------------------------
    unsigned long padPressTimestamps[SequencerConstants::MAX_STEPS_COUNT] = { 0 };

    // Temporary flash timers (volatile because updated from interrupts/context)
    volatile unsigned long flash23Until = 0;
    volatile unsigned long flash25Until = 0;
    volatile unsigned long flash31Until = 0;

    // Button press timestamps & edge tracking
    unsigned long lastAS5600ButtonPressTime = 0;
    unsigned long voiceSwitchPressTime = 0;
    bool voiceSwitchWasPressed = false;
    unsigned long lastSlideModeToggleTime = 0;

    // Play/stop button state
    unsigned long playStopPressTime = 0;
    bool playStopWasPressed = false;

    // ---------------------------------------------------------------------
    // Randomize / shuffle
    // ---------------------------------------------------------------------
    unsigned long randomizePressTime[NUM_RANDOMIZE] = { 0 };
    bool randomizeWasPressed[NUM_RANDOMIZE] = { false };
    bool randomizeResetTriggered[NUM_RANDOMIZE] = { false };

    uint8_t currentShufflePatternIndex = 0;

    // ---------------------------------------------------------------------
    // Settings mode
    // ---------------------------------------------------------------------
    bool settingsMode = false;

    enum class SettingsSubMode : uint8_t { PRESET_SELECTION = 0, VOICE_PARAMETER = 1 };
    SettingsSubMode currentSubMode = SettingsSubMode::PRESET_SELECTION;

    uint8_t settingsMenuIndex = 0;    // 0-7 for 8 menu items
    uint8_t settingsSubMenuIndex = 0; // For preset selection
    bool inPresetSelection = false;

    // Voice presets per voice
    uint8_t voicePresetIndices[MAX_VOICES] = { 4, 2, 1, 6 }; // Default: Lead, Bass, Lead, Percussion

    // ---------------------------------------------------------------------
    // AS5600 / gate sequence length mode (press-and-hold)
    // ---------------------------------------------------------------------
    unsigned long as5600ControlPressTime = 0;
    bool as5600ControlWasPressed = false;
    bool gateSeqLengthMode = false; // true => step buttons set gate length for selected voice

    // ---------------------------------------------------------------------
    // Voice parameter editing
    // ---------------------------------------------------------------------
    bool inVoiceParameterMode = false;
    uint8_t lastVoiceParameterButton = 0;
    unsigned long voiceParameterChangeTime = 0;

    // Immediate UI triggers
    bool voiceSwitchTriggered = false;     // Force immediate OLED update for voice switch
    bool resetStepsLightsFlag = false;     // Signal to reset LED matrix step lights

    // ---------------------------------------------------------------------
    // Hardware status & display
    // ---------------------------------------------------------------------
    HardwareStatus hardwareStatus;
    bool showHardwareStatus = false;
    unsigned long hardwareStatusToggleTime = 0; // debounce for toggling status display

    // ---------------------------------------------------------------------
    // Methods
    // ---------------------------------------------------------------------
    /**
     * Update hardwareStatus from the provided HardwareRegistry.
     * Implementation lives in the corresponding .cpp file.
     */
    void updateHardwareStatus(const HardwareRegistry& registry);
};

#endif // UI_STATE_H