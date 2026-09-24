#include "ControlIO.h"
#include "AppState.h"
#include "StepPlayback.h"
#include "../../includes.h"
#include "../ui/ControlSurfaceLogic.h"
#include "../utils/FreezeWatchdog.h"

// Core 0 control slices: 1 ms hands/sensors, ~13 ms LEDs, ~40 ms OLED.
// The OLED only ships changed pages and LEDs go via PIO+DMA, so the fast
// cadences stay cheap and the groove never waits on feedback.

namespace
{
constexpr uint32_t kControlIntervalMs = 1;
// The OLED and LED matrix run on independent cadences. OLED: commitFrame()
// only puts the pages that changed on the bus, so a frame costs a page or two
// of I2C transfer instead of the old full 1 KB push.
constexpr uint32_t kOledIntervalMs = 40; // ~25 fps: readable without hogging I2C
// LEDs: 3x the OLED rate. show() hands the 32-pixel frame to FastLED's PIO+DMA
// driver (~1 ms on the wire, no interrupt blackout), so a fast cadence costs
// Core 0 little. Blends in updateStepLEDs() are per frame, so fades settle
// in a third of the time they did at the shared 40 ms cadence.
constexpr uint32_t kLedIntervalMs = 13; // ~77 fps: fades settle fast, PIO+DMA hides cost
constexpr uint32_t kTileBusFrequencyHz = 100000; // Tiles demand 100 kHz; 400 kHz stalls them
constexpr uint32_t kMainBusFrequencyHz = 400000; // Fast mode (400 kHz); OLED and sensors on Wire
constexpr uint8_t kStartupLedBrightness = 150;
constexpr uint8_t kTouchSensorAddress = 0x5A; // MPR121 32-pad address;
constexpr uint8_t kTouchThreshold = 45;
constexpr uint8_t kReleaseThreshold = 14;

// Program-long hardware objects; UI callbacks borrow them. Core 1 never sees them.
struct ControlHardware
{
    LEDMatrix ledMatrix;
    AlchemyControlBridge alchemyBridge;
    Adafruit_MPR121 touchSensor;
    OLEDDisplay display;
    uint32_t lastControlUpdate = 0;
    uint32_t lastOledUpdate = 0;
    uint32_t lastLedUpdate = 0;
};
ControlHardware controls;

static void printAlchemyTileScanReport()
{
    const AlchemyTiles &tiles = controls.alchemyBridge.tiles();
    Serial.print("[ALCHEMY] tiles found: ");
    Serial.println(tiles.presentTileCount());
    for (int slot = 0; slot < tiles.tileCount(); ++slot)
    {
        const AlchemyTiles::TileInfo &info = tiles.info(slot);
        if (!info.present)
            continue;
        Serial.print("[ALCHEMY]   slot ");
        Serial.print(slot);
        Serial.print(" addr 0x");
        Serial.print(info.address, HEX);
        Serial.print(" type 0x");
        Serial.print(info.identity.typeId, HEX);
        Serial.print(" dataLen ");
        Serial.println(info.identity.dataLen);
    }
    if (!tiles.hasSlider())
    {
        Serial.println("[ALCHEMY] no slider tile - faders and voice selects dead");
    }
    if (tiles.firstSlotOfType(alchemy::kTypeButton4) < 0)
    {
        Serial.println("[ALCHEMY] no button tile - param/utility buttons dead");
    }
}
} // namespace

void ControlIO::beginMainBusAndLeds()
{
    Wire.setSDA(PIN_WIRE_SDA);
    Wire.setSCL(PIN_WIRE_SCL);
    Wire.begin();
    Wire.setClock(kMainBusFrequencyHz);
    Wire.setTimeout(25, true); // Cap a wedged bus at 25 ms so the groove survives

    // Wire1 is tiles-only; the OLED stays on Wire (set up above).
    Wire1.setSDA(PIN_ALCHEMY_WIRE1_SDA);
    Wire1.setSCL(PIN_ALCHEMY_WIRE1_SCL);
    Wire1.begin();
    Wire1.setClock(kTileBusFrequencyHz);
    Wire1.setTimeout(25, true);

    // From here a Core 0 hang/fault reboots in ~2 s with a post-mortem next boot.
    freezeWatchdogArm();
    freezeWatchdogFeed(FW_SETUP_BUS);

    randomSeed(analogRead(A0) + millis());
    controls.ledMatrix.begin(kStartupLedBrightness);
    setupLEDMatrixFeedback();
}

