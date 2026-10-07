// User presets: record layout, bank file, directory, patch field table, validation, and the
// store that streams a bank to flash. See docs/preset-studio.md.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include <cstring>
#include <limits>
#include <set>
#include <string>

#include "../support/MemoryBankFile.h"
#include "persistence/SnapshotFormat.h"
#include "persistence/UserPresetBank.h"
#include "presetlink/UserPresetStore.h"
#include "voice/PatchCodec.h"
#include "voice/PatchFields.h"
#include "voice/UserPresetCodec.h"
#include "voice/VoiceConfig.h"
#include "voice/VoiceEditParameters.h"
#include "voice/VoicePresets.h"

using namespace persistence;
using testsupport::MemoryBankFile;

namespace
{
UserPresetRecord makeRecord(uint8_t base, uint8_t page, uint8_t pad, const char *name)
{
    UserPresetRecord r;
    usercodec::fromFactory(base, r);
    std::memset(r.name, 0, sizeof r.name);
    std::strncpy(r.name, name, sizeof r.name - 1);
    r.page = page;
    r.pad = pad;
    r.colorR = 10 + pad;
    r.colorG = 20;
    r.colorB = 30;
    usercodec::canonicalize(r);
    return r;
}

float nan()
{
    return std::numeric_limits<float>::quiet_NaN();
}

void setField(UserPresetRecord &r, const char *key, float value)
{
    const int i = patchfields::indexOfKey(key);
    REQUIRE(i >= 0);
    patchfields::write(r.patch, patchfields::field(static_cast<size_t>(i)), value);
}

float getField(const UserPresetRecord &r, const char *key)
{
    const int i = patchfields::indexOfKey(key);
    REQUIRE(i >= 0);
    return patchfields::read(r.patch, patchfields::field(static_cast<size_t>(i)));
}

int firstPresetWithEngine(uint8_t engine)
{
    for (uint8_t i = 0; i < VoicePresets::getPresetCount(); ++i)
        if (VoicePresets::getPresetConfig(i).engine == engine)
            return i;
    return -1;
}
} // namespace

TEST_CASE("user preset record and bank layout are locked", "[userpreset]")
{
    STATIC_REQUIRE(sizeof(UserPresetRecord) == 256u);
    STATIC_REQUIRE(offsetof(UserPresetRecord, patch) == 24u);
    STATIC_REQUIRE(kUserSlotCount == 62u);
    STATIC_REQUIRE(userBankFileSize(0) == 16u);
    STATIC_REQUIRE(userBankFileSize(62) == 12u + 256u * 62u + 4u);
    // The bank must stay small enough to share the 64 KB filesystem with the song file.
    STATIC_REQUIRE(kUserBankMaxFileSize <= 16 * 1024);
    STATIC_REQUIRE(userSlotIndex(1, 0) == 0u);
    STATIC_REQUIRE(userSlotIndex(2, 30) == 61u);
    STATIC_REQUIRE(slotPage(31) == 2u);
    STATIC_REQUIRE(slotPad(31) == 0u);
}

TEST_CASE("incremental crc matches the one-shot crc", "[userpreset]")
{
    const char *v = "123456789";
    Crc32 crc;
    crc.update(reinterpret_cast<const uint8_t *>(v), 4);
    crc.update(reinterpret_cast<const uint8_t *>(v) + 4, 5);
    REQUIRE(crc.value() == 0xCBF43926u);
    REQUIRE(Crc32{}.value() == 0u);
}

TEST_CASE("bank header round-trips and rejects foreign files", "[userpreset]")
{
    uint8_t h[kUserBankHeaderSize];
    writeUserBankHeader(h, 17);
    uint16_t n = 0;
    REQUIRE(readUserBankHeader(h, n) == BankStatus::Ok);
    REQUIRE(n == 17);

    uint8_t bad[kUserBankHeaderSize];
    std::memcpy(bad, h, sizeof h);
    bad[0] ^= 1;
    REQUIRE(readUserBankHeader(bad, n) == BankStatus::BadMagic);
    std::memcpy(bad, h, sizeof h);
    bad[4] = 9;
    REQUIRE(readUserBankHeader(bad, n) == BankStatus::BadVersion);
    std::memcpy(bad, h, sizeof h);
    bad[7] = 0; // record size 256 -> 0
    REQUIRE(readUserBankHeader(bad, n) == BankStatus::BadRecordSize);
    writeUserBankHeader(bad, kUserSlotCount + 1);
    REQUIRE(readUserBankHeader(bad, n) == BankStatus::BadCount);
}

