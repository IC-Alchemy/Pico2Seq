#include "VoicePresets.h"
#include "VoiceParameters.h"
#include "presets/OscillatorPresets.h"
#include "presets/StringPresets.h"
#include "presets/TexturePresets.h"
#include "presets/RecipePresets.h"
#include <algorithm>
#include <iterator>

namespace VoicePresets {
namespace {
struct Preset { const char *name; VoiceConfig config; };
// Compile-time storage in flash; no runtime registration or heap allocation.
constexpr Preset kPresets[] = {
#define VOICE_PRESET(id, name, factory) {name, factory()},
#include "presets/PresetBank.h"
#undef VOICE_PRESET
};
static_assert(std::size(kPresets) == static_cast<size_t>(Id::Count));
static_assert(std::size(kPresets) <= 255, "Preset IDs must fit in uint8_t");
constexpr bool validBank()
{
  for (const auto &p : kPresets) {
    if (!p.name || !p.name[0]) return false;
    if (p.config.engine == ENGINE_RECIPE && (!p.config.recipe || !p.config.parameters)) return false;
    if (p.config.parameters) {
      for (const auto &b : p.config.parameters->slots) {
        if (b.maximum <= b.minimum) return false;
        if (b.curve == dspmap::Mapping::LOG && b.minimum <= 0.0f) return false;
      }
    }
  }
  return true;
}
static_assert(validBank(), "Each recipe needs a parameter layout with valid control ranges");
char lowerAscii(char c) noexcept { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
bool sameName(std::string_view a, std::string_view b) noexcept
{
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
  return true;
}
} // namespace

  const VoiceConfig &getAnalogVoice() noexcept { return kPresets[0].config; }
  const VoiceConfig &getDigitalVoice() noexcept { return kPresets[1].config; }
  const VoiceConfig &getBassVoice() noexcept { return kPresets[2].config; }
  const VoiceConfig &getLeadVoice() noexcept { return kPresets[3].config; }
  const VoiceConfig &getSquareVoice() noexcept { return kPresets[4].config; }
  const VoiceConfig &getPadVoice() noexcept { return kPresets[5].config; }
  const VoiceConfig &getPercussionVoice() noexcept { return kPresets[6].config; }

  const VoiceConfig &getSubFunkVoice() noexcept { return kPresets[7].config; }
  const VoiceConfig &getRubberSubVoice() noexcept { return kPresets[8].config; }
  const VoiceConfig &getWaveguidePluckVoice() noexcept { return kPresets[9].config; }
  const VoiceConfig &getWaveguideNylonVoice() noexcept { return kPresets[10].config; }
  const VoiceConfig &getWaveguideBellVoice() noexcept { return kPresets[11].config; }
  const VoiceConfig &getWaveguideShimmerVoice() noexcept { return kPresets[12].config; }
  const VoiceConfig &getHypersawVoice() noexcept { return kPresets[13].config; }
  const VoiceConfig &getNoiseStormVoice() noexcept { return kPresets[14].config; }


const char *getPresetName(uint8_t index) noexcept
{
  return index < getPresetCount() ? kPresets[index].name : "Unknown";
}
const VoiceConfig &getPresetConfig(uint8_t index) noexcept
{
  return kPresets[index < getPresetCount() ? index : 0].config;
}
int findPreset(std::string_view name) noexcept
{
  for (size_t i = 0; i < std::size(kPresets); ++i)
    if (sameName(name, kPresets[i].name)) return static_cast<int>(i);
  return -1;
}
const VoiceConfig &getPresetConfigByName(std::string_view name) noexcept
{
  const int index = findPreset(name);
  return getPresetConfig(index < 0 ? 0 : static_cast<uint8_t>(index));
}
uint8_t getPresetCount() noexcept { return static_cast<uint8_t>(std::size(kPresets)); }
VoiceParamSet getPresetParamSet(uint8_t index) noexcept
{
  return index < getPresetCount() ? static_cast<VoiceParamSet>(kPresets[index].config.paramSet)
                                  : PARAMSET_STANDARD;
}
const char *getSequencerParamName(uint8_t index, ParamId id) noexcept
{
  return index < getPresetCount() ? VoiceParameters::binding(kPresets[index].config, id).name : nullptr;
}
float wgT60ToNormalized(float seconds) noexcept
{
  return VoiceParameters::binding(getWaveguidePluckVoice(), ParamId::Decay).normalize(seconds);
}
uint8_t presetPageCount(uint8_t count) noexcept
{
  return static_cast<uint8_t>((static_cast<unsigned>(count) + kPresetsPerPage - 1) / kPresetsPerPage);
}
uint8_t presetCountOnPage(uint8_t count, uint8_t page) noexcept
{
  const unsigned first = static_cast<unsigned>(page) * kPresetsPerPage;
  return first < count ? static_cast<uint8_t>(std::min<unsigned>(kPresetsPerPage, count - first)) : 0;
}
int presetIndexForPad(uint8_t pad, uint8_t count, uint8_t page) noexcept
{
  if (pad < kFirstPresetPad || pad >= kFirstPresetPad + kPresetsPerPage) return -1;
  const unsigned index = static_cast<unsigned>(page) * kPresetsPerPage + pad - kFirstPresetPad;
  return index < count ? static_cast<int>(index) : -1;
}
uint8_t changePresetPage(uint8_t page, int direction, uint8_t count) noexcept
{
  const int pages = presetPageCount(count);
  return pages ? static_cast<uint8_t>(std::clamp(static_cast<int>(page) + direction, 0, pages - 1)) : 0;
}
} // namespace VoicePresets
