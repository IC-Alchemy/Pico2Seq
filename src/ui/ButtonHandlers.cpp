#include "ButtonHandlers.h"
#include "UIConstants.h"
#include "UIState.h"
#include "ButtonManager.h"
#include "../sequencer/Sequencer.h"
#include "../scales/scales.h"
#include "../sequencer/ShuffleTemplates.h"
#include "../voice/VoiceManager.h"
#include "../LEDMatrix/LEDMatrixFeedback.h"
#include <uClock.h>

// External flags and helpers used by UI
extern bool isClockRunning;
extern Sequencer seq1, seq2, seq3, seq4;
extern void onClockStart();
extern void onClockStop();
extern uint8_t currentScale;
extern std::unique_ptr<VoiceManager> voiceManager;
extern uint8_t voice1Id;
extern uint8_t voice2Id;
extern uint8_t voice3Id;
extern uint8_t voice4Id;

// Begin tracking a randomize press for a voice index [0..3]
void beginRandomizePress(int voiceIndex, UIState& state) {
    if (voiceIndex < 0 || voiceIndex >= UIState::NUM_RANDOMIZE) return;
    state.randomizePressTime[voiceIndex] = millis();
    state.randomizeWasPressed[voiceIndex] = true;
}

// End tracking for a randomize press and clear flags
void endRandomizePress(int voiceIndex, UIState& state) {
    if (voiceIndex < 0 || voiceIndex >= UIState::NUM_RANDOMIZE) return;
    state.randomizeWasPressed[voiceIndex] = false;
    state.randomizeResetTriggered[voiceIndex] = false;
}

// Handle randomize button behavior for a single voice
void handleRandomizeButton(int voiceIndex, UIState& state) {
    if (voiceIndex < 0 || voiceIndex >= UIState::NUM_RANDOMIZE) return;

    // Get the appropriate sequencer
    Sequencer* seq = nullptr;
    switch(voiceIndex) {
        case 0: seq = &seq1; break;
        case 1: seq = &seq2; break;
        case 2: seq = &seq3; break;
        case 3: seq = &seq4; break;
        default: return;
    }

    // Calculate press duration and branch accordingly
    unsigned long heldTime = millis() - state.randomizePressTime[voiceIndex];
    if (!isLongPress(heldTime)) {
        // Short press: randomize parameters
        seq->randomizeParameters();
        Serial.print("Seq ");
        Serial.print(voiceIndex + 1);
        Serial.println(" randomized by short press");
    }

    // Reset state and UI flashes common to all randomize buttons
    endRandomizePress(voiceIndex, state);
    state.selectedStepForEdit = -1;
    state.flash31Until = millis() + CONTROL_LED_FLASH_DURATION_MS;
}

// Helper to cycle AS5600 parameter selection and report
static void cycleAS5600Parameter(UIState &uiState) {
    uiState.currentAS5600Parameter = static_cast<AS5600ParameterMode>(
        (static_cast<uint8_t>(uiState.currentAS5600Parameter) + 1) %
        static_cast<uint8_t>(AS5600ParameterMode::COUNT));

    uiState.lastAS5600ButtonPressTime = millis();

    Serial.print("AS5600 parameter switched to: ");
    switch (uiState.currentAS5600Parameter) {
    case AS5600ParameterMode::Note:
        Serial.println("Note");
        break;
    case AS5600ParameterMode::Velocity:
        Serial.println("Velocity");
        break;
    case AS5600ParameterMode::Filter:
        Serial.println("Filter");
        break;
    case AS5600ParameterMode::Attack:
        Serial.println("Attack");
        break;
    case AS5600ParameterMode::Decay:
        Serial.println("Decay");
        break;
    case AS5600ParameterMode::DelayTime:
        Serial.println("Delay Time");
        break;
    case AS5600ParameterMode::DelayFeedback:
        Serial.println("Delay Feedback");
        break;
    case AS5600ParameterMode::SlideTime:
        Serial.println("Slide Time");
        break;
    case AS5600ParameterMode::COUNT:
        break; // Should not happen
    }
}