TEST_CASE("directory tracks slots and navigates pages", "[userpreset]")
{
    UserPresetDirectory dir;
    REQUIRE(dir.count() == 0);
    REQUIRE(dir.nextPage(0) == 0); // nothing else to flip to
    REQUIRE(dir.clampPage(1) == 0);

    UserPresetRecord a = makeRecord(2, 1, 4, "Alpha");
    UserPresetRecord b = makeRecord(3, 2, 30, "Omega");
    REQUIRE(dir.add(a, 0));
    REQUIRE_FALSE(dir.add(a, 1)); // slot taken
    UserPresetRecord badPlace = a;
    badPlace.page = 0;
    REQUIRE_FALSE(dir.add(badPlace, 2));
    badPlace.page = 1;
    badPlace.pad = kPageKeyPad; // pad 31 is the page key, never a preset
    REQUIRE_FALSE(dir.add(badPlace, 2));

    REQUIRE(dir.count() == 1);
    REQUIRE(dir.entry(1, 4) != nullptr);
    REQUIRE(std::string(dir.entry(1, 4)->name) == "Alpha");
    REQUIRE(dir.entry(1, 4)->baseIndex == 2);
    REQUIRE(dir.entry(1, 4)->r == a.colorR);
    REQUIRE(dir.entry(1, 5) == nullptr);
    REQUIRE(dir.slot(userSlotIndex(1, 4)) == dir.entry(1, 4));

    // Page 2 is empty, so the flip skips it: 0 -> 1 -> 0.
    REQUIRE(dir.nextPage(0) == 1);
    REQUIRE(dir.nextPage(1) == 0);
    REQUIRE(dir.clampPage(1) == 1);
    REQUIRE(dir.clampPage(2) == 0);

    REQUIRE(dir.add(b, 1));
    REQUIRE(dir.nextPage(1) == 2);
    REQUIRE(dir.nextPage(2) == 0);
    REQUIRE(dir.pageCount(1) == 1);
    REQUIRE(dir.pageCount(2) == 1);
    REQUIRE(dir.pageCount(0) == 0);
    REQUIRE(dir.pageCount(3) == 0);

    dir.clear();
    REQUIRE(dir.count() == 0);
    REQUIRE(dir.entry(1, 4) == nullptr);
}

TEST_CASE("patch field table is unique, in bounds and non-overlapping", "[userpreset][fields]")
{
    std::set<std::string> keys;
    bool covered[sizeof(PatchSnapshot)] = {};
    for (size_t i = 0; i < patchfields::count(); ++i)
    {
        const auto &f = patchfields::field(i);
        INFO("row " << i << " " << f.key);
        REQUIRE(keys.insert(f.key).second);
        REQUIRE(patchfields::indexOfKey(f.key) == static_cast<int>(i));
        REQUIRE(f.edit < VoiceEdit::Id::Count);
        const size_t width = (f.type == patchfields::Type::Float || f.type == patchfields::Type::Int32) ? 4 : 1;
        REQUIRE(f.offset + width <= sizeof(PatchSnapshot));
        if (f.type == patchfields::Type::Flag)
        {
            REQUIRE(f.offset == offsetof(PatchSnapshot, flags));
            REQUIRE((f.mask & (f.mask - 1)) == 0); // one bit
            continue;
        }
        for (size_t b = 0; b < width; ++b)
        {
            REQUIRE_FALSE(covered[f.offset + b]);
            covered[f.offset + b] = true;
        }
    }
    REQUIRE(patchfields::indexOfKey("no.such.field") == -1);
    REQUIRE(patchfields::indexOfKey(nullptr) == -1);
}

