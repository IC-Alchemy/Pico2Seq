#include "Matrix.h"
#include "../app/HardwarePins.h"
#include "Arduino.h"

// Matrix.cpp — MPR121 row+column touch grid behind the 32 step pads.
// A pad reads pressed when its row AND column electrodes are both touched.
// ISR only flags change; Matrix_scan() (Core 0 loop) reads + dispatches.

// --- Matrix Mapping Definitions ---
// Physical rows -> MPR121 electrodes 3..0 (reversed by wiring).
const uint8_t MATRIX_ROW_INPUTS[4] = {3, 2, 1, 0};
// Physical columns -> MPR121 electrodes 4..11.
const uint8_t MATRIX_COL_INPUTS[8] = {4, 5, 6, 7, 8, 9, 10, 11};

// Linear pad -> (row, col) electrodes; current finger level per pad.
static MatrixButton matrixButtons[MATRIX_BUTTON_COUNT];
static bool buttonState[MATRIX_BUTTON_COUNT];
// MPR121 instance (set in Matrix_init) + UI dispatch callbacks.
static Adafruit_MPR121 *mpr121 = nullptr;
static void (*eventHandler)(const MatrixButtonEvent &) = nullptr;
static void (*risingEdgeHandler)(uint8_t buttonIndex) = nullptr;

// The MPR121 INT output is active-low and open-drain. The ISR only records
// that a status change occurred; the control loop performs the I2C read and
// dispatches callbacks outside interrupt context.
static volatile bool mpr121InterruptPending = false;

static void onMpr121Interrupt()
{
    mpr121InterruptPending = true;
}

static bool consumeMpr121Interrupt()
{
    noInterrupts();
    const bool pending = mpr121InterruptPending;
    mpr121InterruptPending = false;
    interrupts();
    return pending;
}

// Build the linear pad -> electrode table (row-major: pad = row*8+col).
static void setupMatrixMapping()
{
    uint8_t idx = 0;
    // Iterate through each row and column to populate the matrixButtons array.
    for (uint8_t row = 0; row < 4; ++row)
    {
        for (uint8_t col = 0; col < 8; ++col)
        {
            // Assign the corresponding MPR121 input pins for the current row and column.
            matrixButtons[idx].rowInput = MATRIX_ROW_INPUTS[row];
            matrixButtons[idx].colInput = MATRIX_COL_INPUTS[col];
            ++idx; // Move to the next button index.
        }
    }
}

// A pad is pressed only when both its row and column electrodes sense touch.
// (One finger bridges the row/col pair at that crossing.)

static bool scanMatrixButton(const MatrixButton &btn, uint16_t touchBits)
{
    return (touchBits & (1 << btn.rowInput)) &&
           (touchBits & (1 << btn.colInput));
}

// Diff all pads against last state; dispatch press/release edges on change.

static void updateButtonStates(uint16_t touchBits)
{
    for (uint8_t i = 0; i < MATRIX_BUTTON_COUNT; ++i)
    {
        bool prev = buttonState[i];
        bool curr = scanMatrixButton(matrixButtons[i], touchBits);
        if (curr != prev)
        {
            buttonState[i] = curr;
            if (curr && risingEdgeHandler)
            {
                risingEdgeHandler(i);
            }
            // Rise-then-press ordering: rising edge first, then the event.
            if (eventHandler)
            {
                MatrixButtonEvent evt;
                evt.buttonIndex = i;
                evt.type = curr ? MATRIX_BUTTON_PRESSED : MATRIX_BUTTON_RELEASED;
                eventHandler(evt);
            }
        }
    }
}

// Bind the sensor, build the pad map, arm the GP8 interrupt.
// No Heavy init here: the MPR121 begin() belongs to the caller (setup).
void Matrix_init(Adafruit_MPR121 *sensor)
{
    Serial.println("Matrix_init called");
    mpr121 = sensor;
    setupMatrixMapping();
    memset(buttonState, 0, sizeof(buttonState));
    eventHandler = nullptr;
    risingEdgeHandler = nullptr;
    mpr121InterruptPending = false;

    if (mpr121)
    {
        // GP8 uses its internal pull-up for the MPR121's open-drain, active-low
        // interrupt. Reading touched() in Matrix_scan() clears the MPR121 IRQ.
        pinMode(PIN_MPR121_INT, INPUT_PULLUP);
        mpr121InterruptPending = true; // Seed one scan so boot touches register.
        attachInterrupt(digitalPinToInterrupt(PIN_MPR121_INT), onMpr121Interrupt, FALLING);
        Serial.println("MPR121 pointer is valid in Matrix_init");
    }
    else
    {
        Serial.println("ERROR: MPR121 pointer is NULL in Matrix_init!");
    }
}

// Scans the matrix only after the MPR121 signals a touch-status change on
// GP8. The ISR deliberately does no I2C work, serial output, or UI dispatch.
void Matrix_scan()
{
    if (!mpr121 || !consumeMpr121Interrupt())
    {
        // Quiet fast path: most loop passes have no touch change.
        return;
    }

    uint16_t touchBits = mpr121->touched();

    // All fingers lifted: release every stuck pad so no gate hangs on.
    if (touchBits == 0)
    {
        // Check if any button was previously pressed and needs a release event.
        for (uint8_t i = 0; i < MATRIX_BUTTON_COUNT; ++i)
        {
            if (buttonState[i])
            {
                buttonState[i] = false;
                if (eventHandler)
                {
                    MatrixButtonEvent evt = {i, MATRIX_BUTTON_RELEASED};
                    eventHandler(evt);
                }
            }
        }
        return;
    }

    // Some electrodes touched: diff every pad for press/release edges.
    for (uint8_t i = 0; i < MATRIX_BUTTON_COUNT; ++i)
    {
        bool isPressed = scanMatrixButton(matrixButtons[i], touchBits);
        bool wasPressed = buttonState[i];

        if (isPressed != wasPressed)
        {
            buttonState[i] = isPressed; // Commit first so re-entrant reads agree.
            Serial.printf("Button %d state changed to: %s\n", i, isPressed ? "PRESSED" : "RELEASED");

            if (eventHandler)
            {
                MatrixButtonEvent evt = {i, isPressed ? MATRIX_BUTTON_PRESSED : MATRIX_BUTTON_RELEASED};
                eventHandler(evt);
            }

            if (isPressed && risingEdgeHandler)
            {
                risingEdgeHandler(i);
            }
        }
    }
}

// Level read for pad idx (out-of-range reads as released, never crashes).
bool Matrix_getButtonState(uint8_t idx)
{
    if (idx >= MATRIX_BUTTON_COUNT)
        return false;
    return buttonState[idx];
}

// Attach the press/release dispatch (single owner: the UI funnel).
void Matrix_setEventHandler(void (*handler)(const MatrixButtonEvent &))
{
    eventHandler = handler;
}

// Attach the press-only dispatch (optional secondary listener).
void Matrix_setRisingEdgeHandler(void (*handler)(uint8_t buttonIndex))
{
    risingEdgeHandler = handler;
}

// Serial debug dump of the 4x8 pad grid (1 = touched).
void Matrix_printState()
{
    Serial.println("Button Matrix State (1=pressed, 0=not pressed):");
    for (uint8_t row = 0; row < 4; ++row)
    {
        for (uint8_t col = 0; col < 8; ++col)
        {
            uint8_t idx = row * 8 + col;
            Serial.print(buttonState[idx] ? "1 " : "0 ");
        }
        Serial.println();
    }
    Serial.println();
}
