#include "Matrix.h"
#include "MatrixResolver.h"
#include "../app/HardwarePins.h"
#include "Arduino.h"

// Matrix.cpp — MPR121 row+column touch grid behind the 32 step pads.
// A pad reads pressed when its row AND column electrodes are both touched;
// MatrixResolver uses the pads already held to reject row/column ghosts.
// ISR only flags change; Matrix_scan() (Core 0 loop) reads + dispatches.

// --- Matrix Mapping Definitions ---
// Physical rows -> MPR121 electrodes 3..0 (reversed by wiring).
const uint8_t MATRIX_ROW_INPUTS[4] = {3, 2, 1, 0};
// Physical columns -> MPR121 electrodes 4..11.
const uint8_t MATRIX_COL_INPUTS[8] = {4, 5, 6, 7, 8, 9, 10, 11};

// Linear pad -> (row, col) electrodes; resolved held pads (bit = pad index).
static MatrixButton matrixButtons[MATRIX_BUTTON_COUNT];
static uint32_t heldPads = 0;
// MPR121 instance (set in Matrix_init) + UI dispatch callback.
static Adafruit_MPR121 *mpr121 = nullptr;
static void (*eventHandler)(const MatrixButtonEvent &) = nullptr;

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

// Fold the 12 electrode bits into physical row (0..3) and column (0..7) masks.
static void splitTouchBits(uint16_t touchBits, uint8_t &rowMask, uint8_t &colMask)
{
    rowMask = 0;
    colMask = 0;
    for (uint8_t row = 0; row < 4; ++row)
        if (touchBits & (1u << MATRIX_ROW_INPUTS[row]))
            rowMask |= static_cast<uint8_t>(1u << row);
    for (uint8_t col = 0; col < 8; ++col)
        if (touchBits & (1u << MATRIX_COL_INPUTS[col]))
            colMask |= static_cast<uint8_t>(1u << col);
}

// Bind the sensor, build the pad map, arm the GP8 interrupt.
// No Heavy init here: the MPR121 begin() belongs to the caller (setup).
void Matrix_init(Adafruit_MPR121 *sensor)
{
    Serial.println("Matrix_init called");
    mpr121 = sensor;
    setupMatrixMapping();
    heldPads = 0;
    eventHandler = nullptr;
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

    uint8_t rowMask = 0;
    uint8_t colMask = 0;
    splitTouchBits(mpr121->touched(), rowMask, colMask);

    // Resolve against the pads already held: a first pad stays held, so a
    // second finger on another row and column is one new pad, not three.
    // All fingers lifted resolves to no pads, releasing everything.
    const uint32_t pressed = MatrixResolver::resolve(rowMask, colMask, heldPads);
    const uint32_t changed = pressed ^ heldPads;
    if (!changed)
        return;

    // Diff every pad for press/release edges.
    for (uint8_t i = 0; i < MATRIX_BUTTON_COUNT; ++i)
    {
        const uint32_t bit = 1u << i;
        if (!(changed & bit))
            continue;

        const bool isPressed = (pressed & bit) != 0;
        heldPads ^= bit; // Commit first so re-entrant reads agree.

        if (eventHandler)
        {
            MatrixButtonEvent evt = {i, isPressed ? MATRIX_BUTTON_PRESSED : MATRIX_BUTTON_RELEASED};
            eventHandler(evt);
        }
    }
}

// Attach the press/release dispatch (single owner: the UI funnel).
void Matrix_setEventHandler(void (*handler)(const MatrixButtonEvent &))
{
    eventHandler = handler;
}
