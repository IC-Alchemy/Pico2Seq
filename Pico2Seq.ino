#include "includes.h"
#include "diagnostic.h"
#include "src/dsp/dsp.h"
#include "src/voice/Voice.h"
#include "src/utils/Debug.h"
#include "src/scales/scales.h"
#include "src/voice/VoicePresets.h"
#include "src/voice/VoiceSystem.h"

// =======================
//   GLOBAL VARIABLES
// =======================
UIState uiState;   // Central state object for the UI
Sequencer seq1(1); // Channel 1 for first sequencer
Sequencer seq2(2); // Channel 2 for second sequencer
Sequencer seq3(3); // Channel 3 for third sequencer
Sequencer seq4(4); // Channel 4 for fourth sequencer
LEDMatrix ledMatrix;

// --- MIDI & Clock ---
Adafruit_USBD_MIDI raw_usb_midi;
midi::SerialMIDI<Adafruit_USBD_MIDI> serial_usb_midi(raw_usb_midi);
midi::MidiInterface<midi::SerialMIDI<Adafruit_USBD_MIDI>> usb_midi(serial_usb_midi);
Adafruit_MPR121 touchSensor = Adafruit_MPR121();

// =======================
//   AUDIO SYSTEM CONSTANTS
// =======================
constexpr float SAMPLE_RATE = 48000.0f;
constexpr size_t MAX_DELAY_SAMPLES = static_cast<size_t>(SAMPLE_RATE * 1.8f);
constexpr int NUM_AUDIO_BUFFERS = 3;
constexpr int SAMPLES_PER_BUFFER = 256;
constexpr float INT16_MAX_AS_FLOAT = 32767.0f;
constexpr float INT16_MIN_AS_FLOAT = -32768.0f;
constexpr float OSC_DETUNE_FACTOR = 0.001f;
constexpr float FEEDBACK_FADE_RATE = 0.01f;

// =======================
//   AUDIO SYSTEM VARIABLES
// =======================
// Voice Management System
std::unique_ptr<VoiceManager> voiceManager;
VoiceSystem voiceSystem; // Consolidated voice system

// Global Audio Effects (shared between voices)
daisysp::Svf delayLowPassFilter;
daisysp::DelayLine<float, MAX_DELAY_SAMPLES> globalDelayLine;
float feedbackGain1 = 0.65f;
float currentDelayOutputGain = 0.0f; // For smooth delay output fade
float currentFeedbackGain = 0.0f;    // For smooth delay feedback fade
float delayTarget = 48000.0f * 0.15f;
float currentDelay = 48000.0f * 0.15f;
float feedbackAmount = 0.45f; // Safer initial feedback level (typo fix: Ammount -> Amount)

// Audio Buffer Management
audio_buffer_pool_t *producer_pool = nullptr;

// Voice State Storage is now in voiceSystem

// =======================
//   HARDWARE INTERFACE CONSTANTS
// =======================
constexpr int MAX_HEIGHT = SensorConstants::DistanceSensor::MAX_DISTANCE_HEIGHT_MM;
constexpr int MIN_HEIGHT = SensorConstants::DistanceSensor::MIN_DISTANCE_HEIGHT_MM;
constexpr int PIN_TOUCH_IRQ = 10;
constexpr uint8_t PICO_AUDIO_I2S_DATA_PIN = 15;
constexpr uint8_t PICO_AUDIO_I2S_CLOCK_PIN_BASE = 16;
constexpr int IRQ_PIN = 1;
constexpr int MAX_MIDI_NOTES = 16;

// MIDI note calculation constants
constexpr int MIDI_BASE_NOTE_OFFSET = 36;  // Base MIDI note offset for scale calculations
constexpr int MIDI_NOTE_MIN = 0;           // Minimum valid MIDI note
constexpr int MIDI_NOTE_MAX = 127;         // Maximum valid MIDI note

// UI timing constants
constexpr unsigned long LED_UPDATE_INTERVAL_MS = 20;      // LED update rate (50Hz)
constexpr unsigned long CONTROL_UPDATE_INTERVAL_MS = 1;   // Sensor polling rate (1000Hz)

// =======================
//   UI AND DISPLAY VARIABLES
// =======================
OLEDDisplay display;
bool resetStepsLightsFlag = true;

// =======================
//   FORWARD DECLARATIONS
// =======================
void fill_audio_buffer(audio_buffer_t *buffer);
void initOscillators();

void updateParametersForStep(uint8_t stepToUpdate);
void onStepCallback(uint32_t uClockCurrentStep);
void applyEnvelopeParameters(const VoiceState &state, daisysp::Adsr &env);
float calculateFilterFrequency(float filterValue);
void setupI2SAudio(audio_format_t *audioFormat, audio_i2s_config_t *i2sConfig);
void setup();
void setup1();
void loop();
void loop1();

