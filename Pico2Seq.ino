#include "includes.h"
#include "diagnostic.h"
#include "src/utils/Debug.h"
#include "src/scales/scales.h"

// =======================
//   CV / GATE PINS
// =======================
#define CV1_PIN 2
#define GATE1_PIN 3
#define CV2_PIN 4
#define GATE2_PIN 5
#define CV3_PIN 6
#define GATE3_PIN 7
#define CV4_PIN 8
#define GATE4_PIN 9


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
//   HARDWARE INTERFACE CONSTANTS
// =======================
constexpr int MAX_HEIGHT = SensorConstants::DistanceSensor::MAX_DISTANCE_HEIGHT_MM;
constexpr int MIN_HEIGHT = SensorConstants::DistanceSensor::MIN_DISTANCE_HEIGHT_MM;
constexpr int PIN_TOUCH_IRQ = 10;
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
void updateParametersForStep(uint8_t stepToUpdate);
void onStepCallback(uint32_t uClockCurrentStep);
void setup();
void setup1();
void loop();
void loop1();

// =======================
//   SEQUENCER STATE VARIABLES
// =======================
volatile uint8_t currentSequencerStep = 0;

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

// --- Clock Callbacks ---
void onSync24Callback(uint32_t tick)
{
    usb_midi.sendRealTime(midi::Clock);
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

    isClockRunning = false;
    Serial.println("[uClock] onClockStop()");
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
        Serial.print("  -> Set ");
        Serial.print(CORE_PARAMETERS[static_cast<int>(heldMapping->paramId)].name);
        Serial.print(" to ");
        Serial.println(valueToSet);
    }

}

VoiceState voiceState1;
VoiceState voiceState2;
VoiceState voiceState3;
VoiceState voiceState4;

void onStepCallback(uint32_t uClockCurrentStep)
{
    currentSequencerStep = static_cast<uint8_t>(uClockCurrentStep); // Raw uClock step, sequencers handle their own modulo

    // Advance sequencers and get their new state into local temporary variables.
    VoiceState tempState1, tempState2, tempState3, tempState4;

    int v1Distance = (uiState.selectedVoiceIndex == 0) ? mm : -1;
    int v2Distance = (uiState.selectedVoiceIndex == 1) ? mm : -1;
    int v3Distance = (uiState.selectedVoiceIndex == 2) ? mm : -1;
    int v4Distance = (uiState.selectedVoiceIndex == 3) ? mm : -1;

    seq1.advanceStep(uClockCurrentStep, v1Distance, uiState, &tempState1);
    seq2.advanceStep(uClockCurrentStep, v2Distance, uiState, &tempState2);
    seq3.advanceStep(uClockCurrentStep, v3Distance, uiState, &tempState3);
    seq4.advanceStep(uClockCurrentStep, v4Distance, uiState, &tempState4);

    // applyAS5600BaseValues(&tempState1, 0);
    // applyAS5600BaseValues(&tempState2, 1);

    // Update CV and Gate outputs
    // The noteIndex is a float from 0.0 to 47.0. We scale it by 5 to get a value between 0 and 235,
    // which is within the 8-bit PWM range of 0-255.
    analogWrite(CV1_PIN, tempState1.noteIndex * 5);
    digitalWrite(GATE1_PIN, tempState1.isGateHigh ? HIGH : LOW);

    analogWrite(CV2_PIN, tempState2.noteIndex * 5);
    digitalWrite(GATE2_PIN, tempState2.isGateHigh ? HIGH : LOW);

    analogWrite(CV3_PIN, tempState3.noteIndex * 5);
    digitalWrite(GATE3_PIN, tempState3.isGateHigh ? HIGH : LOW);

    analogWrite(CV4_PIN, tempState4.noteIndex * 5);
    digitalWrite(GATE4_PIN, tempState4.isGateHigh ? HIGH : LOW);

    // Store states
    voiceState1 = tempState1;
    voiceState2 = tempState2;
    voiceState3 = tempState3;
    voiceState4 = tempState4;
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
    // Core 0 setup
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
        touchSensor.setThresholds(50, 25); // touch, release thresholds
        // Serial.println("MPR121 thresholds configured to 155/55");
    }


    // Initialize OLED display
    display.begin();
    Serial.println("OLED display initialized");

    // CV/Gate Pin setup
    pinMode(CV1_PIN, OUTPUT);
    pinMode(GATE1_PIN, OUTPUT);
    pinMode(CV2_PIN, OUTPUT);
    pinMode(GATE2_PIN, OUTPUT);
    pinMode(CV3_PIN, OUTPUT);
    pinMode(GATE3_PIN, OUTPUT);
    pinMode(CV4_PIN, OUTPUT);
    pinMode(GATE4_PIN, OUTPUT);


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
    // Core 0 loop
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
    pollUIHeldButtons(uiState, seq1, seq2);

    unsigned long currentMillis = millis();

    // Check if any parameter buttons are held for real-time recording
    bool parameterRecordingActive = isAnyParameterButtonHeld(uiState);

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
    }

    // =======================
    //   UI UPDATE TIMING CONTROL
    // =======================
    static unsigned long lastLEDUpdate = 0;
    static unsigned long lastControlUpdate = 0;

    const unsigned long LED_UPDATE_INTERVAL = 10;    // 10ms interval for LED updates
    const unsigned long CONTROL_UPDATE_INTERVAL = 2; // 2ms interval for sensor polling
    uint16_t currentTouchedButtons = touchSensor.touched();

    // =======================
    //   SENSOR AND CONTROL INPUT PROCESSING
    // =======================
    // Button matrix, distance sensor, and AS5600 polling (500Hz update rate)
    if ((currentMillis - lastControlUpdate >= CONTROL_UPDATE_INTERVAL))
    {
        lastControlUpdate = currentMillis;

        // Scan button matrix for user input
        Matrix_scan();

        // Update AS5600 magnetic encoder for base parameter control
        as5600Sensor.update();

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

    // =======================
    //   DISPLAY AND LED PROCESSING
    // =======================
    // Handle voice switch display updates
    if (uiState.voiceSwitchTriggered)
    {
        uiState.voiceSwitchTriggered = false; // Clear the trigger flag
    }

    // Update LEDs and display at controlled intervals (100Hz update rate)
    if (currentMillis - lastLEDUpdate >= LED_UPDATE_INTERVAL)
    {
        lastLEDUpdate = currentMillis;

        // Update step sequence LEDs
        updateStepLEDs(ledMatrix, seq1, seq2, seq3, seq4, uiState, mm);

        // Update OLED display
        display.update(uiState, seq1, seq2, seq3, seq4);

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
