#include "Matrix.h"
#include "Arduino.h"

// --- Matrix Mapping Definitions ---
// Define the mapping of physical matrix rows to MPR121 electrode inputs.
// Tip: Consider using named constants or an enum for row/column counts
// instead of magic numbers (4 and 8) for better readability and maintainability.
const uint8_t MATRIX_ROW_INPUTS[4] = {3, 2, 1, 0};
// Define the mapping of physical matrix columns to MPR121 electrode inputs.
const uint8_t MATRIX_COL_INPUTS[8] = {4, 5, 6, 7, 8, 9, 10, 11};

// Array to store the mapping of each button index to its corresponding row and column inputs.
static MatrixButton matrixButtons[MATRIX_BUTTON_COUNT];
// Array to store the current state (pressed/released) of each button.
static bool buttonState[MATRIX_BUTTON_COUNT];
// Pointer to the Adafruit_MPR121 sensor instance.
static Adafruit_MPR121 *mpr121 = nullptr;
// Function pointer for the generic button event handler (pressed or released).
static void (*eventHandler)(const MatrixButtonEvent &) = nullptr;
// Function pointer for the rising edge (button press) specific handler.
static void (*risingEdgeHandler)(uint8_t buttonIndex) = nullptr;

// Sets up the mapping between linear button indices and matrix row/column inputs.
static void setupMatrixMapping() {
    uint8_t idx = 0;
    // Iterate through each row and column to populate the matrixButtons array.
    for (uint8_t row = 0; row < 4; ++row) {
        for (uint8_t col = 0; col < 8; ++col) {
            // Assign the corresponding MPR121 input pins for the current row and column.
            matrixButtons[idx].rowInput = MATRIX_ROW_INPUTS[row];
            matrixButtons[idx].colInput = MATRIX_COL_INPUTS[col];
            ++idx; // Move to the next button index.
        }
    }
}

// Scans a single matrix button to determine its state based on the touch bits from the MPR121.
// A button is considered pressed if both its row and column inputs are touched.

static bool scanMatrixButton(const MatrixButton &btn, uint16_t touchBits) {
    return (touchBits & (1 << btn.rowInput)) &&
           (touchBits & (1 << btn.colInput));
}

// Updates the state of all buttons based on the latest touch bits from the sensor.
// It also triggers event handlers if a button's state changes.

static void updateButtonStates(uint16_t touchBits) {
    for (uint8_t i = 0; i < MATRIX_BUTTON_COUNT; ++i) {
        bool prev = buttonState[i]; // Get the previous state of the button.
        bool curr = scanMatrixButton(matrixButtons[i], touchBits); // Get the current state.
        // Check if the button state has changed.
        if (curr != prev) {
            buttonState[i] = curr; // Update the button state.
            // If the button is now pressed and a rising edge handler is set, call it.
            if (curr && risingEdgeHandler) {
                risingEdgeHandler(i);
            }
            // If a generic event handler is set, call it with the button event details.
            if (eventHandler) {
                MatrixButtonEvent evt;
                evt.buttonIndex = i;
                evt.type = curr ? MATRIX_BUTTON_PRESSED : MATRIX_BUTTON_RELEASED;
                eventHandler(evt);
            }
        }
    }
}

// Initializes the Matrix module.
// Assigns the MPR121 sensor instance and sets up the button mapping.
// Initializes all button states to false (not pressed).
// Tip: Add error handling to check if the sensor initialization was successful before proceeding.
void Matrix_init(Adafruit_MPR121 *sensor) {
    Serial.println("Matrix_init called");
    mpr121 = sensor;
    setupMatrixMapping();
    memset(buttonState, 0, sizeof(buttonState));
    eventHandler = nullptr; // Initialize event handlers to null.
    risingEdgeHandler = nullptr;
    
    if (mpr121) {
        Serial.println("MPR121 pointer is valid in Matrix_init");
    } else {
        Serial.println("ERROR: MPR121 pointer is NULL in Matrix_init!");
    }
}

// Scans the matrix for button presses and updates the button states.


void Matrix_scan() {
    if (!mpr121) {
        // This check is important, but let's not flood the serial port.
        // A single message at init should be enough.
        return;
    }

    uint16_t touchBits = mpr121->touched();

    // If no electrodes are touched, no buttons can be pressed. Exit early.
    if (touchBits == 0) {
        // Check if any button was previously pressed and needs a release event.
        for (uint8_t i = 0; i < MATRIX_BUTTON_COUNT; ++i) {
            if (buttonState[i]) { // If it was pressed
                buttonState[i] = false; // Update its state to released
                if (eventHandler) {
                    MatrixButtonEvent evt = {i, MATRIX_BUTTON_RELEASED};
                    eventHandler(evt);
                }
            }
        }
        return;
    }

    // If we get here, at least one electrode is touched.
    // Let's check the state of all buttons.
    for (uint8_t i = 0; i < MATRIX_BUTTON_COUNT; ++i) {
        bool isPressed = scanMatrixButton(matrixButtons[i], touchBits);
        bool wasPressed = buttonState[i];

        if (isPressed != wasPressed) {
            buttonState[i] = isPressed;

            // Optional: Add a single, clear debug message for state changes.
            Serial.printf("Button %d state changed to: %s\n", i, isPressed ? "PRESSED" : "RELEASED");

            if (eventHandler) {
                MatrixButtonEvent evt = {i, isPressed ? MATRIX_BUTTON_PRESSED : MATRIX_BUTTON_RELEASED};
                eventHandler(evt);
            }

            if (isPressed && risingEdgeHandler) {
                risingEdgeHandler(i);
            }
        }
    }
}

// Gets the current state of a specific button by its index.
// Returns true if pressed, false otherwise.
bool Matrix_getButtonState(uint8_t idx) {
    if (idx >= MATRIX_BUTTON_COUNT) // Bounds checking.
        return false;
    return buttonState[idx];
}

// Sets the function to be called when any button event (pressed or released) occurs.
void Matrix_setEventHandler(void (*handler)(const MatrixButtonEvent &)) {
    eventHandler = handler;
}

// Sets the function to be called when a button is pressed (rising edge).
void Matrix_setRisingEdgeHandler(void (*handler)(uint8_t buttonIndex)) {
    risingEdgeHandler = handler;
}

// Prints the current state of the entire button matrix to the Serial console for debugging.
void Matrix_printState() {
    Serial.println("Button Matrix State (1=pressed, 0=not pressed):");
    for (uint8_t row = 0; row < 4; ++row) {
        for (uint8_t col = 0; col < 8; ++col) {
            uint8_t idx = row * 8 + col;
            Serial.print(buttonState[idx] ? "1 " : "0 ");
        }
        Serial.println(); // Newline after each row.
    }
    Serial.println(); // Extra newline for readability.
}