// =======================
//   SEQUENCER STATE VARIABLES
// =======================
uint8_t currentSequencerStep = 0;
// Gate states and timers are now in voiceSystem

// =======================
//   SENSOR INPUT VARIABLES
// =======================
int rawDistanceMm = 0;
int sensorDistanceMm = 0;
uint8_t currentScale = 0;
float baseFreq = 110.0f; // Hz
volatile bool touchFlag = false;

// =======================
//   TIMING AND CLOCK VARIABLES
// =======================
bool isClockRunning = true;
unsigned long previousMillis = 0;
uint32_t ppqnTicksPending = 0;

// =======================
//   INTERRUPT HANDLERS
// =======================
void touchInterrupt()
{
    touchFlag = true;
}

// =======================
//   AUDIO PROCESSING HELPER FUNCTIONS
// =======================
/**
 * @brief Smooth parameter transitions to prevent audio artifacts
 * @param currentDelay Current delay time value
 * @param targetDelay Target delay time value
 * @param slewRate Rate of change (0.0-1.0)
 * @return Smoothed delay value
 */
float delayTimeSmoothing(const float currentDelay, const float targetDelay, const float slewRate)
{
    const float difference = targetDelay - currentDelay;
    return currentDelay + (difference * slewRate);
}

// --- Clock Callbacks ---
void onSync24Callback(uint32_t tick)
{
    usb_midi.sendRealTime(midi::Clock);
}
void muteOscillators()
{

}

void unmuteOscillators()
{

}
void onClockStart()
{
    // Serial.println("[uClock] onClockStart()");
    usb_midi.sendRealTime(midi::Start);
    // Start all four sequencers so  LEDs and audio advance for 3/4 as well
    seq1.start();
    seq2.start();
    seq3.start();
    seq4.start();
    isClockRunning = true;
}

void onClockStop()
{
    usb_midi.sendRealTime(midi::Stop);
    // Stop all four sequencers
    seq1.stop();
    seq2.stop();
    seq3.stop();
    seq4.stop();

    // Use MidiNoteManager for comprehensive cleanup
    midiNoteManager.onSequencerStop();

    // Legacy allNotesOff() call for sequencer state cleanup
    isClockRunning = false;
    Serial.println("[uClock] onClockStop()");
}

// =======================
//   FUNCTION DEFINITIONS
// =======================


/**
 * @brief Initialize audio synthesis system and voice management
 *
 * Sets up global audio effects, delay processing, and voice management system.
 * Creates 4 voices with default presets and attaches them to sequencers.
 *
 * @note Called during setup() on Core 0 before audio processing begins
 */
void initOscillators()
{
    // Initialize global delay effect low-pass filter
    delayLowPassFilter.Init(SAMPLE_RATE);
    delayLowPassFilter.SetFreq(1340.0f); // Delay low-pass filter frequency
    delayLowPassFilter.SetRes(0.19f);    // Filter resonance
    delayLowPassFilter.SetDrive(0.95f);  // Filter drive amount

    // Initialize delay line
    globalDelayLine.Init();
    globalDelayLine.Reset(); // Clear any garbage in delay buffer

    // Set initial delay time (500ms)
    const float delayMs = 667.0f;
    size_t delaySamples = static_cast<size_t>(delayMs * SAMPLE_RATE * 0.001f);
    globalDelayLine.SetDelay(delaySamples);

    // Initialize delay target to match initial delay
    delayTarget = static_cast<float>(delaySamples);

    // Initialize Voice Manager with maximum 4 concurrent voices
    voiceManager = std::make_unique<VoiceManager>(4);

    // Create voices 1-4 using presets from UIState defaults
    const uint8_t *presetIndices = uiState.voicePresetIndices;
    Sequencer *sequencers[] = {&seq1, &seq2, &seq3, &seq4};

    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++)
    {
        voiceSystem.setVoiceId(i, voiceManager->addVoice(VoicePresets::getPresetConfig(presetIndices[i])));
        voiceManager->attachSequencer(voiceSystem.getVoiceId(i), sequencers[i]);
    }

    // Note: OLED display registration occurs in setup1() after display initialization
}
/*
 * Apply voice preset to the specified voice index (zero-based).
 *
 * Note:
 * - Caller must pass a zero-based voiceIndex in range [0, VoiceSystem::MAX_VOICES-1].
 * - VoiceManager and VoiceSystem use internal voice IDs; we look them up via voiceSystem.getVoiceId().
 */