TEST_CASE("patch field table covers every sound-bearing byte of the patch", "[userpreset][fields]")
{
    // Everything in PatchSnapshot is either a table row or one of the bytes the firmware derives.
    bool covered[sizeof(PatchSnapshot)] = {};
    for (size_t i = 0; i < patchfields::count(); ++i)
    {
        const auto &f = patchfields::field(i);
        const size_t width = (f.type == patchfields::Type::Float || f.type == patchfields::Type::Int32) ? 4 : 1;
        for (size_t b = 0; b < width; ++b)
            covered[f.offset + b] = true;
    }
    const size_t derived[] = {offsetof(PatchSnapshot, paramSet), offsetof(PatchSnapshot, presetIndex),
                              offsetof(PatchSnapshot, flags), offsetof(PatchSnapshot, reserved),
                              offsetof(PatchSnapshot, reserved) + 1};
    for (size_t b : derived)
        covered[b] = true;
    for (size_t b = 0; b < sizeof(PatchSnapshot); ++b)
    {
        INFO("byte " << b << " of PatchSnapshot is not editable and not derived");
        REQUIRE(covered[b]);
    }
}

TEST_CASE("patch field table is pinned to a layout version", "[userpreset][fields]")
{
    // If this fails you changed a row (or PatchSnapshot). Bump patchfields::kLayoutVersion,
    // regenerate the editor resources (see docs/preset-studio.md) and update both numbers.
    REQUIRE(patchfields::kLayoutVersion == 1);
    REQUIRE(patchfields::tableHash() == 0xCD2B51B3u);
}

TEST_CASE("patch fields read and write every stored type", "[userpreset][fields]")
{
    PatchSnapshot p{};
    for (size_t i = 0; i < patchfields::count(); ++i)
    {
        const auto &f = patchfields::field(i);
        float value = 1.0f;
        if (f.type == patchfields::Type::Float)
            value = 0.25f + static_cast<float>(i) * 0.01f;
        if (f.type == patchfields::Type::Int32)
            value = -7.0f;
        patchfields::write(p, f, value);
        INFO(f.key);
        REQUIRE(patchfields::read(p, f) == Catch::Approx(value));
    }
    // Writing a flag leaves the others alone.
    const auto &gate = patchfields::field(static_cast<size_t>(patchfields::indexOfKey("base.gate")));
    const auto &slide = patchfields::field(static_cast<size_t>(patchfields::indexOfKey("base.slide")));
    patchfields::write(p, gate, 0.0f);
    REQUIRE(patchfields::read(p, gate) == 0.0f);
    REQUIRE(patchfields::read(p, slide) == 1.0f);
}

TEST_CASE("every factory preset becomes a valid user preset with the same sound", "[userpreset][codec]")
{
    for (uint8_t i = 0; i < VoicePresets::getPresetCount(); ++i)
    {
        INFO("factory preset " << int(i) << " " << VoicePresets::getPresetName(i));
        UserPresetRecord r;
        usercodec::fromFactory(i, r);
        const auto check = usercodec::validate(r);
        INFO(usercodec::problemName(check.problem) << " field " << int(check.field)
             << (check.field < patchfields::count() ? patchfields::field(check.field).key : ""));
        REQUIRE(check.ok());

        // Canonicalising an already canonical record changes nothing.
        UserPresetRecord again = r;
        usercodec::canonicalize(again);
        REQUIRE(std::memcmp(&again, &r, sizeof r) == 0);

        // Rebuilding the voice gives the factory voice's captured patch back.
        VoiceConfig built;
        REQUIRE(usercodec::toConfig(r, built));
        PatchSnapshot expected{}, actual{};
        voicecodec::capturePatch(VoicePresets::getPresetConfig(i), expected);
        voicecodec::capturePatch(built, actual);
        expected.presetIndex = actual.presetIndex = i;
        expected.flags |= 1; // a factory config predates use-patch-bases; the firmware sets it
        REQUIRE(std::memcmp(&expected, &actual, sizeof expected) == 0);
        // ...with the flash descriptors of the base preset intact.
        REQUIRE(built.parameters == VoicePresets::getPresetConfig(i).parameters);
        REQUIRE(built.recipe == VoicePresets::getPresetConfig(i).recipe);
    }
}

TEST_CASE("edited values reach the playable voice", "[userpreset][codec]")
{
    UserPresetRecord r = makeRecord(2, 1, 0, "Edited");
    setField(r, "filter.cutoff", 0.61f);
    setField(r, "filter.resonance", 0.44f);
    setField(r, "env.release", 1.25f);
    setField(r, "out.level", 0.5f);
    setField(r, "osc1.wave", WAVE_TRI);
    setField(r, "base.gate", 0.0f);
    usercodec::canonicalize(r);
    REQUIRE(usercodec::validate(r).ok());

    VoiceConfig c;
    REQUIRE(usercodec::toConfig(r, c));
    REQUIRE(c.filterCutoffBase == Catch::Approx(0.61f));
    REQUIRE(c.filterRes == Catch::Approx(0.44f));
    REQUIRE(c.defaultRelease == Catch::Approx(1.25f));
    REQUIRE(c.outputLevel == Catch::Approx(0.5f));
    REQUIRE(c.oscWaveforms[0] == WAVE_TRI);
    REQUIRE_FALSE(c.baseGate);
    REQUIRE(c.usePatchBases);
}