// Handle parameter button for a specific voice and parameter index
// paramIndex is the matrix button index (e.g., 9..24) for voice parameter toggles
void handleVoiceParameterButton(int voiceIndex, int paramIndex, UIState& state) {
    if (!voiceManager) return;
    if (voiceIndex < 0 || voiceIndex > 3) return;

    uint8_t currentVoiceId = (voiceIndex == 0) ? voice1Id :
                             (voiceIndex == 1) ? voice2Id :
                             (voiceIndex == 2) ? voice3Id : voice4Id;

    VoiceConfig* config = voiceManager->getVoiceConfig(currentVoiceId);
    if (!config) return;

    // Set UI state for voice parameter mode feedback
    state.inVoiceParameterMode = true;
    state.lastVoiceParameterButton = paramIndex;
    state.voiceParameterChangeTime = millis();

    uint8_t displayVoiceNumber = static_cast<uint8_t>(voiceIndex + 1);

    switch (paramIndex) {
        case 8: // Toggle hasEnvelope per voice
            config->hasEnvelope = !config->hasEnvelope;
            Serial.print("Voice ");
            Serial.print(displayVoiceNumber);
            Serial.print(" envelope ");
            Serial.println(config->hasEnvelope ? "ON" : "OFF");
            break;
        case 9: // Toggle hasOverdrive
            config->hasOverdrive = !config->hasOverdrive;
            Serial.print("Voice ");
            Serial.print(displayVoiceNumber);
            Serial.print(" overdrive ");
            Serial.println(config->hasOverdrive ? "ON" : "OFF");
            break;
        case 10: // Toggle hasWavefolder
            config->hasWavefolder = !config->hasWavefolder;
            Serial.print("Voice ");
            Serial.print(displayVoiceNumber);
            Serial.print(" wavefolder ");
            Serial.println(config->hasWavefolder ? "ON" : "OFF");
            break;
        case 11: { // Cycle through filterMode
            int currentMode = static_cast<int>(config->filterMode);
            currentMode = (currentMode + 1) % 5; // 5 filter modes
            config->filterMode = static_cast<daisysp::LadderFilter::FilterMode>(currentMode);

            const char* filterNames[] = {"LP12", "LP24", "LP36", "BP12", "BP24"};
            Serial.print("Voice ");
            Serial.print(displayVoiceNumber);
            Serial.print(" filter mode: ");
            Serial.println(filterNames[currentMode]);
            }
            break;
        case 12: { // Cycle through filter resonance amounts
            float currentResonance = config->filterRes;
            currentResonance += 0.1f;
            if (currentResonance > 1.0f) currentResonance = 0.0f;
            config->filterRes = currentResonance;

            Serial.print("Voice ");
            Serial.print(displayVoiceNumber);
            Serial.print(" filter resonance: ");
            Serial.println(currentResonance, 2);
            }
            break;
        case 14: // Toggle Dalek effect
            config->hasDalek = !config->hasDalek;
            Serial.print("Voice ");
            Serial.print(displayVoiceNumber);
            Serial.print(" dalek ");
            Serial.println(config->hasDalek ? "ON" : "OFF");
            break;
        default:
            // Buttons 15-24 reserved for future voice parameters
            Serial.print("Voice parameter button ");
            Serial.print(paramIndex);
            Serial.println(" pressed (no action defined yet)");
            break;
    }

    // Apply the updated configuration to the voice to persist changes
    voiceManager->setVoiceConfig(currentVoiceId, *config);
}

// Handle generic control buttons by button id
void handleControlButton(int buttonId, UIState& state) {
    switch (buttonId) {
    case BUTTON_SLIDE_MODE:
        state.slideMode = !state.slideMode;
        state.selectedStepForEdit = -1;
        state.currentEditParameter = ParamId::Count; // Clear edit parameter
        Serial.print("Slide mode ");
        Serial.println(state.slideMode ? "ON" : "OFF");
        break;

    case BUTTON_AS5600_CONTROL:
        cycleAS5600Parameter(state);
        break;

    case BUTTON_PLAY_STOP:
        if (isClockRunning) {
            onClockStop();
            // Enter settings mode when stopping
            state.settingsMode = true;
        } else {
            onClockStart();
            // Exit settings mode if active
            if (state.settingsMode) {
                state.settingsMode = false;
                state.inPresetSelection = false;
                Serial.println("Exited settings mode");
            }
            state.flash25Until = millis() + CONTROL_LED_FLASH_DURATION_MS;
        }
        break;

    case BUTTON_CHANGE_SCALE:
        currentScale = (currentScale + 1) % 13;
        Serial.print("Scale changed to: ");
        Serial.print(currentScale);
        Serial.print(" (");
        Serial.print(scaleNames[currentScale]);
        Serial.println(")");
        break;

    case BUTTON_CHANGE_THEME:
        state.currentThemeIndex = (state.currentThemeIndex + 1) % static_cast<int>(LEDTheme::COUNT);
        setLEDTheme(static_cast<LEDTheme>(state.currentThemeIndex));
        break;

    case BUTTON_CHANGE_SWING_PATTERN: {
        state.currentShufflePatternIndex = (state.currentShufflePatternIndex + 1) % NUM_SHUFFLE_TEMPLATES;
        const ShuffleTemplate &currentTemplate = shuffleTemplates[state.currentShufflePatternIndex];
        // Apply shuffle template to uClock
        uClock.setShuffleTemplate(const_cast<int8_t *>(currentTemplate.ticks), SHUFFLE_TEMPLATE_SIZE);
        uClock.setShuffle(state.currentShufflePatternIndex > 0); // Enable shuffle if not "No Shuffle"

        Serial.print("Shuffle pattern changed to index ");
        Serial.print(state.currentShufflePatternIndex);
        Serial.print(": ");
        Serial.println(currentTemplate.name);
        }
        break;

    case BUTTON_TOGGLE_DELAY:
        state.delayOn = !state.delayOn;
        state.flash23Until = millis() + CONTROL_LED_FLASH_DURATION_MS;
        if (state.delayOn) {
            state.currentAS5600Parameter = AS5600ParameterMode::DelayTime;
        }
        break;

    default:
        // Unknown/unused control button
        break;
    }
}