void applyVoicePreset(uint8_t voiceIndex, uint8_t presetIndex)
{
    if (presetIndex >= VoicePresets::getPresetCount())
    {
        Serial.println("Invalid preset index");
        return;
    }

    VoiceConfig config = VoicePresets::getPresetConfig(presetIndex);

    if (voiceIndex >= VoiceSystem::MAX_VOICES)
    {
        Serial.println("Invalid voice index");
        return;
    }

    uint8_t voiceId = voiceSystem.getVoiceId(voiceIndex); // voiceIndex is zero-based

    if (voiceManager->setVoiceConfig(voiceId, config))
    {
        Serial.print("Applied preset '");
        Serial.print(VoicePresets::getPresetName(presetIndex));
        Serial.print("' to Voice ");
        Serial.println(voiceIndex); // Display zero-based index
    }
    else
    {
        Serial.println("Failed to apply voice preset");
    }
}


// Long press detection is now handled by ButtonManager module
// isVoice2Mode is now managed by ButtonManager module
int currentThemeIndex = static_cast<int>(LEDTheme::DEFAULT); // Global variable for current theme

/**
 * @brief PPQN output callback for timing synchronization
 * @param tick Current PPQN tick count
 * @note Keep minimal - runs in interrupt context
 */
void onOutputPPQNCallback(uint32_t tick)
{
    // Increment counter to signal pending tick processing
    ppqnTicksPending++;
}

// =======================
//   HELPER FUNCTIONS FOR VOICE PARAMETER CALCULATIONS
// =======================

/**

 * @param state Voice state containing normalized parameter values (0.0-1.0)
 * @param env DaisySP ADSR envelope to configure
 * @param voiceNum Voice number (1 ,2,3,4) for voice-specific parameter ranges
 */
void applyEnvelopeParameters(const VoiceState &state, daisysp::Adsr &env)
{
    float attack, release;

    attack = daisysp::fmap(state.attackTimeSeconds, 0.005f, 0.75f, daisysp::Mapping::LINEAR);
    release = daisysp::fmap(state.decayTimeSeconds, 0.001f, .8f, daisysp::Mapping::LINEAR);

    env.SetDecayTime(.085f + (release * 0.35f));
    env.SetAttackTime(attack);
    env.SetReleaseTime(release);
}

/**
 * Calculate filter cutoff frequency with exponential scaling.
 * Range: 100Hz to 8010Hz provides musical filter sweep from bass to presence
 * @param filterValue Normalized filter value (0.0-1.0) from sequencer
 * @return Filter frequency in Hz
 */
float calculateFilterFrequency(const float filterValue)
{
    return daisysp::fmap(filterValue, 100.0f, 8010.0f, daisysp::Mapping::EXP);
}

uint8_t getVoiceIndexFromGateRole(bool isVoice2)
{
    return isVoice2 ? 1 : 0;
}

int calculateMidiNoteForState(const VoiceState &state)
{
    uint8_t noteIndex = static_cast<uint8_t>(std::max(0.0f, std::min(state.noteIndex, static_cast<float>(SCALE_STEPS - 1))));
    return scale[currentScale][noteIndex] + MIDI_BASE_NOTE_OFFSET + static_cast<int>(state.octaveOffset);
}

void startOrRetriggerMidiGate(
    const VoiceState &state,
    uint8_t midiVoiceId,
    int midiNote,
    volatile bool *gate)
{
    if (!(*gate))
    {
        *gate = true;
        int clampedMidiNote = std::max(MIDI_NOTE_MIN, std::min(midiNote, MIDI_NOTE_MAX));
        midiNoteManager.noteOn(midiVoiceId, static_cast<int8_t>(clampedMidiNote),
                               static_cast<uint8_t>(state.velocityLevel * 127), 1, state.gateLengthTicks);
        return;
    }

    int8_t currentActiveNote = midiNoteManager.getActiveNote(midiVoiceId);
    if (currentActiveNote != midiNote)
    {
        midiNoteManager.noteOn(midiVoiceId, static_cast<int8_t>(midiNote),
                               static_cast<uint8_t>(state.velocityLevel * 127), 1, state.gateLengthTicks);
    }
    *gate = true;
}

void updateGateAndMidiState(
    const VoiceState &state,
    bool isVoice2,
    volatile bool *gate,
    volatile GateTimer *gateTimer)
{
    uint8_t midiVoiceId = getVoiceIndexFromGateRole(isVoice2);

    if (state.isGateHigh)
    {
        int midiNote = calculateMidiNoteForState(state);
        gateTimer->start(state.gateLengthTicks);
        startOrRetriggerMidiGate(state, midiVoiceId, midiNote, gate);
        midiNoteManager.setGateState(midiVoiceId, true, state.gateLengthTicks);
        return;
    }

    gateTimer->stop();
    *gate = false;
    midiNoteManager.setGateState(midiVoiceId, false);
}

void updateVoiceManagerState(uint8_t voiceIndex, const VoiceState &state)
{
    uint8_t voiceId = voiceSystem.getVoiceId(voiceIndex);
    voiceManager->updateVoiceState(voiceId, state);
}

