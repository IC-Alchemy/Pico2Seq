#pragma once

#include "../pico2seq-core/persistence/UserPresetBank.h"
#include "../voice/VoicePresets.h"
#include <cstdint>

// PresetBrowser.h - what a pad means on each page of the preset browser (portable, no
// hardware). Page 0 is the factory bank (pad N = preset N); pages 1.. hold the user presets
// uploaded from the PC editor, each at the pad its owner chose. Pad 31 is the page key.
// The same resolution drives pad taps, LED colours and the OLED, so they cannot disagree.
namespace PresetBrowser
{
struct Target
{
    enum class Kind : uint8_t
    {
        None,    // nothing on this pad
        Factory, // index = factory preset index
        User,    // index = user slot (persistence::userSlotIndex)
        PageKey  // flip to the next page
    };
    Kind kind = Kind::None;
    uint8_t index = 0;
};

inline Target resolve(uint8_t page, uint8_t pad, const persistence::UserPresetDirectory &user) noexcept
{
    using Kind = Target::Kind;
    if (pad == persistence::kPageKeyPad)
        // With nothing to flip to, the key stays dark and unassigned, as pad 31 always was.
        return user.count() != 0 ? Target{Kind::PageKey, 0} : Target{};
    if (pad >= persistence::kUserPadsPerPage)
        return {};
    if (page == 0)
    {
        const int preset = VoicePresets::presetIndexForPad(pad, VoicePresets::getPresetCount());
        return preset >= 0 ? Target{Kind::Factory, static_cast<uint8_t>(preset)} : Target{};
    }
    if (!user.entry(page, pad))
        return {};
    return {Kind::User, persistence::userSlotIndex(page, pad)};
}

// Whether the pad holds the sound this voice is playing: its factory preset, or its user slot.
inline bool isCurrent(const Target &target, uint8_t voicePresetIndex, uint8_t voiceUserSlot) noexcept
{
    switch (target.kind)
    {
    case Target::Kind::Factory: return voiceUserSlot == persistence::kNoSlot && target.index == voicePresetIndex;
    case Target::Kind::User: return target.index == voiceUserSlot;
    default: return false;
    }
}
} // namespace PresetBrowser
