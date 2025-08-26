#ifndef UI_STATE_H
#define UI_STATE_H

#include <Arduino.h>
#include "../sequencer/SequencerDefs.h"
#include "../sensors/as5600.h" // For AS5600ParameterMode

/**
 * @brief Centralized state management for the PicoMudrasSequencer UI.
 *
 * This struct encapsulates all UI-related state variables, eliminating
 * global externs and improving modularity. An instance of this struct
 * is passed to UI functions, making data flow explicit and easier to manage.
 */
struct UIState
{
    // --- Parameter Button States ---
    // Indexed by ParamId for direct lookup.
    bool parameterButtonHeld[PARAM_ID_COUNT] = {false};

    // --- Mode States ---
    bool delayOn = true;
    bool modGateParamSeqLengthsMode = false;
    bool slideMode = false;
    // Selected voice index 0..3 (replaces isVoice2Mode)
    uint8_t selectedVoiceIndex = 0;
    bool isVoice2Mode = false; // Legacy flag (kept for compatibility in some code paths)
    int selectedStepForEdit = -1;
    ParamId currentEditParameter = ParamId::Count; // Parameter being edited in toggle mode (Count = none)
    int currentThemeIndex = 0;
    AS5600ParameterMode currentAS5600Parameter = AS5600ParameterMode::Velocity;

    // --- Timing States ---
    unsigned long padPressTimestamps[SequencerConstants::MAX_STEPS_COUNT] = {0};
    volatile unsigned long flash23Until = 0;
    volatile unsigned long flash25Until = 0;
    volatile unsigned long flash31Until = 0;
    unsigned long lastAS5600ButtonPressTime = 0;
    unsigned long voiceSwitchPressTime = 0;
    bool voiceSwitchWasPressed = false;

    // --- Randomize Button States ---
    static constexpr int NUM_RANDOMIZE = 4;
    unsigned long randomizePressTime[NUM_RANDOMIZE] = {0};
    bool randomizeWasPressed[NUM_RANDOMIZE] = {false};
    bool randomizeResetTriggered[NUM_RANDOMIZE] = {false};

    // --- Shuffle State ---
    uint8_t currentShufflePatternIndex = 0;

    // --- Flags ---
    // Flag to signal the LED matrix to reset step lights.
    bool resetStepsLightsFlag = false;

    // --- Debounce for Slide Mode Toggle ---
    unsigned long lastSlideModeToggleTime = 0;

    // --- Settings Mode State ---
    bool settingsMode = false;
    uint8_t settingsMenuIndex = 0;    // 0-7 for 8 menu items
    uint8_t settingsSubMenuIndex = 0; // For preset selection
    bool inPresetSelection = false;
    static constexpr int MAX_VOICES = 4;
    uint8_t voicePresetIndices[MAX_VOICES] = {4, 2, 1, 6}; // Default presets: Lead, Bass, Lead, Percussion
    // New: transient preset index while navigating before confirming apply
    uint8_t presetSelectionIndex = 0;
    unsigned long playStopPressTime = 0;
    bool playStopWasPressed = false;

    // --- AS5600 Control Hold / Gate Seq Length Mode ---
    // Press/hold tracking for BUTTON_AS5600_CONTROL to enable gate seq length mode while held
    unsigned long as5600ControlPressTime = 0;
    bool as5600ControlWasPressed = false;
    bool gateSeqLengthMode = false; // When true, step buttons set Gate track length (per selected voice)

    // --- Voice Parameter Editing State ---
    bool inVoiceParameterMode = false;
    uint8_t lastVoiceParameterButton = 0;       // Track which voice parameter was last changed
    unsigned long voiceParameterChangeTime = 0; // Timestamp of last voice parameter change

    // --- Voice Switch State ---
    bool voiceSwitchTriggered = false; // Flag to trigger immediate OLED update for voice switching
};

#endif // UI_STATE_H