void sendVoiceParameterMidiCCs(uint8_t midiVoiceId, const VoiceState &state)
{
    midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Filter, state.filterCutoff);
    midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Attack, state.attackTimeSeconds);
    midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Decay, state.decayTimeSeconds);
    midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Octave, state.octaveOffset);
}


void updateVoiceParameters(
    const VoiceState &state,
    bool isVoice2,
    bool updateGate = false,
    volatile bool *gate = nullptr,
    volatile GateTimer *gateTimer = nullptr)
{
    // Handle gate timing and MIDI note events (sequencer playback mode only)
    if (updateGate && gate && gateTimer)
    {
        updateGateAndMidiState(state, isVoice2, gate, gateTimer);
    }

    // GATE-CONTROLLED FREQUENCY UPDATES: Only update frequency when gate is HIGH
    // This prevents new frequencies from being sent when gate is LOW, allowing
    // current notes to continue playing or fade naturally
    // Frequency will be updated inside Voice::updateParameters() when gate is HIGH

    // Update all voice parameters through VoiceManager in single call
    updateVoiceManagerState(getVoiceIndexFromGateRole(isVoice2), state);
}


// New helper to update a specific voice (1-4)
void updateVoiceParametersForVoice(
    const VoiceState &state,
    uint8_t voiceNumber,
    bool updateGate = false,
    volatile bool *gate = nullptr,
    volatile GateTimer *gateTimer = nullptr)
{
    // For voices 1 and 2, reuse existing gate/MIDI logic; for 3/4 skip gates
    bool isVoice2 = (voiceNumber == 2);

    if (updateGate && (voiceNumber == 1 || voiceNumber == 2))
    {
        updateVoiceParameters(state, isVoice2, updateGate, gate, gateTimer);
        return;
    }

    // Map to actual VoiceManager voice IDs
    if (voiceNumber < 1 || voiceNumber > VoiceSystem::MAX_VOICES)
    {
        return; // Invalid voice number
    }

    uint8_t voiceIndex = voiceNumber - 1; // Convert 1-based to 0-based

    // Push full state to voice (Voice computes frequencies internally on gate HIGH)
    updateVoiceManagerState(voiceIndex, state);

    // Send MIDI CC only for voices 1 and 2
    if (voiceNumber == 1 || voiceNumber == 2)
    {
        uint8_t midiVoiceId = (voiceNumber == 1) ? 0 : 1;
        sendVoiceParameterMidiCCs(midiVoiceId, state);
    }

    // Voice separation verified - distance sensor now voice-specific
}
/**
 * Apply immediate voice parameter updates during real-time parameter recording.
 * Called when user holds parameter buttons (16-21) to provide instant audio feedback.
 */
void updateActiveVoiceState(uint8_t stepIndex, Sequencer &activeSeq)
{
    uint8_t currentSequencerStep = activeSeq.getCurrentStep();

    // Only update currently playing step to avoid audio glitches
    if (stepIndex != currentSequencerStep)
    {
        return;
    }

    // Determine currently selected voice (1-4)
    uint8_t selectedIndex = uiState.selectedVoiceIndex; // 0..3
    uint8_t voiceNumber = selectedIndex + 1;            // 1..4

    // Pick the matching VoiceState for the selected voice
    if (voiceNumber < 1 || voiceNumber > VoiceSystem::MAX_VOICES)
    {
        return; // Invalid voice number
    }

    uint8_t voiceIndex = voiceNumber - 1; // Convert 1-based to 0-based
    VoiceState *activeVoiceState = &voiceSystem.getVoiceState(voiceIndex);

    // Update voice state with new step parameters + AS5600 encoder modifications
    activeSeq.playStepNow(stepIndex, activeVoiceState);

    // Apply AS5600 base values for all voices (map 1&3 -> base set 1, 2&4 -> base set 2)
    applyAS5600BaseValues(activeVoiceState, voiceNumber - 1);

    // Update synth hardware for immediate audio feedback using the per-voice function
    updateVoiceParametersForVoice(*activeVoiceState, voiceNumber);

    //Serial.print("Applied immediate voice updates for step ");
    //Serial.println(stepIndex);
}
// --- Update Parameters for Step Editing ---
void updateParametersForStep(uint8_t stepToUpdate) ///  This is the selected step for edit function
{
    if (stepToUpdate >= SequencerConstants::MAX_STEPS_COUNT)
        return;

    Sequencer *seqPtr = (uiState.selectedVoiceIndex == 0) ? &seq1 : (uiState.selectedVoiceIndex == 1) ? &seq2
                                                                : (uiState.selectedVoiceIndex == 2)   ? &seq3
                                                                                                      : &seq4;
    Sequencer &activeSeq = *seqPtr;

    // Simple normalization of sensor value
    float normalizedSensorValue = 0.0f;
    if (MAX_HEIGHT > 0)
    {
        normalizedSensorValue = static_cast<float>(sensorDistanceMm) / static_cast<float>(MAX_HEIGHT);
    }
    normalizedSensorValue = std::max(0.0f, std::min(normalizedSensorValue, 1.0f));

    bool parametersWereUpdated = false;
    const ParamButtonMapping *heldMapping = getHeldParameterButton(uiState);
    if (heldMapping)
    {
        // GATE-CONTROLLED NOTE PROGRAMMING: Check gate restriction for Note parameter
        if (heldMapping->paramId == ParamId::Note)
        {
            float gateValue = activeSeq.getStepParameterValue(ParamId::Gate, stepToUpdate);
            if (gateValue <= 0.5f) // Gate is LOW (0.0)
            {
                // Skip Note parameter editing on steps with LOW gates
                // This protects steps from note frequency changes during programming/editing
                return;
            }
        }

        // Use the helper function to do the scaling correctly for any parameter.
        float valueToSet = mapNormalizedValueToParamRange(heldMapping->paramId, normalizedSensorValue);
        activeSeq.setStepParameterValue(heldMapping->paramId, stepToUpdate, valueToSet);
        parametersWereUpdated = true;

        // Send immediate MIDI CC for real-time parameter recording (voices 1/2 only)
        uint8_t midiVoiceId = (uiState.selectedVoiceIndex == 0) ? 0 : (uiState.selectedVoiceIndex == 1) ? 1
                                                                                                        : 255;
        if (midiVoiceId != 255)
        {
            midiNoteManager.updateParameterCC(midiVoiceId, heldMapping->paramId, valueToSet);
        }

        // Debug print if needed
      //  Serial.print("  -> Set ");
     //   Serial.print(CORE_PARAMETERS[static_cast<int>(heldMapping->paramId)].name);
       // Serial.print(" to ");
      //  Serial.println(valueToSet);
    }

    // Provide immediate audio feedback when recording parameters to current step
    if (parametersWereUpdated)
    {
        updateActiveVoiceState(stepToUpdate, activeSeq);
    }
}


