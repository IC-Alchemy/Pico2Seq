#include "UserPresetStorage.h"
#include "../ui/UIState.h"
#include "../voice/VoicePresets.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <cstring>

namespace
{
constexpr const char *kLivePath = "/presets.p2u";
constexpr const char *kTempPath = "/presets.tmp";

// LittleFS blocks are 4 KB. A file costs its data blocks (plus a few pointer bytes per
// block) and, while it is being replaced, the old file stays too; keep two spare blocks
// for metadata so a tight filesystem refuses the upload up front instead of failing halfway.
constexpr uint32_t kSpareBlocks = 2;
// Generous allowance for LittleFS's own bookkeeping inside each block: erring high only
// makes canWrite() refuse a little early, which is the safe side.
constexpr uint32_t kBlockOverheadBytes = 16;

class LittleFsBankFile final : public presetlink::UserPresetFile
{
public:
    int32_t size() override
    {
        File f = LittleFS.open(kLivePath, "r");
        if (!f)
            return -1;
        const int32_t n = static_cast<int32_t>(f.size());
        f.close();
        return n;
    }

    bool read(size_t offset, uint8_t *data, size_t length) override
    {
        File f = LittleFS.open(kLivePath, "r");
        if (!f)
            return false;
        const bool ok = f.seek(static_cast<uint32_t>(offset)) &&
                        f.read(data, length) == static_cast<int>(length);
        f.close();
        return ok;
    }

    bool canWrite(size_t bytes) override
    {
        FSInfo info;
        if (!LittleFS.info(info) || info.blockSize == 0)
            return false;
        if (LittleFS.exists(kTempPath))
            LittleFS.remove(kTempPath); // a torn upload from a previous boot
        if (!LittleFS.info(info))
            return false;
        const uint32_t usable = info.blockSize > kBlockOverheadBytes ? info.blockSize - kBlockOverheadBytes
                                                                     : info.blockSize;
        const uint32_t needBlocks = static_cast<uint32_t>((bytes + usable - 1) / usable) + kSpareBlocks;
        const uint32_t freeBlocks = (info.totalBytes - info.usedBytes) / info.blockSize;
        return freeBlocks >= needBlocks;
    }

    uint32_t freeBytes() override
    {
        FSInfo info;
        if (!LittleFS.info(info) || info.usedBytes > info.totalBytes)
            return 0;
        return static_cast<uint32_t>(info.totalBytes - info.usedBytes);
    }

    bool beginWrite() override
    {
        if (temp_)
            temp_.close();
        temp_ = LittleFS.open(kTempPath, "w");
        return static_cast<bool>(temp_);
    }

    bool write(const uint8_t *data, size_t length) override
    {
        return temp_ && temp_.write(data, length) == length;
    }

    bool commitWrite() override
    {
        if (!temp_)
            return false;
        temp_.close();
        return LittleFS.rename(kTempPath, kLivePath);
    }

    void abortWrite() override
    {
        if (temp_)
            temp_.close();
        LittleFS.remove(kTempPath);
    }

private:
    File temp_;
};

// FNV-1a folded to one byte. It is the tamper check saved beside a voice's slot tag (see
// sessionCheck): not security, just enough that a song saved before the bank was replaced
// cannot silently re-label a voice with whatever preset now sits in that pad.
uint8_t nameHash(const char *name)
{
    uint32_t h = 2166136261u;
    for (const char *p = name; *p; ++p)
    {
        h ^= static_cast<uint8_t>(*p);
        h *= 16777619u;
    }
    return static_cast<uint8_t>(h ^ (h >> 8) ^ (h >> 16) ^ (h >> 24));
}
} // namespace

namespace UserPresetStorage
{
presetlink::UserPresetStore &store()
{
    // Built on first use, after the filesystem is mounted: no static-init-order hazard.
    static LittleFsBankFile file;
    static presetlink::UserPresetStore instance(file);
    return instance;
}

const persistence::UserPresetDirectory &directory()
{
    return store().directory();
}

uint16_t begin()
{
    const uint16_t shown = store().load();
    Serial.printf("[PRESETS] %u user preset%s on flash\n", static_cast<unsigned>(shown), shown == 1 ? "" : "s");
    return shown;
}

void setVoiceOrigin(UIState &state, uint8_t voice, uint8_t slot, const char *name)
{
    if (voice >= UIState::MAX_VOICES)
        return;
    char *dest = state.voiceUserName[voice];
    state.voiceUserSlot[voice] = slot;
    dest[0] = '\0';
    if (name)
        std::strncpy(dest, name, sizeof state.voiceUserName[voice] - 1);
    // strncpy does not terminate a name that fills the buffer.
    dest[sizeof state.voiceUserName[voice] - 1] = '\0';
}

const char *voiceLabel(const UIState &state, uint8_t voice)
{
    const uint8_t v = voice < UIState::MAX_VOICES ? voice : 0;
    if (state.voiceUserName[v][0] != '\0')
        return state.voiceUserName[v];
    return VoicePresets::getPresetName(state.voicePresetIndices[v]);
}

uint8_t sessionTag(const UIState &state, uint8_t voice)
{
    if (voice >= UIState::MAX_VOICES || state.voiceUserSlot[voice] >= persistence::kUserSlotCount)
        return 0;
    return static_cast<uint8_t>(state.voiceUserSlot[voice] + 1);
}

uint8_t sessionCheck(const UIState &state, uint8_t voice)
{
    return sessionTag(state, voice) ? nameHash(state.voiceUserName[voice]) : 0;
}

void restoreFromSession(UIState &state, uint8_t voice, uint8_t tag, uint8_t check)
{
    // Start from "factory"; only a tag that still checks out earns its pad back.
    setVoiceOrigin(state, voice, persistence::kNoSlot, nullptr);
    if (tag == 0 || tag > persistence::kUserSlotCount)
        return;
    const persistence::UserPresetEntry *entry = directory().slot(static_cast<uint8_t>(tag - 1));
    if (!entry || entry->baseIndex != state.voicePresetIndices[voice] || nameHash(entry->name) != check)
        return;
    setVoiceOrigin(state, voice, static_cast<uint8_t>(tag - 1), entry->name);
}

void refreshAfterBankChange(UIState &state)
{
    state.presetPage = directory().clampPage(state.presetPage);
    for (uint8_t v = 0; v < UIState::MAX_VOICES; ++v)
    {
        if (state.voiceUserSlot[v] == persistence::kNoSlot)
            continue;
        const persistence::UserPresetEntry *entry = directory().slot(state.voiceUserSlot[v]);
        // The voice keeps sounding (and keeps its name); it just no longer owns a pad.
        if (!entry || std::strncmp(entry->name, state.voiceUserName[v], persistence::kUserPresetNameSize) != 0)
            state.voiceUserSlot[v] = persistence::kNoSlot;
    }
}
} // namespace UserPresetStorage
