#ifndef PICO2SEQ_ARP_LED_PALETTE_H
#define PICO2SEQ_ARP_LED_PALETTE_H

#include <stdint.h>

// Pitch-to-colour rules for the Arpeggiator LED map. The hardware renderer keeps
// each active theme's saturation and brightness, then applies these small hue
// offsets in FastLED's 0..255 hue units. Semitone neighbours alternate around
// the selected voice's root hue, so the palette stays related while played notes
// remain easy to tell apart. Offsets expand in alternating directions around
// the root through a bounded arc; repeated octaves keep exactly the same hue.
namespace ArpLedPalette
{
inline constexpr uint8_t kPitchClassCount = 12;

constexpr uint8_t classifySemitone(int semitone) noexcept
{
  int pitchClass = semitone % static_cast<int>(kPitchClassCount);
  if (pitchClass < 0)
  {
    pitchClass += static_cast<int>(kPitchClassCount);
  }
  return static_cast<uint8_t>(pitchClass);
}

constexpr int16_t hueOffsetSteps(uint8_t pitchClass) noexcept
{
  constexpr int16_t offsets[kPitchClassCount] = {
      0, -16, 16, -32, 32, -48, 48, -64, 64, -80, 80, -96};
  return pitchClass < kPitchClassCount ? offsets[pitchClass] : 0;
}
} // namespace ArpLedPalette

#endif // PICO2SEQ_ARP_LED_PALETTE_H