int getDistanceForVoice(uint8_t voiceIndex)
{
    return (uiState.selectedVoiceIndex == voiceIndex) ? sensorDistanceMm : -1;
}

void advanceSequencersForStep(uint32_t uClockCurrentStep, VoiceState *voiceStates)
{
    seq1.advanceStep(uClockCurrentStep, getDistanceForVoice(0), uiState, &voiceStates[0]);
    seq2.advanceStep(uClockCurrentStep, getDistanceForVoice(1), uiState, &voiceStates[1]);
    seq3.advanceStep(uClockCurrentStep, getDistanceForVoice(2), uiState, &voiceStates[2]);
    seq4.advanceStep(uClockCurrentStep, getDistanceForVoice(3), uiState, &voiceStates[3]);
}

void applyAS5600ValuesToStepStates(VoiceState *voiceStates)
{
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++)
    {
        applyAS5600BaseValues(&voiceStates[i], i);
    }

    applyAS5600DelayValues();
}

void updateSynthAndStoreVoiceState(uint8_t voiceIndex, VoiceState &state)
{
    if (voiceIndex < 2)
    {
        updateVoiceParametersForVoice(state, voiceIndex + 1, true,
                                      &voiceSystem.getGate(voiceIndex),
                                      &voiceSystem.getGateTimer(voiceIndex));
    }
    else
    {
        updateVoiceParametersForVoice(state, voiceIndex + 1, false);
    }

    voiceSystem.getVoiceState(voiceIndex) = state;
}

void updateSynthAndStoreVoiceStates(VoiceState *voiceStates)
{
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++)
    {
        updateSynthAndStoreVoiceState(i, voiceStates[i]);
    }
}

