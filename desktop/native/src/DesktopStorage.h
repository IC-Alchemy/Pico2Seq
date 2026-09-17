#pragma once
// Desktop-owned storage configuration hook. The SessionStorage API and the
// snapshot framing (12-byte header + CRC32 + payload) are shared with the
// firmware; only the file layer differs (std::filesystem vs LittleFS).

namespace p2s
{
namespace storage
{
// Redirect the session directory (tests / portable installs). Takes effect
// from the next SessionStorage::begin(); pass nullptr to clear.
void setOverrideDir(const char *dir);
}
}
