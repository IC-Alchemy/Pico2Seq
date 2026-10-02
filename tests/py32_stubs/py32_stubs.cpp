// Backing store for tests/py32_stubs/Arduino.h.

#include "Arduino.h"

namespace py32 {
State& state() {
  static State s;
  return s;
}
}  // namespace py32

namespace {
I2C_TypeDef gI2c1{};
GPIO_TypeDef gGpioA{}, gGpioB{}, gGpioF{};
IWDG_TypeDef gIwdg{};
uint8_t gPinModes[py32::kPinCount] = {};
}  // namespace

I2C_TypeDef* const I2C1 = &gI2c1;

DataRegister::operator uint32_t() const {
  gI2c1.SR1 &= ~(I2C_SR1_RXNE | I2C_SR1_BTF);
  return value_;
}
GPIO_TypeDef* const GPIOA = &gGpioA;
GPIO_TypeDef* const GPIOB = &gGpioB;
GPIO_TypeDef* const GPIOF = &gGpioF;
IWDG_TypeDef* const IWDG = &gIwdg;

void pinMode(uint8_t pin, uint8_t mode) {
  if (pin < py32::kPinCount) gPinModes[pin] = mode;
}

int digitalRead(uint8_t pin) {
  if (pin != PB5) return py32::state().digital[pin];

  // The strap probe drives PB5's pull through PUPDR and reads the level back.
  // 01 = pull-up, 10 = pull-down, 00 = none.
  const uint32_t pull = (GPIOB->PUPDR >> (PB5 * 2)) & 3u;
  switch (py32::state().strap) {
    case py32::Strap::Gnd: return LOW;   // 1k to GND wins over either pull
    case py32::Strap::Vcc: return HIGH;  // 1k to 3V3 likewise
    case py32::Strap::Floating: return (pull == 1u) ? HIGH : LOW;
  }
  return LOW;
}

void py32HalI2cForceReset() {
  // A peripheral reset clears the register file, exactly as on the part. The
  // tile has to rebuild its own transaction state to match; that it did not
  // used to is the freeze this harness exists to pin down.
  gI2c1.CR1 = 0; gI2c1.CR2 = 0; gI2c1.OAR1 = 0; gI2c1.OAR2 = 0;
  gI2c1.DR = 0; gI2c1.SR1 = 0; gI2c1.CCR = 0; gI2c1.TRISE = 0;
  // SR2 keeps BUSY: a peripheral reset does not free a bus another
  // device is holding, and the tile must cope with coming back to one.
}

void HAL_GPIO_Init(GPIO_TypeDef*, GPIO_InitTypeDef*) {}
void HAL_GPIO_WritePin(GPIO_TypeDef*, uint32_t, uint32_t) {}

uint32_t HAL_GPIO_ReadPin(GPIO_TypeDef*, uint32_t pin) {
  if (pin == GPIO_PIN_7) {
    return py32::state().sdaHeldLow ? GPIO_PIN_RESET : GPIO_PIN_SET;
  }
  return GPIO_PIN_SET;
}

void HAL_I2C_Init(I2C_HandleTypeDef* handle) {
  if (handle == nullptr || handle->Instance == nullptr) return;
  handle->Instance->OAR1 = handle->Init.OwnAddress1;
  handle->Instance->CR1 |= I2C_CR1_PE;
}

void HAL_NVIC_SetPriority(IRQn_Type, uint32_t, uint32_t) {}
void HAL_NVIC_ClearPendingIRQ(IRQn_Type) {}
void HAL_NVIC_EnableIRQ(IRQn_Type) { ++py32::state().nvicEnables; }
void HAL_NVIC_DisableIRQ(IRQn_Type) { ++py32::state().nvicDisables; }