//  This gets called every 16th note
void onStepCallback(uint32_t uClockCurrentStep)
{
    currentSequencerStep = static_cast<uint8_t>(uClockCurrentStep); // Raw uClock step, sequencers handle their own modulo

    VoiceState tempStates[VoiceSystem::MAX_VOICES];
    advanceSequencersForStep(uClockCurrentStep, tempStates);
    applyAS5600ValuesToStepStates(tempStates);
    updateSynthAndStoreVoiceStates(tempStates);
}

 // Constants kept local for clarity and zero-cost access
    constexpr float kInt16MaxF = 32767.0f;   // +0x7FFF
    constexpr float kInt16MinF = -32768.0f;  // -0x8000

    // Branchless clamp for float; avoids dependency on external DSP headers
    static inline float clampf(float v, float lo, float hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    /**
     * Convert normalized float sample [-1.0, +1.0] to 16-bit PCM with saturation and round-to-nearest.
     *
     * Optimizations:
     * - Clamp BEFORE scaling to prevent overflow
     * - Use lrintf for fast round-to-nearest (hardware instruction on ARM)
     * - Removed isfinite() check (assumes clean audio path, saves ~10 cycles/sample)
     * - Removed post-rounding saturation (redundant after proper clamping)
     *
     * Note: If NaN/Inf issues occur, re-enable isfinite() check in debug builds
     */
    static inline int16_t FloatToPcm16(float sample) noexcept
    {
        // Clamp to legal audio range [-1.0, 1.0]
        const float s = clampf(sample, -1.0f, 1.0f);

        // Scale to 16-bit domain and round to nearest
        // lrintf maps to ARM VCVTR instruction (single-cycle on Cortex-M33)
        const float scaled = s * kInt16MaxF;
        return static_cast<int16_t>(lrintf(scaled));
    }
/**
 * @brief Main audio buffer processing function (Core 0 - Real-time critical)
 *
 * This function runs on Core 0 and must maintain real-time performance.
 * It processes all voices through the VoiceManager and applies global delay effects.
 *
 * @param buffer Audio buffer to fill with processed samples
 *
 * @note Dual-core constraint: Only audio processing occurs here
 * @warning Keep processing minimal to prevent audio dropouts
 */
void fill_audio_buffer(audio_buffer_t *buffer)
{
    int N = buffer->max_sample_count;
    int16_t *out = reinterpret_cast<int16_t *>(buffer->buffer->bytes);
    float finalVoiceOutput;

    // Process each sample in the buffer
    for (int i = 0; i < N; ++i)
    {
        // Process all voices through VoiceManager (voice states updated by sequencer callbacks)
        finalVoiceOutput = voiceManager->processAllVoices();

        // Convert once for both channels (mono -> stereo)
        // FloatToPcm16 handles clamping internally, no need for redundant clamp
        int16_t convertedSample = FloatToPcm16(finalVoiceOutput);
        out[2 * i + 0] = convertedSample; // Left channel
        out[2 * i + 1] = convertedSample; // Right channel
    }

    buffer->sample_count = N;
}

/**
 * @brief Process delay effect with feedback and filtering
 *
 * Implements a delay line with low-pass filtered feedback to prevent harsh artifacts.
 * Uses smooth gain transitions to avoid audio pops when delay is toggled.
 *
 * @param inputSignal Dry input signal from voice processing
 * @return Mixed dry and wet signal with delay effect applied
 *
 * @note Feedback is clamped at 75% to prevent runaway feedback
 */
float processDelayEffect(const float inputSignal)
{
    // Read current delay output
    float delayOutput = globalDelayLine.Read();

    // Calculate feedback signal with current gain
    float feedbackSignal = delayOutput * currentFeedbackGain;

    // Apply low-pass filtering to feedback to prevent harsh artifacts
    delayLowPassFilter.Process(feedbackSignal);
    float filteredFeedback = delayLowPassFilter.Low();

    // Write to delay line: dry input + filtered feedback (clamped at 75%)
    globalDelayLine.Write(inputSignal + (filteredFeedback * 0.75f));

    // Mix dry and wet signals based on current delay output gain
    return inputSignal + (delayOutput * currentDelayOutputGain);
}

/**
 * @brief Configure and initialize I2S audio hardware interface
 *
 * Sets up I2S audio output with specified format and configuration.
 * Connects the audio buffer pool and enables audio processing.
 *
 * @param audioFormat Audio format specification (sample rate, bit depth, channels)
 * @param i2sConfig I2S hardware configuration (pins, DMA, PIO)
 *
 * @note Sets global error flags if initialization fails
 */
void setupI2SAudio(audio_format_t *audioFormat, audio_i2s_config_t *i2sConfig)
{
    // Initialize I2S hardware with specified format
    if (!audio_i2s_setup(audioFormat, i2sConfig))
    {
        g_errorState |= ERR_AUDIO;
        return;
    }

    // Connect audio buffer pool to I2S interface
    if (!audio_i2s_connect(producer_pool))
    {
        g_errorState |= ERR_AUDIO;
        return;
    }

    // Enable audio processing
    audio_i2s_set_enabled(true);
    g_audioOK = true;
}

// =======================
//   CORE 0 SETUP (AUDIO PROCESSING)
// =======================
/**
 * @brief Core 0 initialization - Audio system setup
 *
 * Initializes the audio synthesis system, configures I2S hardware interface,
 * and sets up audio buffer management. This core is dedicated to real-time
 * audio processing only.
 *
 * @note Runs on Core 0 - Keep minimal for real-time performance
 */
void setup()
{
    delay(100); // Allow system stabilization

    // Initialize audio synthesis system
    initOscillators();

    // Configure audio format (48kHz, 16-bit stereo)
    static audio_format_t audioFormat = {
        .sample_freq = static_cast<uint32_t>(SAMPLE_RATE),
        .format = AUDIO_BUFFER_FORMAT_PCM_S16,
        .channel_count = 2};

    // Configure audio buffer format
    static audio_buffer_format_t bufferFormat = {
        .format = &audioFormat,
        .sample_stride = 4 // 2 channels * 2 bytes per sample
    };

    // Create audio buffer pool
    producer_pool = audio_new_producer_pool(&bufferFormat, NUM_AUDIO_BUFFERS, SAMPLES_PER_BUFFER);

    // Configure I2S hardware interface
    audio_i2s_config_t i2sConfig = {
        .data_pin = PICO_AUDIO_I2S_DATA_PIN,
        .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
        .dma_channel = 0,
        .pio_sm = 0};

    // Initialize I2S audio system
    setupI2SAudio(&audioFormat, &i2sConfig);
}

// =======================
//   CORE 1 SETUP (UI, MIDI, SENSORS)
// =======================
/**
 * @brief Core 1 initialization - UI and control systems
 *
 * Initializes all non-audio systems including MIDI, sensors, display,
 * LED matrix, and user interface components. This core handles all
 * user interaction and system control.
 */
void setup1()
{
    delay(300); // Allow Core 0 audio system to stabilize

    // Initialize MIDI communication
    usb_midi.begin(MIDI_CHANNEL_OMNI);

    delay(100);

    Serial.begin(115200);

    //  Debug::setLevel(Debug::Level::Info);

    Serial.print("[CORE1] Setup starting... ");

    randomSeed(analogRead(A0) + millis());
    ledMatrix.begin(100);
    setupLEDMatrixFeedback();
    initLEDController();

    if (!distanceSensor.begin())
    {
        Serial.println("[ERROR] Distance sensor initialization failed!");
    }
    else
    {
        Serial.println("Distance sensor initialized successfully");
    }

    // Initialize AS5600 magnetic encoder
    if (!as5600Sensor.begin())
    {
        Serial.println("[ERROR] AS5600 magnetic encoder initialization failed!");
    }
    else
    {
        Serial.println("AS5600 magnetic encoder initialized successfully");

        // Uncomment the line below to run smooth scaling validation test
        // as5600Sensor.validateSmoothScaling();
    }

    // Initialize AS5600 base values with proper defaults
    initAS5600BaseValues();

    if (!touchSensor.begin(0x5A))
    {
        Serial.println("MPR121 not found, check wiring?");
        while (1)
            ;
    }
    else
    {
        Serial.println("MPR121 found and initialized");
        touchSensor.setAutoconfig(true);

        // Configure MPR121 touch thresholds.
        // Using the original, more conservative thresholds.
        touchSensor.setThresholds(55, 22); // touch, release thresholds
        // Serial.println("MPR121 thresholds configured to 155/55");
    }

    // Initialize OLED display
    display.begin();
    Serial.println("OLED display initialized");

    // Register OLED display as observer for voice parameter changes
    if (voiceManager)
    {
        display.setVoiceManager(voiceManager.get());

        // Use VoiceManager's callback system for parameter updates
        voiceManager->setVoiceUpdateCallback([](uint8_t voiceId, const VoiceState &state)
                                             { display.onVoiceParameterChanged(voiceId, state); });

        Serial.println("OLED display registered as voice parameter observer");
    }
    else
    {
        Serial.println("[ERROR] VoiceManager not initialized - cannot register OLED observer");
    }

    Matrix_init(&touchSensor);
    Serial.println("Matrix initialized");

    // Force a matrix scan to test the system
    Serial.println("Forcing initial matrix scan...");
    Matrix_scan();
    // Matrix_printState();

    // Use a lambda to capture the context needed by the event handler
    Matrix_setEventHandler([](const MatrixButtonEvent &evt)
                           {
        Serial.print("Matrix event: button ");
        Serial.print(evt.buttonIndex);
        Serial.print(evt.type == MATRIX_BUTTON_PRESSED ? " pressed" : " released");
        Serial.println();
        Sequencer* seqs[] = { &seq1, &seq2, &seq3, &seq4 };
        matrixEventHandler(evt, uiState, seqs, 4, midiNoteManager); });

    uClock.init();
    uClock.setOnSync24(onSync24Callback);
    uClock.setOnClockStart(onClockStart);
    uClock.setOnClockStop(onClockStop);
    uClock.setOutputPPQN(uClock.PPQN_480);
    uClock.setOnStep(onStepCallback);
    uClock.setOnOutputPPQN(onOutputPPQNCallback);
    uClock.setTempo(90);
    uClock.start();
    uClock.setShuffle(true);
    seq1.start();
    seq2.start();

    Serial.println("[CORE1] Setup complete!");
}

// =======================
//   CORE 0 MAIN LOOP (AUDIO PROCESSING)
// =======================
/**
 * @brief Core 0 main loop - Real-time audio processing
 *
 * Continuously processes audio buffers for I2S output. This loop must maintain
 * real-time performance to prevent audio dropouts. Only audio processing
 * occurs on this core.
 *
 * @note Dual-core architecture: UI, MIDI, and sensors handled on Core 1
 */
void loop()
{
    // Get available audio buffer from pool
    audio_buffer_t *audioBuffer = take_audio_buffer(producer_pool, true);

    if (audioBuffer)
    {
        // Fill buffer with processed audio samples
        fill_audio_buffer(audioBuffer);

        // Return completed buffer to I2S system
        give_audio_buffer(producer_pool, audioBuffer);
    }
}

void processMidiIO()
{
    usb_midi.read();
}

void pollHeldUIButtons()
{
    pollUIHeldButtons(uiState, seq1, seq2);
}

void processPendingPpqnTicks()
{
    static uint16_t globalTickCounter = 0; // Global tick counter for MidiNoteManager

    while (ppqnTicksPending > 0)
    {
        // Decrement the counter *before* processing the tick
        ppqnTicksPending--;
        globalTickCounter++;

        // Update MidiNoteManager timing - this handles all MIDI note-off timing
        midiNoteManager.updateTiming(globalTickCounter);

        // Process sequencer note duration timing
        seq1.tickNoteDuration(&voiceSystem.getVoiceState(0));
        seq2.tickNoteDuration(&voiceSystem.getVoiceState(1));

        // Process gate timers - now synchronized with MidiNoteManager
        voiceSystem.tickAllGateTimers();
    }
}

void pollControlsAndSensors()
{
    // Preserve existing held-parameter check timing even though the value is not used here.
    bool parameterRecordingActive = isAnyParameterButtonHeld(uiState);
    (void)parameterRecordingActive;

    // Scan button matrix for user input
    Matrix_scan();

    // Update AS5600 magnetic encoder for base parameter control
    as5600Sensor.update();
    updateAS5600BaseValues(uiState);

    // Update distance sensor for real-time parameter recording
    distanceSensor.update();
    int rawDistanceValue = distanceSensor.getRawDistanceMm();
    if (rawDistanceValue >= MIN_HEIGHT && rawDistanceValue <= MAX_HEIGHT)
    {
        sensorDistanceMm = rawDistanceValue - MIN_HEIGHT;
    }
    else
    {
        sensorDistanceMm = 0; // Invalid reading - use safe default
    }
}

void updateDisplayAndLEDs()
{
    // Handle voice switch display updates
    if (uiState.voiceSwitchTriggered)
    {
        uiState.voiceSwitchTriggered = false; // Clear the trigger flag
        display.onVoiceSwitched(uiState, voiceManager.get());
    }

    // Update step sequence LEDs
    updateStepLEDs(ledMatrix, seq1, seq2, seq3, seq4, uiState, sensorDistanceMm);

    // Update OLED display
    display.update(uiState, seq1, seq2, seq3, seq4, voiceManager.get());

    // Update control parameter LEDs
    updateControlLEDs(ledMatrix, uiState);

    // Apply LED updates to hardware
    ledMatrix.show();
}

void recordSelectedStepParameters()
{
    // Apply distance sensor values to selected step when parameter buttons are held
    if (uiState.selectedStepForEdit != -1)
    {
        updateParametersForStep(uiState.selectedStepForEdit);
    }
}

// =======================
//   CORE 1 MAIN LOOP (UI, MIDI, SENSORS)
// =======================
/**
 * @brief Core 1 main loop - User interface and control processing
 *
 * Handles all non-audio processing including MIDI I/O, sensor reading,
 * button matrix scanning, LED updates, and display rendering.
 *
 * @note Dual-core architecture: Audio processing handled on Core 0
 */
void loop1()
{
    processMidiIO();

    unsigned long currentMillis = millis();
    pollHeldUIButtons();
    processPendingPpqnTicks();

    // =======================
    //   UI UPDATE TIMING CONTROL
    // =======================
    static unsigned long lastLEDUpdate = 0;
    static unsigned long lastControlUpdate = 0;

    // =======================
    //   SENSOR AND CONTROL INPUT PROCESSING
    // =======================
    // Button matrix, distance sensor, and AS5600 polling (1000Hz update rate)
    if ((currentMillis - lastControlUpdate >= CONTROL_UPDATE_INTERVAL_MS))
    {
        lastControlUpdate = currentMillis;
        pollControlsAndSensors();
    }

    // Update LEDs and display at controlled intervals (50Hz update rate)
    if (currentMillis - lastLEDUpdate >= LED_UPDATE_INTERVAL_MS)
    {
        lastLEDUpdate = currentMillis;
        updateDisplayAndLEDs();
    }

    // =======================
    //   REAL-TIME PARAMETER RECORDING
    // =======================
    recordSelectedStepParameters();
}
