#ifndef MATRIX_H
#define MATRIX_H

#include <Adafruit_MPR121.h>
#include <Arduino.h>
#include <OneButton.h>

/**
 * @brief Modular 32-button matrix scanning, debouncing, and event dispatch.
 * This module is self-contained and can be reused in any project with a compatible matrix.
 */

#define MATRIX_BUTTON_COUNT 32

extern const uint8_t MATRIX_ROW_INPUTS[4];
extern const uint8_t MATRIX_COL_INPUTS[8];

typedef struct {
    uint8_t rowInput;
    uint8_t colInput;
} MatrixButton;

typedef enum {
    MATRIX_BUTTON_PRESSED,
    MATRIX_BUTTON_RELEASED
} MatrixButtonEventType;

typedef struct {
    uint8_t buttonIndex;
    MatrixButtonEventType type;
} MatrixButtonEvent;

void Matrix_init(Adafruit_MPR121 *sensor);
void Matrix_scan();
bool Matrix_getButtonState(uint8_t idx);
void Matrix_setEventHandler(void (*handler)(const MatrixButtonEvent &));
void Matrix_setRisingEdgeHandler(void (*handler)(uint8_t buttonIndex));
void Matrix_printState();

#endif // MATRIX_H
