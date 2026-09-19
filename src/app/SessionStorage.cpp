#include "SessionStorage.h"
#include "../pico2seq-core/persistence/SnapshotFormat.h"
#include <Arduino.h>
#include <LittleFS.h>

namespace
{
constexpr const char *kSavePath = "/session.p2s";
constexpr const char *kTempPath = "/session.tmp";

// Static, not stack: ~12.4 KB each and this runs on the Core 0 Arduino loop
// stack during saves/loads.
persistence::ProjectSnapshot g_saveBuffer;
persistence::ProjectSnapshot g_loadBuffer;
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

SessionStorage::LoadResult SessionStorage::load(persistence::ProjectSnapshot &out)
{
    if (!LittleFS.exists(kSavePath))
        return LoadResult::NoFile;
    File f = LittleFS.open(kSavePath, "r");
    if (!f)
        return LoadResult::IoError;
    uint8_t header[12];
    if (f.read(header, 12) != 12)
    {
        f.close();
        return LoadResult::BadFrame;
    }
    // A format-1 payload is the prefix of format 2 and is completed below.
    const uint16_t version = persistence::frameVersion(header);
    const bool formatV1 = version == persistence::SNAPSHOT_FORMAT_VERSION_V1;
    const size_t payloadSize = formatV1 ? sizeof(persistence::ProjectSnapshotV1)
                                        : sizeof(persistence::ProjectSnapshot);
    if (f.read(reinterpret_cast<uint8_t *>(&g_loadBuffer), payloadSize) !=
        static_cast<int>(payloadSize))
    {
        f.close();
        return LoadResult::BadFrame; // truncated file
    }
    f.close();
    // Header and payload live in separate buffers; the validator CRCs the
    // payload buffer directly (never bytes past the 12-byte header).
    const persistence::FrameStatus status = persistence::readFrameHeader(
        header, reinterpret_cast<const uint8_t *>(&g_loadBuffer), sizeof(g_loadBuffer),
        static_cast<uint16_t>(payloadSize),
        formatV1 ? persistence::SNAPSHOT_FORMAT_VERSION_V1 : persistence::SNAPSHOT_FORMAT_VERSION);
    if (status != persistence::FrameStatus::Ok)
        return LoadResult::BadFrame;
    if (formatV1)
        persistence::upgradeFromV1(g_loadBuffer);
    if (!persistence::validateProjectSnapshot(g_loadBuffer))
        return LoadResult::BadFrame;
    out = g_loadBuffer;
    return LoadResult::Ok;
}

bool SessionStorage::save(const persistence::ProjectSnapshot &snap)
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
