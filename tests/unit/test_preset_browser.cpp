// Preset browser pages: what each pad means on the factory page and on the user pages.
#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "persistence/UserPresetBank.h"
#include "ui/PresetBrowser.h"
#include "voice/UserPresetCodec.h"
#include "voice/VoicePresets.h"

using namespace persistence;
using Kind = PresetBrowser::Target::Kind;

namespace
{
UserPresetRecord record(uint8_t page, uint8_t pad, uint8_t base = 2)
{
    UserPresetRecord r;
    usercodec::fromFactory(base, r);
    std::strncpy(r.name, "User", sizeof r.name - 1);
    r.page = page;
    r.pad = pad;
    usercodec::canonicalize(r);
    return r;
}
} // namespace

TEST_CASE("with no user presets the browser behaves exactly as before", "[browser]")
{
    UserPresetDirectory none;
    const uint8_t count = VoicePresets::getPresetCount();
    for (uint8_t pad = 0; pad < 32; ++pad)
    {
        const auto t = PresetBrowser::resolve(0, pad, none);
        if (pad < count)
        {
            REQUIRE(t.kind == Kind::Factory);
            REQUIRE(t.index == pad);
        }
        else
        {
            REQUIRE(t.kind == Kind::None); // spare pads and pad 31 stay unassigned
        }
    }
    REQUIRE(none.nextPage(0) == 0);
}

TEST_CASE("user presets add pages and a page key", "[browser]")
{
    UserPresetDirectory dir;
    dir.add(record(1, 3), 0);
    dir.add(record(2, 30), 1);

    // Page 0 still holds the factory bank, plus the key on pad 31.
    REQUIRE(PresetBrowser::resolve(0, 5, dir).kind == Kind::Factory);
    REQUIRE(PresetBrowser::resolve(0, kPageKeyPad, dir).kind == Kind::PageKey);

    // A user page shows only the pads its owner placed - nothing else, not even factory presets.
    REQUIRE(PresetBrowser::resolve(1, 3, dir).kind == Kind::User);
    REQUIRE(PresetBrowser::resolve(1, 3, dir).index == userSlotIndex(1, 3));
    REQUIRE(PresetBrowser::resolve(1, 4, dir).kind == Kind::None);
    REQUIRE(PresetBrowser::resolve(1, 5, dir).kind == Kind::None);
    REQUIRE(PresetBrowser::resolve(1, kPageKeyPad, dir).kind == Kind::PageKey);
    REQUIRE(PresetBrowser::resolve(2, 30, dir).index == userSlotIndex(2, 30));

    // Flipping walks factory -> 1 -> 2 -> factory.
    uint8_t page = 0;
    page = dir.nextPage(page);
    REQUIRE(page == 1);
    page = dir.nextPage(page);
    REQUIRE(page == 2);
    page = dir.nextPage(page);
    REQUIRE(page == 0);
}

TEST_CASE("pads past the preset grid resolve to nothing", "[browser]")
{
    UserPresetDirectory dir;
    dir.add(record(1, 0), 0);
    REQUIRE(PresetBrowser::resolve(0, 32, dir).kind == Kind::None);
    REQUIRE(PresetBrowser::resolve(0, 255, dir).kind == Kind::None);
    REQUIRE(PresetBrowser::resolve(7, 0, dir).kind == Kind::None); // a page that does not exist
}

TEST_CASE("the pad holding a voice's sound is the current one", "[browser]")
{
    UserPresetDirectory dir;
    dir.add(record(1, 3, 6), 0);
    const auto factory = PresetBrowser::resolve(0, 6, dir);
    const auto user = PresetBrowser::resolve(1, 3, dir);

    // A factory sound lights its factory pad...
    REQUIRE(PresetBrowser::isCurrent(factory, 6, kNoSlot));
    REQUIRE_FALSE(PresetBrowser::isCurrent(factory, 5, kNoSlot));
    REQUIRE_FALSE(PresetBrowser::isCurrent(user, 6, kNoSlot));
    // ...and a user sound lights its user pad, not the factory preset it was built on.
    REQUIRE(PresetBrowser::isCurrent(user, 6, userSlotIndex(1, 3)));
    REQUIRE_FALSE(PresetBrowser::isCurrent(factory, 6, userSlotIndex(1, 3)));
    REQUIRE_FALSE(PresetBrowser::isCurrent(PresetBrowser::Target{}, 6, kNoSlot));
    REQUIRE_FALSE(PresetBrowser::isCurrent(PresetBrowser::resolve(0, kPageKeyPad, dir), 6, kNoSlot));
}

TEST_CASE("a page that emptied falls back to the factory page", "[browser]")
{
    UserPresetDirectory dir;
    dir.add(record(2, 1), 0);
    REQUIRE(dir.clampPage(2) == 2);
    dir.clear();
    REQUIRE(dir.clampPage(2) == 0);
    REQUIRE(dir.clampPage(1) == 0);
    REQUIRE(dir.clampPage(200) == 0);
}