void ControlIO::beginPerformanceSensors()
{
    freezeWatchdogFeed(FW_SETUP_SENSORS);
    if (!distanceSensor.begin())
    {
        Serial.println("[ERROR] Distance sensor initialization failed!");
    }
    else
    {
        Serial.println("Distance sensor initialized successfully");
    }

    // Magnetic encoder (Velocity Encoder board, 0x35): the performer's knob.
    if (!magEncoder.begin())
    {
        Serial.println("[ERROR] TMAG5273 magnetic encoder initialization failed!");
    }
    else
    {
        Serial.println("TMAG5273 magnetic encoder initialized successfully");
    }

    // Encoder base values give the knob somewhere sensible to start from.
    initEncoderBaseValues();
}

void ControlIO::beginTouchPads()
{
    freezeWatchdogFeed(FW_SETUP_MPR121);
    if (!controls.touchSensor.begin(kTouchSensorAddress))
    {
        Serial.println("MPR121 not found, check wiring?");
        while (1)
            ;
    }
    else
    {
        Serial.println("MPR121 found and initialized");
        controls.touchSensor.setAutoconfig(true);

        // Conservative touch/release thresholds: firm taps speak, brushes do not.
        controls.touchSensor.setThresholds(kTouchThreshold, kReleaseThreshold); // touch, release;
    }
}

void ControlIO::beginDisplay()
{
    freezeWatchdogFeed(FW_SETUP_OLED);
    controls.display.begin();
    Serial.println("OLED display initialized");
}

void ControlIO::observeVoiceChanges()
{
    // Register the OLED as the voice-change observer so edits show at once.
    if (voiceManager)
    {
        controls.display.setVoiceManager(voiceManager.get());

        // Mirror voice edits into the OLED via VoiceManager's callback.
        voiceManager->setVoiceUpdateCallback([](uint8_t voiceId, const VoiceState &state)
                                             { controls.display.onVoiceParameterChanged(voiceId, state); });

        Serial.println("OLED display registered as voice parameter observer");
    }
    else
    {
        Serial.println("[ERROR] VoiceManager not initialized - cannot register OLED observer");
    }
}

void ControlIO::beginMatrixAndTiles()
{
    freezeWatchdogFeed(FW_SETUP_MATRIX);
    Matrix_init(&controls.touchSensor);
    Serial.println("Matrix initialized");

    // Force one matrix scan so a stuck pad shows up before the first downbeat.
    Serial.println("Forcing initial matrix scan...");
    Matrix_scan();

    // Alchemy tiles (faders/buttons) live on their own Wire1 bank + GP7 strap.
    // Kept at 100 kHz: 400 kHz stalls transfers on this rig (same rate as the
    // working Pico_DSP_Garden tile sketches).
    freezeWatchdogFeed(FW_SETUP_ALCHEMY);
    pinMode(PIN_ALCHEMY_MODE_SWITCH, INPUT_PULLUP);
    Wire1.setSDA(PIN_ALCHEMY_WIRE1_SDA);
    Wire1.setSCL(PIN_ALCHEMY_WIRE1_SCL);
    Wire1.begin();
    Wire1.setClock(kTileBusFrequencyHz);
    controls.alchemyBridge.setModeSwitchPin(PIN_ALCHEMY_MODE_SWITCH);
    controls.alchemyBridge.begin(Wire1, /*bankB=*/nullptr, millis());
    printAlchemyTileScanReport();

    // Forward pad presses/releases into the shared UI event handler.
    Matrix_setEventHandler([](const MatrixButtonEvent &evt)
                           {
        Serial.print("Matrix event: button ");
        Serial.print(evt.buttonIndex);
        Serial.print(evt.type == MATRIX_BUTTON_PRESSED ? " pressed" : " released");
        Serial.println();
        matrixEventHandler(evt, uiState, AppState::sequencerView); });
}

