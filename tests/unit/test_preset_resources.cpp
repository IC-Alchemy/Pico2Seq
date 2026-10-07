// The files the PC editor (tools/PresetStudio) is built from are generated here from the
// firmware's own tables, so the two cannot disagree about the layout:
//
//   patch-schema.json    every editable value: key, byte offset, type, label, unit, range, choices,
//                        when it applies (from patchfields + the VoiceEdit catalog)
//   factory-presets.json the factory bank as editor starting points, with each preset's lane
//                        names, macro spans and cutoff mapping
//   golden-vectors.json  byte-exact records and request/reply frames, replayed by the C# tests
//
// The checked-in copies must match what this test generates. After a deliberate change:
//   PICO2SEQ_UPDATE_RESOURCES=1 ./build_test_ninja/tests/pico2seq_tests "[resources]"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../support/JsonWriter.h"
#include "../support/LinkRig.h"
#include "../support/MemoryBankFile.h"
#include "persistence/UserPresetBank.h"
#include "presetlink/PresetLinkProtocol.h"
#include "voice/PatchCodec.h"
#include "voice/PatchFields.h"
#include "voice/UserPresetCodec.h"
#include "voice/VoiceConfig.h"
#include "voice/VoiceEditParameters.h"
#include "voice/VoiceParameters.h"
#include "voice/VoicePresets.h"

#ifndef PICO2SEQ_SOURCE_DIR
#error "PICO2SEQ_SOURCE_DIR must be defined by tests/CMakeLists.txt"
#endif

using namespace persistence;
using testsupport::JsonWriter;

namespace
{
const char *const kUnitNames[] = {"Number", "Percent", "Seconds", "Semitones", "Cents", "Hertz", "Toggle", "Choice"};
const char *const kGroupIds[] = {"Sequenced", "Source", "Osc1", "Osc2", "Osc3", "Envelope",
                                 "Filter", "HighPass", "Drive", "Engine", "Output"};
const char *const kLaneIds[] = {"Note", "Velocity", "Filter", "Attack", "Decay", "Octave",
                                "GateLength", "Gate", "Slide", "Sustain", "Release"};
static_assert(sizeof(kUnitNames) / sizeof(kUnitNames[0]) == 8, "VoiceEdit::Unit changed");
static_assert(sizeof(kGroupIds) / sizeof(kGroupIds[0]) == static_cast<size_t>(VoiceEdit::Group::Count),
              "VoiceEdit::Group changed");
static_assert(sizeof(kLaneIds) / sizeof(kLaneIds[0]) == PARAM_ID_COUNT, "ParamId changed");

const char *typeName(patchfields::Type t)
{
    switch (t)
    {
    case patchfields::Type::Float: return "float";
    case patchfields::Type::Int32: return "int32";
    case patchfields::Type::Byte: return "byte";
    case patchfields::Type::Flag: return "flag";
    }
    return "float";
}

const char *curveName(dspmap::Mapping m)
{
    switch (m)
    {
    case dspmap::Mapping::LINEAR: return "linear";
    case dspmap::Mapping::EXP: return "exp";
    case dspmap::Mapping::LOG: return "log";
    case dspmap::Mapping::OCTAVE: return "octave";
    }
    return "linear";
}

const char *unitName(VoiceParameterUnit u)
{
    switch (u)
    {
    case VoiceParameterUnit::Percent: return "Percent";
    case VoiceParameterUnit::Seconds: return "Seconds";
    case VoiceParameterUnit::Semitones: return "Semitones";
    case VoiceParameterUnit::Ratio: return "Ratio";
    case VoiceParameterUnit::Hertz: return "Hertz";
    default: return "Number";
    }
}

std::string hex(const uint8_t *data, size_t n)
{
    static const char *digits = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i)
    {
        s += digits[data[i] >> 4];
        s += digits[data[i] & 15];
    }
    return s;
}
std::string hex(const std::vector<uint8_t> &v) { return hex(v.data(), v.size()); }

bool isWaveField(const patchfields::Field &f)
{
    return f.edit == VoiceEdit::Id::Wave1 || f.edit == VoiceEdit::Id::Wave2 || f.edit == VoiceEdit::Id::Wave3;
}

