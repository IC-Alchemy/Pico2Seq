// SessionStorage implementation for the desktop port.
//
// Byte-identical session files to the firmware: same 12-byte frame header,
// CRC32, payload layout, and atomic write-then-rename policy — just on a
// real filesystem (%APPDATA%\Pico2Seq by default) instead of LittleFS.

#include "DesktopStorage.h"
#include "app/SessionStorage.h"
#include "pico2seq-core/persistence/SnapshotFormat.h"

#if defined(_WIN32)
// Before <Arduino.h>: this TU also pulls in the Windows SDK, whose
// winuser.h INPUT typedef must not meet the Arduino INPUT macro.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <Arduino.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
namespace fs = std::filesystem;

std::string g_overrideDir;
std::string g_dir;

std::string resolveBaseDir()
{
    if (!g_overrideDir.empty())
        return g_overrideDir;
    const char *appData = std::getenv("APPDATA");
    if (appData && *appData)
        return std::string(appData) + "\\Pico2Seq";
    return "Pico2SeqData"; // portable fallback next to the working directory
}
} // namespace

namespace p2s
{
namespace storage
{
void setOverrideDir(const char *dir) { g_overrideDir = dir ? dir : ""; }
} // namespace storage
} // namespace p2s

bool SessionStorage::begin()
{
    g_dir = resolveBaseDir();
    std::error_code ec;
    fs::create_directories(g_dir, ec);
    if (ec)
    {
        Serial.println("[STORAGE] cannot create session directory");
        return false;
    }
    Serial.print("[STORAGE] session dir: ");
    Serial.println(g_dir.c_str());
    return true;
}

SessionStorage::LoadResult SessionStorage::load(persistence::ProjectSnapshotV1 &out)
{
    // Same static-buffer discipline as the firmware: snapshots are ~10.1 KB.
    static persistence::ProjectSnapshotV1 loadBuffer;

    const fs::path path = fs::path(g_dir) / "session.p2s";
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return LoadResult::NoFile;
    const std::streamsize size = file.tellg();
    if (size != static_cast<std::streamsize>(12 + sizeof(loadBuffer)))
        return LoadResult::BadFrame; // truncated or foreign file
    file.seekg(0);
    uint8_t header[12];
    if (!file.read(reinterpret_cast<char *>(header), sizeof(header)) ||
        !file.read(reinterpret_cast<char *>(&loadBuffer), sizeof(loadBuffer)))
        return LoadResult::BadFrame;

    const persistence::FrameStatus status = persistence::readFrameHeader(
        header, reinterpret_cast<const uint8_t *>(&loadBuffer), sizeof(loadBuffer),
        sizeof(persistence::ProjectSnapshotV1));
    if (status != persistence::FrameStatus::Ok)
        return LoadResult::BadFrame;
    if (!persistence::validateProjectSnapshot(loadBuffer))
        return LoadResult::BadFrame;
    out = loadBuffer;
    return LoadResult::Ok;
}

bool SessionStorage::save(const persistence::ProjectSnapshotV1 &snap)
{
    static persistence::ProjectSnapshotV1 saveBuffer;

    saveBuffer = snap;
    const uint32_t crc = persistence::crc32(
        reinterpret_cast<const uint8_t *>(&saveBuffer), sizeof(saveBuffer));
    uint8_t header[12];
    persistence::writeFrameHeader(header, sizeof(saveBuffer), crc);

    const fs::path dir(g_dir);
    const fs::path tmpPath = dir / "session.tmp";
    const fs::path dstPath = dir / "session.p2s";

    {
        std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;
        file.write(reinterpret_cast<const char *>(header), sizeof(header));
        file.write(reinterpret_cast<const char *>(&saveBuffer), sizeof(saveBuffer));
        if (!file)
            return false;
    }

#if defined(_WIN32)
    // std::filesystem::rename cannot replace an existing file on Windows;
    // MoveFileEx with REPLACE_EXISTING keeps the swap atomic like LittleFS.
    if (!MoveFileExW(tmpPath.wstring().c_str(), dstPath.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING))
        return false;
#else
    std::error_code ec;
    fs::rename(tmpPath, dstPath, ec);
    if (ec)
        return false;
#endif
    return true;
}
