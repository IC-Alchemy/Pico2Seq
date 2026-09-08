#include "ControlIO.h"
#include "AppState.h"
#include "StepPlayback.h"
#include "../../includes.h"
#include "../FeatureConfig.h" // PICO2SEQ_I2C_FASTMODE
#include "../utils/FreezeWatchdog.h"

namespace
{
constexpr uint32_t kControlIntervalMs = 1;
constexpr uint32_t kDisplayIntervalMs = 20; // 50 frames/s for OLED and LEDs
constexpr uint32_t kTileBusFrequencyHz = 100000; // This panel stalls at 400 kHz.
constexpr uint8_t kStartupLedBrightness = 100;
constexpr uint8_t kTouchSensorAddress = 0x5A;
constexpr uint8_t kTouchThreshold = 55;
constexpr uint8_t kReleaseThreshold = 22;

// Program-long hardware objects: callbacks borrow them; Core 1 never sees them.
struct ControlHardware
{
    LEDMatrix ledMatrix;
    AlchemyControlBridge alchemyBridge;
    Adafruit_MPR121 touchSensor;
    OLEDDisplay display;
    uint32_t lastControlUpdate = 0;
    uint32_t lastDisplayUpdate = 0;
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
#if PICO2SEQ_I2C_FASTMODE
    // 400 kHz fast mode is opt-in (default OFF): the 2026-09-07 bench run
    // showed constant OLED glitches and freezes with fast mode on this rig,
    // matching the Wire1 tile-bank finding (400 kHz stalls transfers). The
    // OLED library still runs its own frame pushes at 400 kHz (its preclk
    // default) as it always has; this setting only extends fast mode to the
    // idle/sensor traffic between frames. Adafruit_SH110X re-programs the bus
    // to its postclk after every frame, so the durable setting is the postclk
    // constructor argument in OLEDDisplay (src/OLED/oled.cpp); this call
    // covers the window before the first frame push.
    Wire.setClock(400000);
#endif

    // From here on, any Core-0 hang or hard fault reboots within ~2s and the
    // post-mortem prints at the next boot (src/utils/FreezeWatchdog.h).
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

    // Initialize TMAG5273A magnetic encoder (Velocity Encoder board, I2C 0x35)
    if (!magEncoder.begin())
    {
        Serial.println("[ERROR] TMAG5273 magnetic encoder initialization failed!");
    }
    else
    {
        Serial.println("TMAG5273 magnetic encoder initialized successfully");
    }

    // Initialize encoder base values with proper defaults
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

        // Configure MPR121 touch thresholds.
        // Using the original, more conservative thresholds.
        controls.touchSensor.setThresholds(kTouchThreshold, kReleaseThreshold); // touch, release thresholds
        // Serial.println("MPR121 thresholds configured to 155/55");
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
    // Register OLED display as observer for voice parameter changes
    if (voiceManager)
    {
        controls.display.setVoiceManager(voiceManager.get());

        // Use VoiceManager's callback system for parameter updates
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

    // Force a matrix scan to test the system
    Serial.println("Forcing initial matrix scan...");
    Matrix_scan();
    // Matrix_printState();

    // =======================
    //   ALCHEMY TILE CONTROL SURFACE (Wire1 bank + GP7 mode strap)
    // =======================
    // SliderModule + ButtonModule8 live on their own Wire1 bank; Wire1 pin
    // constants are bench-adjustable in includes.h. Standard mode (100 kHz),
    // not fast mode: 400 kHz stalls tile transfers on this rig, which is the
    // rate the working Pico_DSP_Garden sketches run these same tiles at.
    freezeWatchdogFeed(FW_SETUP_ALCHEMY);
    pinMode(PIN_ALCHEMY_MODE_SWITCH, INPUT_PULLUP);
    Wire1.setSDA(PIN_ALCHEMY_WIRE1_SDA);
    Wire1.setSCL(PIN_ALCHEMY_WIRE1_SCL);
    Wire1.begin();
    Wire1.setClock(kTileBusFrequencyHz);
    controls.alchemyBridge.setModeSwitchPin(PIN_ALCHEMY_MODE_SWITCH);
    controls.alchemyBridge.begin(Wire1, /*bankB=*/nullptr, millis());
    printAlchemyTileScanReport();

    // Use a lambda to capture the context needed by the event handler
    Matrix_setEventHandler([](const MatrixButtonEvent &evt)
                           {
        Serial.print("Matrix event: button ");
        Serial.print(evt.buttonIndex);
        Serial.print(evt.type == MATRIX_BUTTON_PRESSED ? " pressed" : " released");
        Serial.println();
        matrixEventHandler(evt, uiState, AppState::sequencers, VoiceSystem::MAX_VOICES, midiNoteManager); });
}

void ControlIO::pollHeldButtons()
{
    freezeWatchdogFeed(FW_LOOP_HELD_BUTTONS);
    pollUIHeldButtons(uiState, seq1, seq2, seq3, seq4);
}

void ControlIO::scanControls(uint32_t nowMs)
{
    if ((nowMs - controls.lastControlUpdate >= kControlIntervalMs))
    {
        controls.lastControlUpdate = nowMs;
        freezeWatchdogFeed(FW_LOOP_CONTROL);

        // Scan button matrix for user input (32 step pads)
        Matrix_scan();

        // Poll the Alchemy tiles (param/utility buttons, voice selects,
        // faders, GP7 mode strap) and translate edges into UI actions.
        controls.alchemyBridge.update(nowMs, uiState, AppState::sequencers, VoiceSystem::MAX_VOICES,
                             midiNoteManager);

        // Update magnetic encoder for base parameter control
        magEncoder.update();
        updateEncoderBaseValues(uiState);

        // Update distance sensor for real-time parameter recording
        distanceSensor.update();
        AppState::performanceInput.observeDistance(distanceSensor.getRawDistanceMm());
        // =======================
        //   REAL-TIME PARAMETER RECORDING
        // =======================
        // Apply distance sensor values to selected step when parameter buttons are held
        if (uiState.selectedStepForEdit != -1)
        {
            updateParametersForStep(uiState.selectedStepForEdit);
        }
    }
}

void ControlIO::refreshDisplays(uint32_t nowMs)
{
    if (nowMs - controls.lastDisplayUpdate >= kDisplayIntervalMs)
    {
        controls.lastDisplayUpdate = nowMs;
        freezeWatchdogFeed(FW_LOOP_DISPLAY);

        // =======================
        //   DISPLAY AND LED PROCESSING
        // =======================
        // Handle voice switch display updates
        if (uiState.voiceSwitchTriggered)
        {
            uiState.voiceSwitchTriggered = false; // Clear the trigger flag
            controls.display.onVoiceSwitched(uiState, voiceManager.get());
        }

        // Update step sequence LEDs
        updateStepLEDs(controls.ledMatrix, seq1, seq2, seq3, seq4, uiState, AppState::performanceInput.distanceAboveMinimumMm);

        // Update OLED display
        controls.display.update(uiState, seq1, seq2, seq3, seq4, voiceManager.get());

        // Apply LED updates to hardware
        controls.ledMatrix.show();
    }
}