bool isChoiceField(const patchfields::Field &f)
{
    return f.edit == VoiceEdit::Id::Engine || f.edit == VoiceEdit::Id::FilterType ||
           f.edit == VoiceEdit::Id::FilterMode || isWaveField(f);
}

// The ways a patch value can be played, in the order the editor offers them.
std::vector<uint8_t> choiceValues(const patchfields::Field &f)
{
    switch (f.edit)
    {
    case VoiceEdit::Id::Engine: return {0, 1, 2, 3, 4};
    case VoiceEdit::Id::FilterType: return {0, 1};
    case VoiceEdit::Id::FilterMode: return {0, 1, 2, 3, 4, 5};
    default: return {WAVE_SIN, WAVE_TRI, WAVE_SAW, WAVE_SQUARE, WAVE_BSP_SAW, WAVE_BSP_SQUARE,
                     WAVE_HARDSYNC_SAW, WAVE_NOISE};
    }
}

std::string choiceLabel(const patchfields::Field &f, uint8_t value)
{
    VoiceConfig c;
    char text[48] = "";
    switch (f.edit)
    {
    case VoiceEdit::Id::Engine: c.engine = value; break;
    case VoiceEdit::Id::FilterType: c.filterType = value; break;
    case VoiceEdit::Id::FilterMode: c.filterMode = static_cast<VoiceFilterMode>(value); break;
    case VoiceEdit::Id::Wave1: c.oscWaveforms[0] = value; break;
    case VoiceEdit::Id::Wave2: c.oscWaveforms[1] = value; break;
    default: c.oscWaveforms[2] = value; break;
    }
    VoiceEdit::format(f.edit, c, text, sizeof text);
    return text;
}

struct Range
{
    float lo, hi;
};

// The range of every Float/Int32/Byte row under one configuration.
std::vector<Range> rangesFor(const VoiceConfig &config)
{
    std::vector<Range> out;
    for (size_t i = 0; i < patchfields::count(); ++i)
    {
        const auto &f = patchfields::field(i);
        Range r{0, 1};
        if (f.type != patchfields::Type::Flag)
            patchfields::limits(f, config, r.lo, r.hi);
        out.push_back(r);
    }
    return out;
}

// Contexts the device can build a voice in: each factory preset, and each engine with the
// default layout (what the engine falls back to when the base preset's layout is dropped).
std::vector<VoiceConfig> engineDefaults()
{
    std::vector<VoiceConfig> out;
    for (uint8_t engine : {ENGINE_OSC, ENGINE_WAVEGUIDE, ENGINE_NOISEFX, ENGINE_HYPERSAW})
    {
        VoiceConfig c;
        VoiceEdit::setValue(VoiceEdit::Id::Engine, c, engine);
        out.push_back(c);
    }
    return out;
}

std::vector<Range> loosestRanges()
{
    std::vector<Range> loose = rangesFor(VoiceConfig{});
    const auto widen = [&](const std::vector<Range> &r) {
        for (size_t i = 0; i < loose.size(); ++i)
        {
            loose[i].lo = std::min(loose[i].lo, r[i].lo);
            loose[i].hi = std::max(loose[i].hi, r[i].hi);
        }
    };
    for (uint8_t i = 0; i < VoicePresets::getPresetCount(); ++i)
        widen(rangesFor(VoicePresets::getPresetConfig(i)));
    for (const VoiceConfig &c : engineDefaults())
        widen(rangesFor(c));
    return loose;
}

void writeRangeOverrides(JsonWriter &w, const std::vector<Range> &loose, const std::vector<Range> &actual)
{
    w.beginObject();
    for (size_t i = 0; i < loose.size(); ++i)
        if (loose[i].lo != actual[i].lo || loose[i].hi != actual[i].hi)
        {
            w.key(patchfields::field(i).key);
            w.beginArray(true);
            w.real(actual[i].lo);
            w.real(actual[i].hi);
            w.endArray();
        }
    w.endObject();
}

