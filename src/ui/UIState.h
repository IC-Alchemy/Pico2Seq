#ifndef UI_STATE_H
#define UI_STATE_H

#include <cstdint>
#include "VoiceEditControls.h"
#include "../pico2seq-core/sequencer/SequencerDefs.h" // For ParamId, EncoderParameterMode

/**
 * @brief Centralized state management for the Pico2Seq UI.
 *
 * This struct encapsulates all UI-related state variables, eliminating
 * global externs and improving modularity. An instance of this struct
 * is passed to UI functions, making data flow explicit and easier to manage.
 */
struct UIState
{
    VoiceEdit::Controls voiceEditor;
    bool controlsWaitRelease = false;
    // --- Parameter Button States ---
    // Indexed by ParamId for direct lookup.
    bool parameterButtonHeld[PARAM_ID_COUNT] = {false};

    // --- Mode States ---
    bool modGateParamSeqLengthsMode = false;
    bool slideMode = false;
    // Selected voice index 0..3; all voice-dependent UI derives from this.
    uint8_t selectedVoiceIndex = 0;
    int selectedStepForEdit = -1;
    ParamId currentEditParameter = ParamId::Count; // Parameter being edited in toggle mode (Count = none)
    int currentThemeIndex = 0;
    EncoderParameterMode currentEncoderParameter = EncoderParameterMode::Velocity;

    // --- Timing States ---
    unsigned long padPressTimestamps[SequencerConstants::MAX_STEPS_COUNT] = {0};
    // --- Transient OLED notice (replaces the old control-cluster LED flashes) ---
    enum class OledNoticeKind : uint8_t { None = 0, Randomized = 1, Saved = 2, Loaded = 3, LoadError = 4, VoiceCleared = 5, AllCleared = 6, Macro = 7, DelayMix = 8, DelayTime = 9 };
    volatile unsigned long oledNoticeUntil = 0;
    volatile OledNoticeKind oledNoticeKind = OledNoticeKind::None;
    volatile uint8_t oledNoticeVoice = 0; // 0-based voice, valid for Randomized and VoiceCleared
    volatile uint8_t macroNoticePercent = 50; // 0..100 macro position, valid for Macro
    // Numeric payload for DelayMix (wet 0-100 %) and DelayTime (milliseconds).
    volatile uint16_t oledNoticeValue = 0;
    unsigned long lastEncoderButtonPressTime = 0;
    // Until this time the OLED shows the base the encoder just changed instead
    // of the playing step's composed value (0 = not showing).
    unsigned long encoderBaseViewUntil = 0;
    // ENV mode: the envelope lane a fader last moved, highlighted on the ENV
    // page, which also outranks a toggled parameter page until envViewUntil.
    ParamId envFaderLane = ParamId::Count;
    unsigned long envViewUntil = 0;
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

    // Settings sub-modes within settingsMode
    enum class SettingsSubMode : uint8_t { PRESET_SELECTION = 0, VOICE_PARAMETER = 1 };
    SettingsSubMode currentSubMode = SettingsSubMode::PRESET_SELECTION;

    uint8_t settingsSubMenuIndex = 0; // For preset selection
    static constexpr int MAX_VOICES = 4;
    uint8_t voicePresetIndices[MAX_VOICES] = {4, 2, 1, 6}; // Default presets: Square, Bass, Digital, Percussion (indices into VoicePresets)
    unsigned long playStopPressTime = 0;
    bool playStopWasPressed = false;

    // --- Encoder Control Hold / Gate Seq Length Mode ---
    // Press/hold tracking for BUTTON_ENCODER_CONTROL to enable gate seq length mode while held
    unsigned long encoderControlPressTime = 0;
    bool encoderControlWasPressed = false;
    bool gateSeqLengthMode = false; // When true, step buttons set Gate track length (per selected voice)

    // --- Transient parameter feedback (independent of the settings page) ---
    bool voiceParameterFeedbackPending = false;
    uint8_t lastVoiceParameterButton = 255; // Raw pad index; 255 = no notice
    uint8_t voiceParameterNoticeVoice = 0;  // Snapshot so a voice switch cannot relabel it
    char voiceParameterNoticeName[24] = {};
    char voiceParameterNoticeValue[32] = {};
    unsigned long voiceParameterChangeTime = 0; // Timestamp of last voice parameter change

    bool isPresetSelection() const noexcept
    {
        return settingsMode && currentSubMode == SettingsSubMode::PRESET_SELECTION;
    }
    bool isVoiceParameterSettings() const noexcept
    {
        return settingsMode && currentSubMode == SettingsSubMode::VOICE_PARAMETER;
    }
    bool hasVoiceParameterFeedback(unsigned long now) const noexcept
    {
        return voiceParameterFeedbackPending && now - voiceParameterChangeTime < 3000;
    }

    // --- Voice Switch State ---
    bool voiceSwitchTriggered = false; // Flag to trigger immediate OLED update for voice switching

    // --- Alchemy Tile Control Surface State ---
    // Tile function set selected by the GP7 strap switch. The physical
    // ButtonModule8 carries the parameter set in Param mode and the
    // transport/utility set in Utility mode; AlchemyControlBridge owns the
    // translation. UI transitions may clear held/latch state.
    enum class AlchemyMode : uint8_t { Param = 0, Utility = 1 };
    AlchemyMode alchemyMode = AlchemyMode::Param;
    bool shiftHeld = false;       // Shift tile button level (works in both modes)
    int8_t latchedParameter = -1; // Shift-latched param (ParamId) or -1 = none
    // While set, the OLED shows the PARAM/UTIL banner after a mode flip.
    volatile unsigned long alchemyModeBannerUntil = 0;
};

#endif // UI_STATE_H
