#ifndef MATRIX_H
#define MATRIX_H

#include <Adafruit_MPR121.h>
#include <Arduino.h>
#include <OneButton.h>

/**
 * @brief 32-pad MPR121 touch step grid (Core 0, interrupt-gated).
 *
 * Pads are the step sequencer: tap toggles a gate, hold opens step-edit
 * (see UIEventHandler). The MPR121 IRQ (GP8, active-low) only flags change;
 * Matrix_scan() does the I2C read + dispatch in the control loop, never in
 * the ISR. Reusable: needs only an MPR121 on Wire. See docs/matrix.md.
 */

#define MATRIX_BUTTON_COUNT 32

extern const uint8_t MATRIX_ROW_INPUTS[4];
extern const uint8_t MATRIX_COL_INPUTS[8];

typedef struct {
    uint8_t rowInput; // MPR121 electrode for this row (0..3)
    uint8_t colInput; // MPR121 electrode for this column (4..11)
} MatrixButton;

typedef enum {
    MATRIX_BUTTON_PRESSED,  // finger touched the pad
    MATRIX_BUTTON_RELEASED  // finger left the pad
} MatrixButtonEventType;

typedef struct {
    uint8_t buttonIndex;        // linear pad 0..31 (bank = index/16, step = index%16)
    MatrixButtonEventType type; // press or release edge
} MatrixButtonEvent;

// Bind the MPR121 and arm its GP8 interrupt; reads the initial pad state.
void Matrix_init(Adafruit_MPR121 *sensor);
// IRQ-gated scan: no-op until the ISR flags a change, then reads + dispatches.
void Matrix_scan();
// Press/release callback: the single dispatch, normally matrixEventHandler.
void Matrix_setEventHandler(void (*handler)(const MatrixButtonEvent &));

#endif // MATRIX_H
