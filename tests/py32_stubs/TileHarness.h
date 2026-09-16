#pragma once
// TileHarness.h — plays the RP2350 master against a tile sketch's slave ISR.
//
// The sketch under test is #included by the test translation unit, so its
// statics stay private and everything here goes through the same two things
// real hardware offers: the I2C register file and the pins. A transaction is
// driven flag by flag, which is what makes the ISR's own edge cases reachable
// — a master that stops mid-frame, a BTF arriving with TXE already set, a
// peripheral reset in the middle of a read.

#include <cstdint>
#include <vector>

#include "Arduino.h"

// Provided by the sketch under test.
extern "C" void I2C1_IRQHandler(void);

namespace tile {

/** Frame register window, mirrored from the protocol. */
inline constexpr std::uint8_t kRegWhoAmI = 0x00;
inline constexpr std::uint8_t kRegDataLen = 0x19;
inline constexpr std::uint8_t kRegStatus = 0x20;
inline constexpr std::uint8_t kRegData = 0x21;

inline constexpr std::uint8_t kStatusHeartbeat = 0x01;
inline constexpr std::uint8_t kStatusLocalFault = 0x02;
inline constexpr std::uint8_t kStatusNotReady = 0x04;

inline std::uint8_t seqOf(std::uint8_t status) { return (status >> 4) & 0x0F; }

/**
 * One I2C master.
 *
 * `busy` mirrors the peripheral's BUSY bit so the tile's bus watchdog sees
 * something realistic: set between START and STOP, clear otherwise.
 */
class Master {
 public:
  /** Pointer write terminated with STOP, as the hub does it. */
  void writePointer(std::uint8_t reg) {
    startWrite();
    sendByte(reg);
    stop();
  }

  /** Register write: pointer then payload bytes, one transaction. */
  void writeRegister(std::uint8_t reg, std::initializer_list<std::uint8_t> payload) {
    startWrite();
    sendByte(reg);
    for (std::uint8_t b : payload) sendByte(b);
    stop();
  }

  /**
   * Read `count` bytes from wherever the pointer sits, ending with the NACK a
   * master sends on the last byte. This is the shape the hub's single-
   * transaction snapshot read takes.
   */
  std::vector<std::uint8_t> read(std::uint8_t count) {
    std::vector<std::uint8_t> out;
    if (count == 0) return out;
    startRead();
    out.push_back(takePrimedByte());
    for (std::uint8_t i = 1; i < count; ++i) out.push_back(takeNextByte());
    nackAndStop();
    return out;
  }

  /**
   * A read the master abandons: it takes `count` bytes and then the bus dies,
   * so the tile never sees the closing NACK or STOP. Models a master reset
   * mid-transaction, which is what leaves the slave holding transaction state.
   */
  std::vector<std::uint8_t> readAndAbandon(std::uint8_t count) {
    std::vector<std::uint8_t> out;
    if (count == 0) return out;
    startRead();
    out.push_back(takePrimedByte());
    for (std::uint8_t i = 1; i < count; ++i) out.push_back(takeNextByte());
    I2C1->SR2 |= I2C_SR2_BUSY;  // BUSY never clears: no STOP arrives
    return out;
  }

  /** Pointer write then a read, the pair the hub issues every poll. */
  std::vector<std::uint8_t> readFrom(std::uint8_t reg, std::uint8_t count) {
    writePointer(reg);
    return read(count);
  }

  /**
   * Deliver one byte with TXE and BTF both set in the same SR1 snapshot. The
   * peripheral does this routinely at 400 kHz; servicing the two flags as
   * independent writes sends two bytes into one slot and skews the rest of
   * the frame.
   */
  std::uint8_t takeNextByteWithBtf() {
    I2C1->SR1 = I2C_SR1_TXE | I2C_SR1_BTF;
    I2C1_IRQHandler();
    return static_cast<std::uint8_t>(I2C1->DR);
  }

  /** True while the modelled bus is between a START and its STOP. */
  [[nodiscard]] bool busy() const { return (I2C1->SR2 & I2C_SR2_BUSY) != 0; }

 private:
  void startWrite() {
    I2C1->SR2 = I2C_SR2_BUSY;  // addressed, TRA clear = master writing
    I2C1->SR1 = I2C_SR1_ADDR;
    I2C1_IRQHandler();
  }

  void startRead() {
    I2C1->SR2 = I2C_SR2_BUSY | I2C_SR2_TRA;
    I2C1->SR1 = I2C_SR1_ADDR;
    I2C1_IRQHandler();
  }

  void sendByte(std::uint8_t value) {
    I2C1->DR = value;
    I2C1->SR1 = I2C_SR1_RXNE;
    I2C1_IRQHandler();
    I2C1->SR1 = 0;  // the handler's read of DR clears RXNE on the part
  }

  std::uint8_t takePrimedByte() {
    return static_cast<std::uint8_t>(I2C1->DR);  // ADDR-match already loaded it
  }

  std::uint8_t takeNextByte() {
    I2C1->SR1 = I2C_SR1_TXE;
    I2C1_IRQHandler();
    return static_cast<std::uint8_t>(I2C1->DR);
  }

  void nackAndStop() {
    I2C1->SR1 = I2C_SR1_AF;
    I2C1_IRQHandler();
    I2C1->SR2 &= ~I2C_SR2_BUSY;
    I2C1->SR1 = 0;
  }

  void stop() {
    I2C1->SR1 = I2C_SR1_STOPF;
    I2C1_IRQHandler();
    I2C1->SR2 &= ~I2C_SR2_BUSY;
    I2C1->SR1 = 0;
  }
};

/** Truncating uint8 sum of STATUS plus every DATA byte — the SUM register. */
inline std::uint8_t frameSum(const std::vector<std::uint8_t>& frame,
                             std::uint8_t dataLen) {
  std::uint16_t sum = frame[0];
  for (std::uint8_t i = 1; i <= dataLen; ++i) sum = std::uint16_t(sum + frame[i]);
  return static_cast<std::uint8_t>(sum);
}

/** A frame is intact when its trailing SUM matches its own contents. */
inline bool frameOk(const std::vector<std::uint8_t>& frame, std::uint8_t dataLen) {
  if (frame.size() != std::size_t(dataLen) + 2u) return false;
  return frameSum(frame, dataLen) == frame[dataLen + 1];
}

}  // namespace tile