TEST_CASE("fromConfig captures a running voice", "[userpreset][codec]")
{
    VoiceConfig live = VoicePresets::getPresetConfig(5);
    live.filterRes = 0.77f;
    UserPresetRecord r;
    usercodec::fromConfig(live, 5, r);
    std::strncpy(r.name, "Grabbed", sizeof r.name - 1);
    r.page = 1;
    r.pad = 3;
    usercodec::canonicalize(r);
    REQUIRE(usercodec::validate(r).ok());
    REQUIRE(r.baseIndex == 5);
    REQUIRE(getField(r, "filter.resonance") == Catch::Approx(0.77f));
}

TEST_CASE("canonicalize derives the bytes the editor does not own", "[userpreset][codec]")
{
    UserPresetRecord r = makeRecord(2, 1, 0, "Derive");
    r.patch.presetIndex = 99;
    r.patch.flags &= static_cast<uint8_t>(~1u);
    r.flags = 7;
    r.reserved = 7;
    r.patch.reserved[0] = 9;
    std::memset(r.name + 7, 'x', sizeof r.name - 8); // junk after the terminator at [6]
    usercodec::canonicalize(r);
    REQUIRE(r.patch.presetIndex == r.baseIndex);
    REQUIRE((r.patch.flags & 1u) == 1u);
    REQUIRE(r.flags == 0);
    REQUIRE(r.reserved == 0);
    REQUIRE(r.patch.reserved[0] == 0);
    for (size_t i = 6; i < sizeof r.name; ++i)
        REQUIRE(r.name[i] == '\0');

    // paramSet follows the engine and hard-sync waveforms, like the on-device editor.
    r.patch.engine = ENGINE_OSC;
    r.patch.oscillatorCount = 2;
    r.patch.oscWaveforms[1] = WAVE_HARDSYNC_SAW;
    usercodec::canonicalize(r);
    REQUIRE(r.patch.paramSet == PARAMSET_HARDSYNC);
    r.patch.oscWaveforms[1] = WAVE_SAW;
    usercodec::canonicalize(r);
    REQUIRE(r.patch.paramSet == PARAMSET_STANDARD);
    r.patch.engine = ENGINE_WAVEGUIDE;
    usercodec::canonicalize(r);
    REQUIRE(r.patch.paramSet == PARAMSET_WAVEGUIDE);

    // Quantised bases.
    r.patch.baseNote = 3.4f;
    r.patch.baseOctave = 11.0f;
    usercodec::canonicalize(r);
    REQUIRE(r.patch.baseNote == 3.0f);
    REQUIRE(r.patch.baseOctave == 12.0f);
}

