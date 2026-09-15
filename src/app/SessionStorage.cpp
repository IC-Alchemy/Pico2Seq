#include "SessionStorage.h"
#include "../pico2seq-core/persistence/SnapshotFormat.h"
#include <Arduino.h>
#include <LittleFS.h>

namespace
{
constexpr const char *kSavePath = "/session.p2s";
constexpr const char *kTempPath = "/session.tmp";

// Static, not stack: ~10.1 KB each and this runs on the Core 0 Arduino loop
// stack during saves/loads.
persistence::ProjectSnapshotV1 g_saveBuffer;
persistence::ProjectSnapshotV1 g_loadBuffer;
} // namespace

bool SessionStorage::begin()
{
    LittleFSConfig cfg;
    cfg.setAutoFormat(false); // format explicitly so we can log/measure it
    LittleFS.setConfig(cfg);
    if (LittleFS.begin())
        return true;
    Serial.println("[STORAGE] first boot: formatting LittleFS partition");
    if (!LittleFS.format())
    {
        Serial.println("[STORAGE] format FAILED; persistence disabled this boot");
        return false;
    }
    return LittleFS.begin();
}

SessionStorage::LoadResult SessionStorage::load(persistence::ProjectSnapshotV1 &out)
{
    if (!LittleFS.exists(kSavePath))
        return LoadResult::NoFile;
    File f = LittleFS.open(kSavePath, "r");
    if (!f)
        return LoadResult::IoError;
    uint8_t header[12];
    if (f.read(header, 12) != 12 ||
        f.read(reinterpret_cast<uint8_t *>(&g_loadBuffer), sizeof(g_loadBuffer)) !=
            static_cast<int>(sizeof(g_loadBuffer)))
    {
        f.close();
        return LoadResult::BadFrame; // truncated file
    }
    f.close();
    // Header and payload live in separate buffers; the validator CRCs the
    // payload buffer directly (never bytes past the 12-byte header).
    const persistence::FrameStatus status = persistence::readFrameHeader(
        header, reinterpret_cast<const uint8_t *>(&g_loadBuffer), sizeof(g_loadBuffer),
        sizeof(persistence::ProjectSnapshotV1));
    if (status != persistence::FrameStatus::Ok)
        return LoadResult::BadFrame;
    if (!persistence::validateProjectSnapshot(g_loadBuffer))
        return LoadResult::BadFrame;
    out = g_loadBuffer;
    return LoadResult::Ok;
}

bool SessionStorage::save(const persistence::ProjectSnapshotV1 &snap)
{
    g_saveBuffer = snap;
    const uint32_t crc = persistence::crc32(
        reinterpret_cast<const uint8_t *>(&g_saveBuffer), sizeof(g_saveBuffer));
    uint8_t header[12];
    persistence::writeFrameHeader(header, sizeof(g_saveBuffer), crc);

    File f = LittleFS.open(kTempPath, "w");
    if (!f)
        return false;
    const bool wrote = f.write(header, 12) == 12 &&
                       f.write(reinterpret_cast<const uint8_t *>(&g_saveBuffer),
                               sizeof(g_saveBuffer)) == sizeof(g_saveBuffer);
    f.close();
    if (!wrote)
        return false;
    return LittleFS.rename(kTempPath, kSavePath);
}
