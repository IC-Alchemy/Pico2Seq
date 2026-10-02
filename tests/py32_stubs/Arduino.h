#pragma once
// Arduino.h — host shim for the PY32F030 tile sketches (tiles/*/*.ino).
//
// Enough of the PY32Duino core and its HAL to compile the tile firmware
// unmodified on a dev machine and drive its I2C slave ISR from a test. The
// peripheral is a plain struct the test writes flags into, so a test plays the
// role of the master: raise ADDR, call I2C1_IRQHandler(), take DR, raise TXE,
// and so on. That is the only way to prove things like "TXE and BTF in one
// SR1 snapshot produce exactly one DR write" without hardware.
//
// Nothing here models timing. Sweep cadence, debounce windows and watchdog
// intervals are all driven by the test's own clock (py32::setMillis).

#include <stdint.h>
#include <string.h>

#include <cstddef>

// --- Core types and constants ------------------------------------------------

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_ANALOG 3

// Pin numbers. Values are arbitrary but must be distinct; the sketches only
// ever use them as opaque handles.
enum : uint8_t {
  PA0 = 0, PA1, PA2, PA3, PA4, PA5, PA6, PA7,
  PB5 = 13, PB6, PB7,
  PF0 = 20, PF1,
};

namespace py32 {

inline constexpr int kPinCount = 32;

// Address strap wiring the test is emulating, read through PB5's pulls.
enum class Strap : uint8_t { Gnd, Vcc, Floating };

struct State {
  uint32_t millis = 0;
  uint32_t micros = 0;
  int analog[kPinCount] = {};
  int digital[kPinCount] = {};   // HIGH/LOW for plain input pins
  Strap strap = Strap::Floating;
  bool sdaHeldLow = false;       // what i2cBusRecover() sees on PB7
  uint32_t nvicEnables = 0;
  uint32_t nvicDisables = 0;
  uint8_t uid[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
};

State& state();

inline void setMillis(uint32_t ms) { state().millis = ms; state().micros = ms * 1000u; }
inline void advanceMillis(uint32_t ms) { setMillis(state().millis + ms); }
inline void setDigital(uint8_t pin, int level) { state().digital[pin] = level; }
inline void setAnalog(uint8_t pin, int counts) { state().analog[pin] = counts; }

}  // namespace py32

// --- Core API ----------------------------------------------------------------

inline unsigned long millis() { return py32::state().millis; }
inline unsigned long micros() { return py32::state().micros; }
inline void delay(unsigned long ms) { py32::advanceMillis(static_cast<uint32_t>(ms)); }
inline void delayMicroseconds(unsigned long) {}
inline void noInterrupts() {}
inline void interrupts() {}
inline void analogReadResolution(int) {}
inline void randomSeed(unsigned long) {}

void pinMode(uint8_t pin, uint8_t mode);
int digitalRead(uint8_t pin);
inline int analogRead(uint8_t pin) { return py32::state().analog[pin]; }

// --- Peripheral register models ----------------------------------------------

/**
 * DR models the one thing a plain uint32_t cannot: reading the data register
 * clears RXNE (and BTF behind it) on the part. The slave ISR's receive drain
 * loops until the peripheral reports empty, so a DR that never clears its flag
 * turns one received byte into three. There is exactly one I2C1, so the
 * accessor reaches the register file directly.
 */
class DataRegister {
 public:
  operator uint32_t() const;  // clears RXNE/BTF, like a real DR read
  DataRegister& operator=(uint32_t v) { value_ = v; return *this; }
  [[nodiscard]] uint32_t peek() const { return value_; }

 private:
  volatile uint32_t value_ = 0;
};

struct I2C_TypeDef {
  volatile uint32_t CR1, CR2, OAR1, OAR2;
  DataRegister DR;
  volatile uint32_t SR1, SR2, CCR, TRISE;
};
struct GPIO_TypeDef {
  volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFR[2];
};
struct IWDG_TypeDef {
  volatile uint32_t KR, PR, RLR, SR;
};

extern I2C_TypeDef* const I2C1;
extern GPIO_TypeDef* const GPIOA;
extern GPIO_TypeDef* const GPIOB;
extern GPIO_TypeDef* const GPIOF;
extern IWDG_TypeDef* const IWDG;

// SR1 / SR2 / CR1 / CR2 bits, I2Cv1 layout.
#define I2C_SR1_SB     (1u << 0)
#define I2C_SR1_ADDR   (1u << 1)
#define I2C_SR1_BTF    (1u << 2)
#define I2C_SR1_STOPF  (1u << 4)
#define I2C_SR1_RXNE   (1u << 6)
#define I2C_SR1_TXE    (1u << 7)
#define I2C_SR1_BERR   (1u << 8)
#define I2C_SR1_ARLO   (1u << 9)
#define I2C_SR1_AF     (1u << 10)
#define I2C_SR1_OVR    (1u << 11)

#define I2C_SR2_MSL    (1u << 0)
#define I2C_SR2_BUSY   (1u << 1)
#define I2C_SR2_TRA    (1u << 2)

#define I2C_CR1_PE     (1u << 0)
#define I2C_CR1_ACK    (1u << 10)
#define I2C_CR2_ITERREN (1u << 8)
#define I2C_CR2_ITEVTEN (1u << 9)
#define I2C_CR2_ITBUFEN (1u << 10)

// The digital-filter register is deliberately NOT defined: the tile guards its
// use with #if defined(I2C_FLTR_DNF), and leaving it undefined here exercises
// the branch that has to keep working on parts without it.

#define GPIO_PIN_6 (1u << 6)
#define GPIO_PIN_7 (1u << 7)
#define GPIO_PIN_RESET 0
#define GPIO_PIN_SET 1

#define GPIO_MODE_OUTPUT_OD 1
#define GPIO_MODE_AF_OD 2
#define GPIO_NOPULL 0
#define GPIO_SPEED_FREQ_HIGH 3
#define GPIO_AF6_I2C 6

#define I2C_DUTYCYCLE_2 0
#define I2C_GENERALCALL_DISABLE 0
#define I2C_NOSTRETCH_DISABLE 0

typedef struct {
  uint32_t Pin, Mode, Pull, Speed, Alternate;
} GPIO_InitTypeDef;

typedef struct {
  uint32_t ClockSpeed, DutyCycle, OwnAddress1, GeneralCallMode, NoStretchMode;
} I2C_InitTypeDef;

typedef struct {
  I2C_TypeDef* Instance;
  I2C_InitTypeDef Init;
} I2C_HandleTypeDef;

typedef enum { I2C1_IRQn = 0 } IRQn_Type;

#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_I2C_CLK_ENABLE() ((void)0)
#define __HAL_RCC_I2C_FORCE_RESET() py32HalI2cForceReset()
#define __HAL_RCC_I2C_RELEASE_RESET() ((void)0)

void py32HalI2cForceReset();
void HAL_GPIO_Init(GPIO_TypeDef* port, GPIO_InitTypeDef* init);
void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint32_t pins, uint32_t value);
uint32_t HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint32_t pin);
void HAL_I2C_Init(I2C_HandleTypeDef* handle);
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t pre, uint32_t sub);
void HAL_NVIC_ClearPendingIRQ(IRQn_Type irq);
void HAL_NVIC_EnableIRQ(IRQn_Type irq);
void HAL_NVIC_DisableIRQ(IRQn_Type irq);