TEST_CASE("validator refuses what it should", "[userpreset][codec]")
{
    const UserPresetRecord good = makeRecord(2, 1, 0, "Good");
    REQUIRE(usercodec::validate(good).ok());

    SECTION("names")
    {
        for (const char *name : {"", "   ", "0123456789ABCDEF" /* 16 chars, no room for NUL */})
        {
            UserPresetRecord r = good;
            std::memset(r.name, 0, sizeof r.name);
            std::memcpy(r.name, name, std::min<size_t>(std::strlen(name), sizeof r.name));
            REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadName);
        }
        UserPresetRecord r = good;
        r.name[0] = '\x7F';
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadName);
        r = good;
        r.name[0] = static_cast<char>(0xC3); // non-ASCII
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadName);
        r = good;
        r.name[10] = 'x'; // text after the terminator
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadName);
        r = good;
        std::memset(r.name, 0, sizeof r.name);
        std::memcpy(r.name, "123456789012345", 15); // exactly 15: fine
        REQUIRE(usercodec::validate(r).ok());
    }
    SECTION("place and base")
    {
        UserPresetRecord r = good;
        r.page = 0;
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadPlace);
        r = good;
        r.page = kUserPageCount + 1;
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadPlace);
        r = good;
        r.pad = kPageKeyPad;
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadPlace);
        r = good;
        r.baseIndex = VoicePresets::getPresetCount();
        r.patch.presetIndex = r.baseIndex;
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadBase);
        r = good;
        r.patch.presetIndex = static_cast<uint8_t>(r.baseIndex + 1);
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadBase);
    }
    SECTION("reserved bytes")
    {
        UserPresetRecord r = good;
        r.flags = 1;
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadReserved);
        r = good;
        r.patch.reserved[1] = 1;
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadReserved);
        r = good;
        r.patch.flags &= static_cast<uint8_t>(~1u); // use-patch-bases must be set
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadReserved);
    }
    SECTION("values")
    {
        const int res = patchfields::indexOfKey("filter.resonance");
        for (float bad : {nan(), std::numeric_limits<float>::infinity(), -0.5f, 1.5f})
        {
            UserPresetRecord r = good;
            setField(r, "filter.resonance", bad);
            const auto check = usercodec::validate(r);
            INFO(bad);
            REQUIRE(check.problem == usercodec::Problem::BadField);
            REQUIRE(check.field == res);
        }
        UserPresetRecord r = good;
        setField(r, "filter.resonance", 1.0f); // the bound itself is fine
        REQUIRE(usercodec::validate(r).ok());
        r = good;
        setField(r, "hp.cutoff", 0.0f); // 0 = high-pass off, which factory presets use
        REQUIRE(usercodec::validate(r).ok());
        setField(r, "hp.cutoff", -1.0f);
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadField);
        setField(r, "hp.cutoff", 25000.0f);
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadField);
        r = good;
        setField(r, "osc1.harmony", 13.0f);
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadField);
        r = good;
        setField(r, "base.octave", 60.0f);
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadField);
    }
    SECTION("choices")
    {
        UserPresetRecord r = good;
        setField(r, "source.engine", 5);
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadField);
        r = good;
        setField(r, "osc1.wave", 7); // 7 is the editor's index for noise; the stored id is 255
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadField);
        r = good;
        setField(r, "osc1.wave", WAVE_NOISE);
        REQUIRE(usercodec::validate(r).ok());
        r = good;
        setField(r, "filter.mode", 6);
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadField);
        r = good;
        setField(r, "filter.type", 2);
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadField);
        r = good;
        setField(r, "source.oscCount", 4);
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadField);
    }
    SECTION("a recipe engine needs a recipe base")
    {
        UserPresetRecord r = good; // base preset 2 (Bass) is an oscillator voice
        REQUIRE(VoicePresets::getPresetConfig(2).recipe == nullptr);
        setField(r, "source.engine", ENGINE_RECIPE);
        usercodec::canonicalize(r);
        REQUIRE(usercodec::validate(r).problem == usercodec::Problem::EngineNeedsRecipe);
    }
}

TEST_CASE("recipe macros are validated against the base preset's own lane spans", "[userpreset][codec]")
{
    // Phase morph's Skew lane runs -1..+1, which the generic Macro 2 catalog range (0..8) excludes.
    int phase = -1;
    for (uint8_t i = 0; i < VoicePresets::getPresetCount(); ++i)
        if (std::string(VoicePresets::getPresetName(i)) == "PhaseMorph")
            phase = i;
    REQUIRE(phase >= 0);
    UserPresetRecord r = makeRecord(static_cast<uint8_t>(phase), 1, 0, "Skewed");
    setField(r, "recipe.macro2", -0.5f);
    REQUIRE(usercodec::validate(r).ok());
    setField(r, "recipe.macro2", -1.5f);
    REQUIRE(usercodec::validate(r).problem == usercodec::Problem::BadField);
}

TEST_CASE("engine can change away from a recipe base", "[userpreset][codec]")
{
    const int recipe = firstPresetWithEngine(ENGINE_RECIPE);
    REQUIRE(recipe >= 0);
    UserPresetRecord r = makeRecord(static_cast<uint8_t>(recipe), 1, 0, "Rebuilt");
    setField(r, "source.engine", ENGINE_OSC);
    setField(r, "source.oscCount", 2);
    setField(r, "osc1.wave", WAVE_SAW);
    setField(r, "osc2.wave", WAVE_SQUARE);
    setField(r, "osc1.level", 0.5f);
    setField(r, "osc2.level", 0.5f);
    usercodec::canonicalize(r);
    REQUIRE(usercodec::validate(r).ok());
    VoiceConfig c;
    REQUIRE(usercodec::toConfig(r, c));
    REQUIRE(c.engine == ENGINE_OSC);
    // The recipe base's lane layout does not carry over to a different engine.
    REQUIRE(c.parameters == nullptr);
}

