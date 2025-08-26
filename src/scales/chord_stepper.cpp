#include "chord_stepper.h"

namespace ChordStepper {

static Config cfg{};
static uint32_t nextChangeAt = 0;
static uint8_t currentIndex = 0;
static uint8_t patternPos = 0;

static inline uint16_t currentInterval()
{
  if (cfg.pattern && cfg.patternLength > 0)
  {
    return cfg.pattern[patternPos % cfg.patternLength];
  }
  return cfg.stepsPerChange == 0 ? 1 : cfg.stepsPerChange;
}

void setStepsPerChange(uint16_t steps)
{
  cfg.stepsPerChange = steps == 0 ? 1 : steps;
  // Disable pattern when using fixed step count
  cfg.pattern = nullptr;
  cfg.patternLength = 0;
  patternPos = 0;
}

void setProgressionLength(uint8_t length)
{
  cfg.progressionLength = (length == 0) ? 1 : length;
}

void setPattern(const uint16_t* pattern, uint8_t length)
{
  if (pattern && length > 0)
  {
    cfg.pattern = pattern;
    cfg.patternLength = length;
  }
  else
  {
    cfg.pattern = nullptr;
    cfg.patternLength = 0;
  }
  patternPos = 0;
}

void reset(uint32_t currentStep)
{
  currentIndex = 0;
  patternPos = 0;
  nextChangeAt = currentStep + static_cast<uint32_t>(currentInterval());
}

void tick(uint32_t currentStep)
{
  // Catch up if multiple changes should occur (e.g., missed steps or large jumps)
  while (currentStep >= nextChangeAt)
  {
    currentIndex = static_cast<uint8_t>((currentIndex + 1) % cfg.progressionLength);

    // Advance pattern position if using a pattern
    if (cfg.pattern && cfg.patternLength > 0)
    {
      patternPos = static_cast<uint8_t>((patternPos + 1) % cfg.patternLength);
    }

    uint16_t interval = currentInterval();
    if (interval == 0) interval = 1; // Safety
    nextChangeAt += interval;
  }
}

uint8_t getIndex()
{
  return currentIndex;
}

} // namespace ChordStepper