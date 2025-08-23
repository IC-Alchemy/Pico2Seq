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
daisysp::Svf delLowPass;
daisysp::DelayLine<float, MAX_DELAY_SAMPLES> del1;
float feedbackGain1 = 0.65f;
float currentDelayOutputGain = 0.0f; // For smooth delay output fade
float currentFeedbackGain = 0.0f;    // For smooth delay feedback fade
float delayTarget = 48000.0f * 0.15f;
float currentDelay = 48000.0f * 0.15f;
float feedbackAmmount = 0.45f; // Safer initial feedback level

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
int raw_mm = 0;
int mm = 0;
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
float delayTimeSmoothing(float currentDelay, float targetDelay, float slewRate)
{
    float difference = targetDelay - currentDelay;
    return currentDelay + (difference * slewRate);
}

// --- Clock Callbacks ---
void onSync24Callback(uint32_t tick)
{
    usb_midi.sendRealTime(midi::Clock);
}
void muteOscillators()
{
    if (voiceManager)
    {
        voiceSystem.muteAllVoices(voiceManager.get());
    }
}

void unmuteOscillators()
{
    if (voiceManager)
    {
        voiceSystem.unmuteAllVoices(voiceManager.get());
    }
}
void onClockStart()
{
    // Serial.println("[uClock] onClockStart()");
    usb_midi.sendRealTime(midi::Start);
    // Start all four sequencers so LEDs and audio advance for 3/4 as well
    seq1.start();
    seq2.start();
    seq3.start();
    seq4.start();
    isClockRunning = true;
    unmuteOscillators();
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
    muteOscillators();
    isClockRunning = false;
    Serial.println("[uClock] onClockStop()");
}

// =======================
//   FUNCTION DEFINITIONS
// =======================

/**
 * @brief Convert floating-point audio sample to 16-bit integer
 *
 * Scales and clamps floating-point samples to prevent overflow and distortion.
 *
 * @param sample Floating-point sample (-1.0 to +1.0 range)
 * @return 16-bit integer sample for I2S output
 */
static inline int16_t convertSampleToInt16(float sample)
{
    float scaled = sample * INT16_MAX_AS_FLOAT;
    scaled = roundf(scaled);
    scaled = daisysp::fclamp(scaled, INT16_MIN_AS_FLOAT, INT16_MAX_AS_FLOAT);
    return static_cast<int16_t>(scaled);
}

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
    delLowPass.Init(SAMPLE_RATE);
    delLowPass.SetFreq(1340.0f); // Delay low-pass filter frequency
    delLowPass.SetRes(0.19f);    // Filter resonance
    delLowPass.SetDrive(0.95f);  // Filter drive amount

    // Initialize delay line
    del1.Init();
    del1.Reset(); // Clear any garbage in delay buffer

    // Set initial delay time (500ms)
    const float delayMs = 667.0f;
    size_t delaySamples = static_cast<size_t>(delayMs * SAMPLE_RATE * 0.001f);
    del1.SetDelay(delaySamples);

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

// Apply voice preset to the specified voice
void applyVoicePreset(uint8_t voiceNumber, uint8_t presetIndex)
{
    if (presetIndex >= VoicePresets::getPresetCount())
    {
        Serial.println("Invalid preset index");
        return;
    }

    VoiceConfig config = VoicePresets::getPresetConfig(presetIndex);

    if (voiceNumber < 1 || voiceNumber > VoiceSystem::MAX_VOICES)
    {
        Serial.println("Invalid voice number");
        return;
    }

    uint8_t voiceId = voiceSystem.getVoiceId(voiceNumber - 1); // Convert 1-based to 0-based index

    if (voiceManager->setVoiceConfig(voiceId, config))
    {
        Serial.print("Applied preset '");
        Serial.print(VoicePresets::getPresetName(presetIndex));
        Serial.print("' to Voice ");
        Serial.println(voiceNumber);
    }
    else
    {
        Serial.println("Failed to apply voice preset");
    }
}