TEST_CASE("the patch field show rules agree with VoiceEdit::available", "[userpreset][fields]")
{
    std::vector<VoiceConfig> configs;
    for (uint8_t i = 0; i < VoicePresets::getPresetCount(); ++i)
    {
        VoiceConfig base = VoicePresets::getPresetConfig(i);
        configs.push_back(base);
        VoiceConfig v = base;
        v.hasFilter = !v.hasFilter;
        configs.push_back(v);
        v = base;
        v.filterType = v.filterType == FILTER_LADDER ? FILTER_SVF : FILTER_LADDER;
        configs.push_back(v);
        v = base;
        v.hasOverdrive = !v.hasOverdrive;
        v.hasEnvelope = !v.hasEnvelope;
        configs.push_back(v);
        if (base.engine == ENGINE_OSC)
            for (uint8_t count = 1; count <= 3; ++count)
                for (uint8_t wave : {WAVE_SAW, WAVE_SQUARE, WAVE_BSP_SQUARE, WAVE_NOISE})
                {
                    v = base;
                    v.oscillatorCount = count;
                    v.oscWaveforms[0] = v.oscWaveforms[1] = v.oscWaveforms[2] = wave;
                    configs.push_back(v);
                }
        for (uint8_t engine : {ENGINE_OSC, ENGINE_WAVEGUIDE, ENGINE_NOISEFX, ENGINE_HYPERSAW})
        {
            v = base;
            VoiceEdit::setValue(VoiceEdit::Id::Engine, v, engine);
            configs.push_back(v);
        }
    }
    for (const VoiceConfig &c : configs)
        for (size_t i = 0; i < patchfields::count(); ++i)
        {
            const auto &f = patchfields::field(i);
            INFO(f.key << " engine=" << int(c.engine) << " osc=" << int(c.oscillatorCount)
                       << " filter=" << c.hasFilter << "/" << int(c.filterType));
            REQUIRE(patchfields::shown(f.show, c) == VoiceEdit::available(f.edit, c));
        }
}

// ---- store ------------------------------------------------------------------------------

namespace
{
using presetlink::UserPresetStore;

void upload(UserPresetStore &store, std::vector<UserPresetRecord> records)
{
    REQUIRE(store.begin(static_cast<uint16_t>(records.size())).ok());
    for (auto &r : records)
        REQUIRE(store.put(r).ok());
    REQUIRE(store.commit().ok());
}
} // namespace

TEST_CASE("an uploaded bank survives a power cycle", "[userpreset][store]")
{
    MemoryBankFile file;
    UserPresetStore store(file);
    REQUIRE(store.load() == 0);
    REQUIRE(store.bankBytes() == 0);

    std::vector<UserPresetRecord> bank = {makeRecord(2, 1, 0, "Bass One"), makeRecord(6, 1, 7, "Hit"),
                                          makeRecord(9, 2, 30, "Pluck Last")};
    upload(store, bank);
    REQUIRE(store.count() == 3);
    REQUIRE(store.fileRecords() == 3);
    REQUIRE(store.bankBytes() == userBankFileSize(3));
    REQUIRE(file.live.size() == userBankFileSize(3));
    REQUIRE(store.directory().entry(1, 7)->baseIndex == 6);

    // "Reboot": a new store over the same flash rebuilds the directory.
    UserPresetStore reborn(file);
    REQUIRE(reborn.load() == 3);
    REQUIRE(std::string(reborn.directory().entry(2, 30)->name) == "Pluck Last");
    UserPresetRecord out;
    REQUIRE(reborn.readSlot(userSlotIndex(1, 7), out) == UserPresetStore::Result::Ok);
    REQUIRE(std::string(out.name) == "Hit");
    REQUIRE(std::memcmp(&out, &bank[1], sizeof out) == 0);
    REQUIRE(reborn.read(2, out) == UserPresetStore::Result::Ok);
    REQUIRE(std::string(out.name) == "Pluck Last");
    REQUIRE(reborn.read(3, out) == UserPresetStore::Result::Range);
    REQUIRE(reborn.readSlot(userSlotIndex(1, 1), out) == UserPresetStore::Result::Range);
}