void ControlIO::pollHeldButtons()
{
    freezeWatchdogFeed(FW_LOOP_HELD_BUTTONS);
    pollUIHeldButtons(uiState, AppState::sequencerView);
}

void ControlIO::scanControls(uint32_t nowMs)
{
    if ((nowMs - controls.lastControlUpdate >= kControlIntervalMs))
    {
        controls.lastControlUpdate = nowMs;
        freezeWatchdogFeed(FW_LOOP_CONTROL);

        // 32 step pads, only when the IRQ says something changed.
        freezeWatchdogMark(FW_LOOP_MATRIX);
        Matrix_scan();

        // Tiles: buttons, voice selects, faders, and the GP7 Param/Utility strap.
        freezeWatchdogMark(FW_LOOP_TILES);
        controls.alchemyBridge.update(nowMs, uiState, AppState::sequencerView);

        // Knob motion for the base-parameter target.
        freezeWatchdogMark(FW_LOOP_ENCODER);
        magEncoder.update();
        updateEncoderBaseValues(uiState);

        // Hand height for live recording; absent hand freezes recording below.
        freezeWatchdogMark(FW_LOOP_DISTANCE);
        distanceSensor.update();
        AppState::performanceInput.observeDistance(distanceSensor.getRawDistanceMm());
        // Arpeggiator mode has no steps to write: the same hand drives the
        // notes' dynamics instead, and losing the hand has to be reported every
        // pass or the arp would keep a stale velocity.
        if (uiState.arp.active())
        {
            uiState.arp.observeDynamics(AppState::performanceInput.handPresent,
                                        AppState::performanceInput.recordingValue());
        }
        // Live recording: a held lane follows the hand every pass (or the selected
        // step in Step Edit); pitch lanes also record on clock steps. No hand: hold.
        else if (AppState::performanceInput.handPresent)
        {
            freezeWatchdogMark(FW_LOOP_RECORD);
            const float hand = AppState::performanceInput.recordingValue();
            const bool everyPass = !isClockRunning || uiState.selectedStepForEdit >= 0;
            for (uint8_t lane = 0; lane < PARAM_ID_COUNT; ++lane)
            {
                const auto id = static_cast<ParamId>(lane);
                // Same lanes advanceStep() records: those with a record button.
                if (uiState.parameterButtonHeld[lane] && CORE_PARAMETERS[lane].recordable &&
                    (everyPass || ControlSurface::recordsBetweenSteps(id)))
                    recordParameter(id, hand);
            }
        }
    }
}

void ControlIO::refreshLeds(uint32_t nowMs)
{
    if (nowMs - controls.lastLedUpdate >= kLedIntervalMs)
    {
        controls.lastLedUpdate = nowMs;
        freezeWatchdogFeed(FW_LOOP_LEDS);

        // Show the step frame, then push it to the matrix.
        updateStepLEDs(controls.ledMatrix, AppState::sequencerView, uiState, AppState::performanceInput.distanceAboveMinimumMm);
        controls.ledMatrix.show();
    }
}

void ControlIO::refreshOled(uint32_t nowMs)
{
    if (nowMs - controls.lastOledUpdate >= kOledIntervalMs)
    {
        controls.lastOledUpdate = nowMs;
        freezeWatchdogFeed(FW_LOOP_OLED);

        // One-shot voice-switch notification, then the regular status frame.
        if (uiState.voiceSwitchTriggered)
        {
            uiState.voiceSwitchTriggered = false; // One-shot: consumed here
            controls.display.onVoiceSwitched(uiState, voiceManager.get());
        }

        controls.display.update(uiState, AppState::sequencerView, voiceManager.get());
    }
}