// Long press detection is now handled by ButtonManager module
// isVoice2Mode is now managed by ButtonManager module
const uint8_t VOICE2_LED_OFFSET = 32;                        // Starting LED index for Voice 2
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
 * Range: 100Hz to 5710Hz provides musical filter sweep from bass to presence
 * @param filterValue Normalized filter value (0.0-1.0) from sequencer
 * @return Filter frequency in Hz
 */
float calculateFilterFrequency(float filterValue)
{
    return daisysp::fmap(filterValue, 100.0f, 8010.0f, daisysp::Mapping::EXP);
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
    float normalized_mm_value = 0.0f;
    if (MAX_HEIGHT > 0)
    {
        normalized_mm_value = static_cast<float>(mm) / static_cast<float>(MAX_HEIGHT);
    }
    normalized_mm_value = std::max(0.0f, std::min(normalized_mm_value, 1.0f));

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
        float valueToSet = mapNormalizedValueToParamRange(heldMapping->paramId, normalized_mm_value);
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
        uint8_t voiceId = isVoice2 ? 1 : 0;

        if (state.isGateHigh)
        {
            // Calculate MIDI note to match audio synthesis approach
            uint8_t noteIndex = static_cast<uint8_t>(std::max(0.0f, std::min(state.noteIndex, static_cast<float>(SCALE_STEPS - 1))));
            int midiNote = scale[currentScale][noteIndex] + 36 + static_cast<int>(state.octaveOffset);

            // Always restart the gate timer for gated steps to ensure proper timing
            gateTimer->start(state.gateLengthTicks);

            // Only send MIDI note-on when gate transitions from off to on
            if (!(*gate))
            {
                *gate = true;

                // Clamp MIDI note to valid range (0-127)
                int clampedMidiNote = std::max(0, std::min(midiNote, 127));

                // Use MidiNoteManager for proper note lifecycle management
                midiNoteManager.noteOn(voiceId, static_cast<int8_t>(clampedMidiNote),
                                       static_cast<uint8_t>(state.velocityLevel * 127), 1, state.gateLengthTicks);
            }
            else
            {
                // Gate is already on - check if note changed and handle retrigger
                int8_t currentActiveNote = midiNoteManager.getActiveNote(voiceId);
                if (currentActiveNote != midiNote)
                {
                    // Note changed during gate - retrigger with new note
                    midiNoteManager.noteOn(voiceId, static_cast<int8_t>(midiNote),
                                           static_cast<uint8_t>(state.velocityLevel * 127), 1, state.gateLengthTicks);
                }
                *gate = true;
            }

            // Update MidiNoteManager gate state
            midiNoteManager.setGateState(voiceId, true, state.gateLengthTicks);
        }
        else
        {
            // Step has no gate - turn off immediately
            gateTimer->stop();
            *gate = false;

            // Use MidiNoteManager for proper note-off handling
            midiNoteManager.setGateState(voiceId, false);
        }
    }

    // OPTIMIZATION: Calculate voice ID once and consolidate all voice updates
    uint8_t voiceIndex = isVoice2 ? 1 : 0;
    uint8_t voiceId = voiceSystem.getVoiceId(voiceIndex);

    // GATE-CONTROLLED FREQUENCY UPDATES: Only update frequency when gate is HIGH
    // This prevents new frequencies from being sent when gate is LOW, allowing
    // current notes to continue playing or fade naturally
    // Frequency will be updated inside Voice::updateParameters() when gate is HIGH

    // Update all voice parameters through VoiceManager in single call
    voiceManager->updateVoiceState(voiceId, state);

    // Send MIDI CC messages for parameter changes
    uint8_t midiVoiceId = isVoice2 ? 1 : 0;
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
    uint8_t voiceId = voiceSystem.getVoiceId(voiceIndex);

    // Push full state to voice (Voice computes frequencies internally on gate HIGH)
    voiceManager->updateVoiceState(voiceId, state);

    // Send MIDI CC only for voices 1 and 2
    if (voiceNumber == 1 || voiceNumber == 2)
    {
        uint8_t midiVoiceId = (voiceNumber == 1) ? 0 : 1;
        midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Filter, state.filterCutoff);
        midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Attack, state.attackTimeSeconds);
        midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Decay, state.decayTimeSeconds);
        midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Octave, state.octaveOffset);
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

    // Apply AS5600 base values only for voices 1/2 (no mapping for 3/4 by design)
    if (voiceNumber == 1 || voiceNumber == 2)
    {
        applyAS5600BaseValues(activeVoiceState, (voiceNumber == 2) ? 1 : 0);
    }

    // Update synth hardware for immediate audio feedback using the per-voice function
    updateVoiceParametersForVoice(*activeVoiceState, voiceNumber);

    //Serial.print("Applied immediate voice updates for step ");
    //Serial.println(stepIndex);
}