TEST_CASE("an empty bank is a valid bank", "[userpreset][store]")
{
    MemoryBankFile file;
    UserPresetStore store(file);
    upload(store, {makeRecord(2, 1, 0, "Gone Soon")});
    REQUIRE(store.count() == 1);
    upload(store, {});
    REQUIRE(store.count() == 0);
    REQUIRE(file.live.size() == userBankFileSize(0));
    UserPresetStore reborn(file);
    REQUIRE(reborn.load() == 0);
}

TEST_CASE("a replaced bank is atomic", "[userpreset][store]")
{
    MemoryBankFile file;
    UserPresetStore store(file);
    upload(store, {makeRecord(2, 1, 0, "Old")});
    const auto oldFile = file.live;

    SECTION("abort keeps the old bank")
    {
        REQUIRE(store.begin(1).ok());
        auto r = makeRecord(3, 1, 1, "New");
        REQUIRE(store.put(r).ok());
        store.abort();
        REQUIRE_FALSE(store.uploading());
        REQUIRE(file.live == oldFile);
        REQUIRE(std::string(store.directory().entry(1, 0)->name) == "Old");
        REQUIRE(store.directory().entry(1, 1) == nullptr);
    }
    SECTION("a flash write failure mid-upload keeps the old bank")
    {
        file.failWriteAfter = 2; // header and one record go through, the second record fails
        REQUIRE(store.begin(2).ok());
        auto a = makeRecord(3, 1, 1, "A");
        auto b = makeRecord(4, 1, 2, "B");
        REQUIRE(store.put(a).ok());
        REQUIRE(store.put(b).result == UserPresetStore::Result::Storage);
        REQUIRE_FALSE(store.uploading());
        REQUIRE(file.live == oldFile);
        REQUIRE(store.directory().entry(1, 0) != nullptr);
    }
    SECTION("a failed rename keeps the old bank")
    {
        file.failCommit = true;
        REQUIRE(store.begin(1).ok());
        auto a = makeRecord(3, 1, 1, "A");
        REQUIRE(store.put(a).ok());
        REQUIRE(store.commit().result == UserPresetStore::Result::Storage);
        REQUIRE(file.live == oldFile);
        REQUIRE(store.directory().entry(1, 0) != nullptr);
        REQUIRE(store.directory().entry(1, 1) == nullptr);
    }
    SECTION("commit before every record arrived is refused and the upload stays open")
    {
        REQUIRE(store.begin(2).ok());
        auto a = makeRecord(3, 1, 1, "A");
        REQUIRE(store.put(a).ok());
        REQUIRE(store.commit().result == UserPresetStore::Result::CountMismatch);
        REQUIRE(store.uploading());
        auto b = makeRecord(4, 1, 2, "B");
        REQUIRE(store.put(b).ok());
        REQUIRE(store.commit().ok());
        REQUIRE(store.count() == 2);
    }
}

TEST_CASE("the store refuses bad uploads without losing the bank", "[userpreset][store]")
{
    MemoryBankFile file;
    UserPresetStore store(file);
    upload(store, {makeRecord(2, 1, 0, "Keep")});

    REQUIRE(store.put(*std::make_unique<UserPresetRecord>()).result == UserPresetStore::Result::BadState);
    REQUIRE(store.commit().result == UserPresetStore::Result::BadState);
    REQUIRE(store.begin(kUserSlotCount + 1).result == UserPresetStore::Result::Range);
    file.capacity = userBankFileSize(1) - 1;
    REQUIRE(store.begin(1).result == UserPresetStore::Result::NoSpace);
    file.capacity = 24 * 1024;

    REQUIRE(store.begin(3).ok());
    REQUIRE(store.begin(1).result == UserPresetStore::Result::BadState); // already uploading
    auto a = makeRecord(3, 1, 1, "A");
    REQUIRE(store.put(a).ok());
    auto dup = makeRecord(4, 1, 1, "Same Pad");
    REQUIRE(store.put(dup).result == UserPresetStore::Result::SlotTaken);
    auto bad = makeRecord(4, 1, 2, "Bad");
    setField(bad, "filter.resonance", 3.0f);
    const auto e = store.put(bad);
    REQUIRE(e.result == UserPresetStore::Result::Invalid);
    REQUIRE(e.check.problem == usercodec::Problem::BadField);
    REQUIRE(e.check.field == patchfields::indexOfKey("filter.resonance"));
    // Refused records do not count: the upload is still one short.
    REQUIRE(store.received() == 1);
    store.abort();
    REQUIRE(store.directory().entry(1, 0) != nullptr);
}