std::string schemaJson()
{
    JsonWriter w;
    const auto loose = loosestRanges();
    w.beginObject();
    w.member("format", "pico2seq-patch-schema");
    w.member("layoutVersion", static_cast<unsigned>(patchfields::kLayoutVersion));
    w.member("tableHash", static_cast<int64_t>(patchfields::tableHash()));
    w.member("recordSize", static_cast<unsigned>(sizeof(UserPresetRecord)));
    w.member("recordHeaderSize", static_cast<unsigned>(offsetof(UserPresetRecord, patch)));
    w.member("patchSize", static_cast<unsigned>(sizeof(PatchSnapshot)));
    w.member("nameSize", static_cast<unsigned>(kUserPresetNameSize));
    w.member("presetNameMaxLength", static_cast<unsigned>(kUserPresetNameSize - 1));

    w.key("record");
    w.beginObject();
    w.member("nameOffset", static_cast<unsigned>(offsetof(UserPresetRecord, name)));
    w.member("pageOffset", static_cast<unsigned>(offsetof(UserPresetRecord, page)));
    w.member("padOffset", static_cast<unsigned>(offsetof(UserPresetRecord, pad)));
    w.member("baseIndexOffset", static_cast<unsigned>(offsetof(UserPresetRecord, baseIndex)));
    w.member("colorOffset", static_cast<unsigned>(offsetof(UserPresetRecord, colorR)));
    w.member("patchOffset", static_cast<unsigned>(offsetof(UserPresetRecord, patch)));
    w.member("paramSetOffset", static_cast<unsigned>(offsetof(UserPresetRecord, patch) + offsetof(PatchSnapshot, paramSet)));
    w.member("presetIndexOffset", static_cast<unsigned>(offsetof(UserPresetRecord, patch) + offsetof(PatchSnapshot, presetIndex)));
    w.member("flagsOffset", static_cast<unsigned>(offsetof(UserPresetRecord, patch) + offsetof(PatchSnapshot, flags)));
    w.member("usePatchBasesMask", 1);
    w.endObject();

    w.key("browser");
    w.beginObject();
    w.member("factoryPage", 0);
    w.member("firstUserPage", static_cast<unsigned>(kFirstUserPage));
    w.member("userPages", static_cast<unsigned>(kUserPageCount));
    w.member("padsPerPage", static_cast<unsigned>(kUserPadsPerPage));
    w.member("pageKeyPad", static_cast<unsigned>(kPageKeyPad));
    w.member("gridWidth", 8);
    w.member("gridHeight", 4);
    w.member("maxPresets", static_cast<unsigned>(kUserSlotCount));
    w.member("maxBankBytes", static_cast<unsigned>(kUserBankMaxFileSize));
    w.endObject();

    w.key("paramSets");
    w.beginObject();
    w.member("standard", static_cast<unsigned>(PARAMSET_STANDARD));
    w.member("waveguide", static_cast<unsigned>(PARAMSET_WAVEGUIDE));
    w.member("hypersaw", static_cast<unsigned>(PARAMSET_HYPERSAW));
    w.member("noisestorm", static_cast<unsigned>(PARAMSET_NOISESTORM));
    w.member("hardsync", static_cast<unsigned>(PARAMSET_HARDSYNC));
    w.endObject();

    w.key("waveforms");
    w.beginObject();
    w.member("hardSyncSaw", static_cast<unsigned>(WAVE_HARDSYNC_SAW));
    w.member("square", static_cast<unsigned>(WAVE_SQUARE));
    w.member("bandLimitedSquare", static_cast<unsigned>(WAVE_BSP_SQUARE));
    w.member("noise", static_cast<unsigned>(WAVE_NOISE));
    w.endObject();

    w.key("engines");
    w.beginObject();
    w.member("osc", static_cast<unsigned>(ENGINE_OSC));
    w.member("waveguide", static_cast<unsigned>(ENGINE_WAVEGUIDE));
    w.member("noisefx", static_cast<unsigned>(ENGINE_NOISEFX));
    w.member("hypersaw", static_cast<unsigned>(ENGINE_HYPERSAW));
    w.member("recipe", static_cast<unsigned>(ENGINE_RECIPE));
    w.endObject();

    w.key("groups");
    w.beginArray();
    for (size_t g = 0; g < static_cast<size_t>(VoiceEdit::Group::Count); ++g)
    {
        w.beginObject();
        w.member("id", kGroupIds[g]);
        w.member("label", VoiceEdit::groupName(static_cast<VoiceEdit::Group>(g)));
        w.endObject();
    }
    w.endArray();

    w.key("fields");
    w.beginArray();
    for (size_t i = 0; i < patchfields::count(); ++i)
    {
        const auto &f = patchfields::field(i);
        const auto &p = VoiceEdit::parameter(f.edit);
        w.beginObject();
        w.member("key", f.key);
        w.member("label", p.name);
        w.member("group", kGroupIds[static_cast<size_t>(p.group)]);
        w.member("offset", static_cast<unsigned>(f.offset));
        w.member("type", typeName(f.type));
        w.member("mask", static_cast<unsigned>(f.mask));
        const bool choice = isChoiceField(f);
        w.member("unit", f.type == patchfields::Type::Flag ? "Toggle" : choice ? "Choice" : kUnitNames[static_cast<size_t>(p.unit)]);
        const bool integer = f.type != patchfields::Type::Float;
        w.member("integer", integer);
        if (f.type == patchfields::Type::Flag)
        {
            w.member("min", 0.0f);
            w.member("max", 1.0f);
        }
        else if (isWaveField(f))
        {
            w.member("min", 0.0f);
            w.member("max", static_cast<float>(WAVE_NOISE));
        }
        else
        {
            w.member("min", loose[i].lo);
            w.member("max", loose[i].hi);
        }
        w.member("log", p.logarithmic);
        w.member("show", patchfields::showName(f.show));
        if (choice)
        {
            w.key("choices");
            w.beginArray();
            for (uint8_t v : choiceValues(f))
            {
                w.beginObject();
                w.member("value", static_cast<unsigned>(v));
                w.member("label", choiceLabel(f, v));
                w.endObject();
            }
            w.endArray();
        }
        w.endObject();
    }
    w.endArray();

    // Ranges that narrow below the ones above when the voice is built on a given engine's
    // default layout (the base preset's own layout is dropped when the engine changes).
    w.key("engineLimits");
    w.beginObject();
    const std::vector<VoiceConfig> engines = engineDefaults();
    const uint8_t engineIds[] = {ENGINE_OSC, ENGINE_WAVEGUIDE, ENGINE_NOISEFX, ENGINE_HYPERSAW};
    for (size_t e = 0; e < engines.size(); ++e)
    {
        w.key(std::to_string(engineIds[e]));
        writeRangeOverrides(w, loose, rangesFor(engines[e]));
    }
    w.endObject();

    w.key("laneIds");
    w.beginArray(true);
    for (const char *lane : kLaneIds)
        w.str(lane);
    w.endArray();
    w.endObject();
    return w.take();
}

