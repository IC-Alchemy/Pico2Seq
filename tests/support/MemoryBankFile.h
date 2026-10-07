// MemoryBankFile - host-side stand-in for the LittleFS bank file (tests and the link
// simulator). Models the two properties the store depends on: the live file only changes
// when commitWrite() succeeds, and writes can fail on demand.
#pragma once

#include "presetlink/UserPresetStore.h"
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace testsupport
{

class MemoryBankFile : public presetlink::UserPresetFile
{
public:
    std::vector<uint8_t> live;      // the committed file
    std::vector<uint8_t> temp;      // the file being written
    bool hasLive = false;
    bool writing = false;
    size_t capacity = 24 * 1024;    // bytes the "filesystem" can hold for the bank
    int failWriteAfter = -1;        // fail the Nth write() call (0-based); -1 never
    bool failCommit = false;
    int writeCalls = 0;
    int commits = 0;

    int32_t size() override { return hasLive ? static_cast<int32_t>(live.size()) : -1; }
    bool read(size_t offset, uint8_t *data, size_t length) override
    {
        if (!hasLive || offset + length > live.size())
            return false;
        std::copy_n(live.begin() + offset, length, data);
        return true;
    }
    bool canWrite(size_t bytes) override { return bytes <= capacity; }
    uint32_t freeBytes() override { return static_cast<uint32_t>(capacity); }
    bool beginWrite() override
    {
        temp.clear();
        writing = true;
        writeCalls = 0;
        return true;
    }
    bool write(const uint8_t *data, size_t length) override
    {
        if (!writing)
            return false;
        if (failWriteAfter >= 0 && writeCalls++ >= failWriteAfter)
            return false;
        temp.insert(temp.end(), data, data + length);
        return true;
    }
    bool commitWrite() override
    {
        if (!writing || failCommit)
            return false;
        live = temp;
        hasLive = true;
        writing = false;
        ++commits;
        return true;
    }
    void abortWrite() override
    {
        temp.clear();
        writing = false;
    }
};

// The same, persisted in a real file (path + ".tmp" while writing) so the simulator can
// "power cycle" between runs.
class DiskBankFile : public MemoryBankFile
{
public:
    explicit DiskBankFile(std::string path) : path_(std::move(path))
    {
        std::ifstream in(path_, std::ios::binary);
        if (in)
        {
            live.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            hasLive = true;
        }
    }
    bool commitWrite() override
    {
        if (!MemoryBankFile::commitWrite())
            return false;
        std::ofstream out(path_ + ".tmp", std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(live.data()), static_cast<std::streamsize>(live.size()));
        out.close();
        return std::rename((path_ + ".tmp").c_str(), path_.c_str()) == 0;
    }

private:
    std::string path_;
};

} // namespace testsupport
