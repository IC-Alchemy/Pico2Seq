#include "SessionStorage.h"
#include "../pico2seq-core/persistence/SnapshotFormat.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <cstring>

namespace
{
constexpr const char *kSavePath = "/session.p2s";
constexpr const char *kTempPath = "/session.tmp";
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
    // An older payload is a prefix of the newest layout; the version picks how many
    // bytes to read and decodeSnapshotFrame() completes (upgrades) the rest. A
    // version newer than this build knows has size 0 and is refused.
    const uint16_t version = persistence::frameVersion(header);
    const size_t payloadSize = persistence::payloadSizeForVersion(version);
    if (payloadSize == 0)
    {
        f.close();
        return LoadResult::BadFrame;
    }
    // The caller's snapshot is a file-static (~12.4 KB, too big for the stack), so the
    // frame is read and decoded straight into it: a private second copy here cost
    // 12,460 bytes of heap for nothing. The price is that `out` is scratch on any
    // result other than Ok; both callers (Application boot and the Load action) only
    // read it after Ok and rebuild it with captureSession() before anything else.
    // An older frame is a prefix of the layout: clear the rest so the upgrade (and the
    // CRC the caller takes of the result) never sees whatever `out` held before.
    if (payloadSize < sizeof(out))
        std::memset(reinterpret_cast<uint8_t *>(&out) + payloadSize, 0, sizeof(out) - payloadSize);
    if (f.read(reinterpret_cast<uint8_t *>(&out), payloadSize) !=
        static_cast<int>(payloadSize))
    {
        f.close();
        return LoadResult::BadFrame; // truncated file
    }
    f.close();
    // Header and payload CRC separately: never read past the 12-byte header. The
    // payload is decoded in place (decodeSnapshotFrame skips the copy when the
    // payload already is `out`, then upgrades older layouts and validates).
    if (!persistence::decodeSnapshotFrame(header, reinterpret_cast<const uint8_t *>(&out),
                                          sizeof(out), out))
        return LoadResult::BadFrame;
    return LoadResult::Ok;
}

bool SessionStorage::save(const persistence::ProjectSnapshot &snap)
{
    // Callers pass a file-static snapshot (e.g. Application::g_sessionSnapshot)
    // that cannot change during this call, so it is CRC'd and written in place.
    const uint32_t crc = persistence::crc32(reinterpret_cast<const uint8_t *>(&snap),
                                            sizeof(snap));
    uint8_t header[12];
    persistence::writeFrameHeader(header, sizeof(snap), crc);

    File f = LittleFS.open(kTempPath, "w");
    if (!f)
        return false;
    const bool wrote = f.write(header, 12) == 12 &&
                       f.write(reinterpret_cast<const uint8_t *>(&snap), sizeof(snap)) ==
                           sizeof(snap);
    f.close();
    if (!wrote)
        return false;
    return LittleFS.rename(kTempPath, kSavePath);
}