std::string factoryJson()
{
    JsonWriter w;
    const auto loose = loosestRanges();
    w.beginObject();
    w.member("format", "pico2seq-factory-presets");
    w.member("layoutVersion", static_cast<unsigned>(patchfields::kLayoutVersion));
    w.key("presets");
    w.beginArray();
    for (uint8_t index = 0; index < VoicePresets::getPresetCount(); ++index)
    {
        const VoiceConfig &config = VoicePresets::getPresetConfig(index);
        UserPresetRecord record;
        usercodec::fromFactory(index, record);

        w.beginObject();
        w.member("index", static_cast<unsigned>(index));
        w.member("name", VoicePresets::getPresetName(index));
        w.member("engine", static_cast<unsigned>(config.engine));
        w.member("paramSet", static_cast<unsigned>(config.paramSet));
        if (config.recipe)
        {
            char text[48];
            VoiceEdit::format(VoiceEdit::Id::Recipe, config, text, sizeof text);
            w.member("recipe", text);
        }
        else
        {
            w.key("recipe");
            w.null();
        }

        w.key("values");
        w.beginObject();
        for (size_t i = 0; i < patchfields::count(); ++i)
        {
            const auto &f = patchfields::field(i);
            const float v = patchfields::read(record.patch, f);
            w.key(f.key);
            if (f.type == patchfields::Type::Float)
                w.real(v);
            else
                w.integer(static_cast<int64_t>(v));
        }
        w.endObject();

        w.key("limits");
        writeRangeOverrides(w, loose, rangesFor(config));

        // What each sequencer lane means on this preset, and which patch value it moves.
        w.key("lanes");
        w.beginArray();
        for (size_t lane = 0; lane < PARAM_ID_COUNT; ++lane)
        {
            w.beginObject();
            w.member("lane", kLaneIds[lane]);
            w.member("name", VoiceEdit::laneName(static_cast<ParamId>(lane), config));
            std::string fieldKey;
            for (size_t i = 0; i < patchfields::count() && fieldKey.empty(); ++i)
                if (VoiceEdit::sequenceLane(patchfields::field(i).edit, config) == static_cast<ParamId>(lane))
                    fieldKey = patchfields::field(i).key;
            w.key("field");
            if (fieldKey.empty())
                w.null();
            else
                w.str(fieldKey);
            w.endObject();
        }
        w.endArray();

        // Recipe macro lanes: the span each of the three macros plays over on this preset.
        w.key("macros");
        w.beginArray();
        float VoiceConfig::*const macros[3] = {&VoiceConfig::macro1, &VoiceConfig::macro2, &VoiceConfig::macro3};
        for (float VoiceConfig::*macro : macros)
        {
            bool found = false;
            for (ParamId lane : {ParamId::Filter, ParamId::Attack, ParamId::Decay})
            {
                const auto &b = VoiceParameters::binding(config, lane);
                if (b.target != macro || config.engine != ENGINE_RECIPE)
                    continue;
                found = true;
                w.beginObject();
                w.member("label", b.name ? b.name : "Macro");
                w.member("unit", unitName(b.unit));
                w.member("curve", curveName(b.curve));
                w.member("min", b.minimum);
                w.member("max", b.maximum);
                w.key("center");
                if (b.isCentered())
                    w.real(b.center);
                else
                    w.null();
                w.endObject();
                break;
            }
            if (!found)
                w.null();
        }
        w.endArray();

        // How filter.cutoff (0..1) becomes Hz on this preset, with check points the C# tests replay.
        const VoiceParameterLayout &layout = VoiceParameters::layout(config);
        w.key("cutoff");
        w.beginObject();
        w.member("min", layout.cutoffMinimum);
        w.member("max", layout.cutoffMaximum);
        w.member("curve", curveName(layout.cutoffCurve));
        w.key("center");
        if (layout.cutoffCentered())
            w.real(layout.cutoffCenter);
        else
            w.null();
        w.key("samples");
        w.beginArray();
        for (float x : {0.0f, 0.1f, 0.25f, 0.37f, 0.5f, 0.75f, 1.0f})
        {
            w.beginArray(true);
            w.real(x);
            w.real(VoiceParameters::mapCutoff(layout, x));
            w.endArray();
        }
        w.endArray();
        w.endObject();
        w.endObject();
    }
    w.endArray();
    w.endObject();
    return w.take();
}

