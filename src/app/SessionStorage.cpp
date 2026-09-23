#include "SessionStorage.h"
#include "../pico2seq-core/persistence/SnapshotFormat.h"
#include <Arduino.h>
#include <LittleFS.h>

namespace
{
constexpr const char *kSavePath = "/session.p2s";
constexpr const char *kTempPath = "/session.tmp";

// Static, not stack: ~12.4 KB each, beyond what the Core 0 loop stack can hold.
persistence::ProjectSnapshot g_saveBuffer;
persistence::ProjectSnapshot g_loadBuffer;
} // namespace

bool SessionStorage::begin()
{
    LittleFSConfig cfg;
    cfg.setAutoFormat(false); // Format explicitly so the wait is logged, not hidden
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
    // v1 and v2 payloads are prefixes of the current format (see
    // ProjectSnapshot); the missing tails are filled by the upgrades below.
    const uint16_t version = persistence::frameVersion(header);
    size_t payloadSize;
    if (version == persistence::SNAPSHOT_FORMAT_VERSION_V1)
        payloadSize = sizeof(persistence::ProjectSnapshotV1);
    else if (version == persistence::SNAPSHOT_FORMAT_VERSION_V2)
        payloadSize = offsetof(persistence::ProjectSnapshot, sitar);
    else
        payloadSize = sizeof(persistence::ProjectSnapshot);
    if (f.read(reinterpret_cast<uint8_t *>(&g_loadBuffer), payloadSize) !=
        static_cast<int>(payloadSize))
    {
        f.close();
        return LoadResult::BadFrame; // truncated file
    }
    f.close();
    // Header and payload CRC separately: never read past the 12-byte header.
    const persistence::FrameStatus status = persistence::readFrameHeader(
        header, reinterpret_cast<const uint8_t *>(&g_loadBuffer), sizeof(g_loadBuffer),
        static_cast<uint16_t>(payloadSize),
        (version == persistence::SNAPSHOT_FORMAT_VERSION_V1 ||
         version == persistence::SNAPSHOT_FORMAT_VERSION_V2)
            ? version
            : persistence::SNAPSHOT_FORMAT_VERSION);
    if (status != persistence::FrameStatus::Ok)
        return LoadResult::BadFrame;
    if (version == persistence::SNAPSHOT_FORMAT_VERSION_V1)
        persistence::upgradeFromV1(g_loadBuffer);
    else if (version == persistence::SNAPSHOT_FORMAT_VERSION_V2)
        persistence::upgradeFromV2(g_loadBuffer);
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