//  This gets called every 16th note
void onStepCallback(uint32_t uClockCurrentStep)
{
    currentSequencerStep = static_cast<uint8_t>(uClockCurrentStep); // Raw uClock step, sequencers handle their own modulo

    // 2. Advance sequencers and get their new state into local temporary variables.
    // Extend to four voices; distance sensor assigned to currently selected voice only
    VoiceState tempState1, tempState2, tempState3, tempState4;

    // Route distance sensor to the currently selected voice (1..4); others disabled (-1)
    int v1Distance = (uiState.selectedVoiceIndex == 0) ? mm : -1;
    int v2Distance = (uiState.selectedVoiceIndex == 1) ? mm : -1;
    int v3Distance = (uiState.selectedVoiceIndex == 2) ? mm : -1;
    int v4Distance = (uiState.selectedVoiceIndex == 3) ? mm : -1;

    seq1.advanceStep(uClockCurrentStep, v1Distance, uiState, &tempState1);
    seq2.advanceStep(uClockCurrentStep, v2Distance, uiState, &tempState2);
    seq3.advanceStep(uClockCurrentStep, v3Distance, uiState, &tempState3);
    seq4.advanceStep(uClockCurrentStep, v4Distance, uiState, &tempState4);

    // 3. Apply AS5600 base values per voice (only velocity/filter/attack/decay are affected)
    applyAS5600BaseValues(&tempState1, 0);
    applyAS5600BaseValues(&tempState2, 1);
    // Voices 3/4 currently share no AS5600 mapping; leave as-is

    // Apply AS5600 base values to global delay effect parameters
    applyAS5600DelayValues();

    // 4. Update synth hardware (voices 1/2 with gates + MIDI; 3/4 audio only)
    VoiceState tempStates[] = {tempState1, tempState2, tempState3, tempState4};

    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++)
    {
        if (i < 2) // Voices 0 and 1 (1-based: 1 and 2) have gate support
        {
            updateVoiceParametersForVoice(tempStates[i], i + 1, true,
                                          &voiceSystem.getGate(i),
                                          &voiceSystem.getGateTimer(i));
        }
        else // Voices 2 and 3 (1-based: 3 and 4) are audio only
        {
            updateVoiceParametersForVoice(tempStates[i], i + 1, false);
        }

        // Store state
        voiceSystem.getVoiceState(i) = tempStates[i];
    }
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
    float processedOutput;

    // Determine target gains based on delay state
    // float targetDelayOutputGain = uiState.delayOn ? 1.0f : 0.0f;
    // float targetFeedbackGain = uiState.delayOn ? feedbackAmmount : 0.0f;

    // Smooth parameters once per buffer to reduce CPU load
    //  currentFeedbackGain = delayTimeSmoothing(currentFeedbackGain, targetFeedbackGain, FEEDBACK_FADE_RATE);
    // currentDelayOutputGain = delayTimeSmoothing(currentDelayOutputGain, targetDelayOutputGain, FEEDBACK_FADE_RATE);
    // currentDelay = delayTimeSmoothing(currentDelay, delayTarget, 0.0001f);

    // Set delay time once per buffer for efficiency
    //  del1.SetDelay(currentDelay);

    // Process each sample in the buffer
    for (int i = 0; i < N; ++i)
    {
        // Process all voices through VoiceManager (voice states updated by sequencer callbacks)
        finalVoiceOutput = voiceManager->processAllVoices();

        // Apply global delay effect
        //  processedOutput = processDelayEffect(finalVoiceOutput);

        // Apply soft limiting to prevent clipping
        float softLimitedOutput = daisysp::SoftLimit(finalVoiceOutput);

        // Output to stereo channels with gain reduction
        // Optimization: Convert sample once for both channels since it's mono output
        int16_t convertedSample = convertSampleToInt16(softLimitedOutput);
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
float processDelayEffect(float inputSignal)
{
    // Read current delay output
    float delayOutput = del1.Read();

    // Calculate feedback signal with current gain
    float feedbackSignal = delayOutput * currentFeedbackGain;

    // Apply low-pass filtering to feedback to prevent harsh artifacts
    delLowPass.Process(feedbackSignal);
    float filteredFeedback = delLowPass.Low();

    // Write to delay line: dry input + filtered feedback (clamped at 75%)
    del1.Write(inputSignal + (filteredFeedback * 0.75f));

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

    // Initialize lightweight debug system
    Debug::begin(115200);
    Debug::setEnabled(false);
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
    // Process MIDI input/output
    usb_midi.read();

    unsigned long currentMillis = millis();
    pollUIHeldButtons(uiState, seq1, seq2);

    // =======================
    //   TIMING AND SEQUENCER PROCESSING
    // =======================
    // Process all pending PPQN ticks
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

    // =======================
    //   UI UPDATE TIMING CONTROL
    // =======================
    static unsigned long lastLEDUpdate = 0;
    static unsigned long lastControlUpdate = 0;

    const unsigned long LED_UPDATE_INTERVAL = 10;    // 10ms interval for LED updates
    const unsigned long CONTROL_UPDATE_INTERVAL = 2; // 2ms interval for sensor polling
    //uint16_t currentTouchedButtons = touchSensor.touched();

    // =======================
    //   SENSOR AND CONTROL INPUT PROCESSING
    // =======================
    // Button matrix, distance sensor, and AS5600 polling (500Hz update rate)
    if ((currentMillis - lastControlUpdate >= CONTROL_UPDATE_INTERVAL))
    {
        lastControlUpdate = currentMillis;

    // Check if any parameter buttons are held for real-time recording
    bool parameterRecordingActive = isAnyParameterButtonHeld(uiState);

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
            mm = rawDistanceValue - MIN_HEIGHT;
        }
        else
        {
            mm = 0; // Invalid reading - use safe default
        }
    }

    // Update LEDs and display at controlled intervals (100Hz update rate)
    if (currentMillis - lastLEDUpdate >= LED_UPDATE_INTERVAL)
    {
        lastLEDUpdate = currentMillis;

    // =======================
    //   DISPLAY AND LED PROCESSING
    // =======================
    // Handle voice switch display updates
    if (uiState.voiceSwitchTriggered)
    {
        uiState.voiceSwitchTriggered = false; // Clear the trigger flag
        display.onVoiceSwitched(uiState, voiceManager.get());
    }

        // Update step sequence LEDs
        updateStepLEDs(ledMatrix, seq1, seq2, seq3, seq4, uiState, mm);

        // Update OLED display
        display.update(uiState, seq1, seq2, seq3, seq4, voiceManager.get());

        // Update control parameter LEDs
        updateControlLEDs(ledMatrix, uiState);

        // Apply LED updates to hardware
        ledMatrix.show();
    }

    // =======================
    //   REAL-TIME PARAMETER RECORDING
    // =======================
    // Apply distance sensor values to selected step when parameter buttons are held
    if (uiState.selectedStepForEdit != -1)
    {
        updateParametersForStep(uiState.selectedStepForEdit);
    }
}