UserPresetRecord goldenRecord(uint8_t base, uint8_t page, uint8_t pad, const char *name)
{
    UserPresetRecord r;
    usercodec::fromFactory(base, r);
    std::memset(r.name, 0, sizeof r.name);
    std::strncpy(r.name, name, sizeof r.name - 1);
    r.page = page;
    r.pad = pad;
    r.colorR = static_cast<uint8_t>(base * 8);
    r.colorG = static_cast<uint8_t>(255 - base * 8);
    r.colorB = static_cast<uint8_t>(base * 3 + 1);
    usercodec::canonicalize(r);
    return r;
}

std::string goldenJson()
{
    JsonWriter w;
    w.beginObject();
    w.member("format", "pico2seq-golden-vectors");
    w.member("layoutVersion", static_cast<unsigned>(patchfields::kLayoutVersion));

    // Every factory preset as a canonical record on page 1.
    w.key("records");
    w.beginArray();
    for (uint8_t i = 0; i < VoicePresets::getPresetCount(); ++i)
    {
        const UserPresetRecord r = goldenRecord(i, 1, static_cast<uint8_t>(i % kUserPadsPerPage), VoicePresets::getPresetName(i));
        w.beginObject();
        w.member("base", static_cast<unsigned>(i));
        w.member("name", r.name);
        w.member("page", static_cast<unsigned>(r.page));
        w.member("pad", static_cast<unsigned>(r.pad));
        w.member("color", static_cast<unsigned>((r.colorR << 16) | (r.colorG << 8) | r.colorB));
        w.member("hex", hex(reinterpret_cast<const uint8_t *>(&r), sizeof r));
        w.endObject();
    }
    w.endArray();

    // A scripted conversation with a real session: request bytes and the reply the firmware gave.
    testsupport::MemoryBankFile file;
    presetlink::UserPresetStore store(file);
    testsupport::RecordingHost host;
    presetlink::PresetLinkSession session(store, host);
    uint8_t seq = 0;
    uint32_t now = 10;

    w.key("frames");
    w.beginArray();
    const auto step = [&](const char *name, uint8_t command, const std::vector<uint8_t> &payload) {
        std::vector<uint8_t> request;
        const std::vector<uint8_t> reply = testsupport::roundTrip(session, command, ++seq, payload, now += 10, &request);
        w.beginObject();
        w.member("name", name);
        w.member("command", static_cast<unsigned>(command));
        w.member("seq", static_cast<unsigned>(seq));
        w.member("payload", hex(payload));
        w.member("request", hex(request));
        w.member("reply", hex(reply));
        w.endObject();
    };
    const auto bytesOf = [](const UserPresetRecord &r) {
        const auto *p = reinterpret_cast<const uint8_t *>(&r);
        return std::vector<uint8_t>(p, p + sizeof r);
    };
    using namespace presetlink;
    const UserPresetRecord a = goldenRecord(2, 1, 0, "Golden Bass");
    const UserPresetRecord b = goldenRecord(9, 2, 30, "Golden Pluck");
    UserPresetRecord bad = a;
    bad.pad = 5;
    patchfields::write(bad.patch, patchfields::field(static_cast<size_t>(patchfields::indexOfKey("filter.resonance"))), 3.0f);
    std::vector<uint8_t> audition = {1};
    const auto aBytes = bytesOf(a);
    audition.insert(audition.end(), aBytes.begin(), aBytes.end());

    step("hello", Command::Hello, {});
    step("begin", Command::BankBegin, {2, 0});
    step("put_a", Command::BankPut, bytesOf(a));
    step("put_invalid", Command::BankPut, bytesOf(bad));
    step("put_b", Command::BankPut, bytesOf(b));
    step("commit", Command::BankCommit, {});
    step("hello_after", Command::Hello, {});
    step("read_0", Command::BankRead, {0, 0});
    step("read_1", Command::BankRead, {1, 0});
    step("read_out_of_range", Command::BankRead, {2, 0});
    step("audition", Command::Audition, audition);
    step("factory_5", Command::FactoryRead, {5});
    step("voice_2", Command::VoiceRead, {2});
    step("unknown", 0x42, {});
    w.endArray();
    w.endObject();
    return w.take();
}