TEST_CASE("put canonicalises, so the stored bytes are the firmware's own", "[userpreset][store]")
{
    MemoryBankFile file;
    UserPresetStore store(file);
    auto r = makeRecord(2, 1, 0, "Sloppy");
    r.patch.presetIndex = 0;     // editor forgot to mirror the base
    r.patch.paramSet = 3;        // and sent a stale lane set
    r.patch.baseOctave = 13.0f;
    upload(store, {r});
    UserPresetRecord stored;
    REQUIRE(store.read(0, stored) == UserPresetStore::Result::Ok);
    REQUIRE(stored.patch.presetIndex == 2);
    REQUIRE(stored.patch.paramSet == PARAMSET_STANDARD);
    REQUIRE(stored.patch.baseOctave == 12.0f);
}

TEST_CASE("a torn or corrupted bank file loads as empty", "[userpreset][store]")
{
    MemoryBankFile file;
    UserPresetStore store(file);
    upload(store, {makeRecord(2, 1, 0, "A"), makeRecord(3, 1, 1, "B")});
    const auto good = file.live;

    auto reload = [&]() {
        UserPresetStore s(file);
        return s.load();
    };
    REQUIRE(reload() == 2);

    file.live = good;
    file.live[kUserBankHeaderSize + 40] ^= 0x01; // flip a bit inside record 0
    REQUIRE(reload() == 0);

    file.live = good;
    file.live.pop_back(); // truncated
    REQUIRE(reload() == 0);

    file.live = good;
    file.live[0] ^= 0xFF; // not a bank at all
    REQUIRE(reload() == 0);

    file.live.assign(3, 0);
    REQUIRE(reload() == 0);

    file.hasLive = false;
    REQUIRE(reload() == 0);
}

TEST_CASE("a record that no longer validates is left out of the browser", "[userpreset][store]")
{
    MemoryBankFile file;
    UserPresetStore store(file);
    upload(store, {makeRecord(2, 1, 0, "Fine"), makeRecord(3, 1, 1, "Soon Invalid")});

    // Simulate a firmware that is stricter: make record 1 out of range, then re-seal the file.
    UserPresetRecord r;
    std::memcpy(&r, file.live.data() + userBankRecordOffset(1), sizeof r);
    setField(r, "filter.resonance", 9.0f);
    std::memcpy(file.live.data() + userBankRecordOffset(1), &r, sizeof r);
    const uint32_t crc = crc32(file.live.data(), file.live.size() - kUserBankTrailerSize);
    for (int i = 0; i < 4; ++i)
        file.live[file.live.size() - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));

    UserPresetStore reborn(file);
    REQUIRE(reborn.load() == 1);
    REQUIRE(reborn.fileRecords() == 2);
    REQUIRE(reborn.directory().entry(1, 0) != nullptr);
    REQUIRE(reborn.directory().entry(1, 1) == nullptr);
}

TEST_CASE("a full bank of 62 presets fits the flash budget", "[userpreset][store]")
{
    MemoryBankFile file;
    file.capacity = kUserBankMaxFileSize;
    UserPresetStore store(file);
    std::vector<UserPresetRecord> bank;
    for (uint8_t page = 1; page <= kUserPageCount; ++page)
        for (uint8_t pad = 0; pad < kUserPadsPerPage; ++pad)
            bank.push_back(makeRecord(static_cast<uint8_t>(pad % VoicePresets::getPresetCount()), page, pad,
                                      ("Preset " + std::to_string(page) + "." + std::to_string(pad)).c_str()));
    upload(store, bank);
    REQUIRE(store.count() == kUserSlotCount);
    REQUIRE(file.live.size() == kUserBankMaxFileSize);
    UserPresetStore reborn(file);
    REQUIRE(reborn.load() == kUserSlotCount);
    REQUIRE(reborn.directory().nextPage(1) == 2);
}