std::string resourcePath(const char *name)
{
    return std::string(PICO2SEQ_SOURCE_DIR) + "/tools/PresetStudio/PresetStudio.Core/Resources/" + name;
}

void checkResource(const char *name, const std::string &generated)
{
    const std::string path = resourcePath(name);
    const char *update = std::getenv("PICO2SEQ_UPDATE_RESOURCES");
    if (update && update[0] == '1')
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << generated;
        SUCCEED("regenerated " << path);
        return;
    }
    std::ifstream in(path, std::ios::binary);
    INFO("missing " << path << " - run with PICO2SEQ_UPDATE_RESOURCES=1 to create it");
    REQUIRE(in.good());
    std::stringstream buffer;
    buffer << in.rdbuf();
    INFO(name << " is out of date with the firmware tables. Regenerate with:\n"
              << "  PICO2SEQ_UPDATE_RESOURCES=1 ./build_test_ninja/tests/pico2seq_tests \"[resources]\"\n"
              << "and commit the result (docs/preset-studio.md, \"Changing the layout\").");
    REQUIRE(buffer.str() == generated);
}
} // namespace

TEST_CASE("the editor's patch schema matches the firmware", "[resources]")
{
    checkResource("patch-schema.json", schemaJson());
}

TEST_CASE("the editor's factory presets match the firmware", "[resources]")
{
    checkResource("factory-presets.json", factoryJson());
}

TEST_CASE("the editor's golden vectors match the firmware", "[resources]")
{
    checkResource("golden-vectors.json", goldenJson());
}
