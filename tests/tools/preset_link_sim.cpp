// preset_link_sim - the firmware's preset link as a host process, for testing the PC editor.
//
// Speaks the real wire protocol on stdin/stdout using the very same frame parser, command
// session, bank store and validator the firmware runs; only the flash (a file) and the voices
// (four plain configs) are stand-ins. The C# tests in tools/PresetStudio launch this and run
// the real client against it, so protocol and layout drift is caught without hardware.
//
//   preset_link_sim [--bank FILE] [--chatty] [--capacity BYTES]
//     --bank      persist the bank in FILE (survives restarts, like flash)
//     --chatty    print firmware-style log lines between frames, as the real port does
//     --capacity  pretend the filesystem has this much room for the bank (default 20000)
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "../support/LinkRig.h"
#include "../support/MemoryBankFile.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace presetlink;

int main(int argc, char **argv)
{
    std::string bankPath;
    bool chatty = false;
    size_t capacity = 20000;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--bank") && i + 1 < argc)
            bankPath = argv[++i];
        else if (!std::strcmp(argv[i], "--chatty"))
            chatty = true;
        else if (!std::strcmp(argv[i], "--capacity") && i + 1 < argc)
            capacity = static_cast<size_t>(std::strtoul(argv[++i], nullptr, 10));
        else
        {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    std::unique_ptr<testsupport::MemoryBankFile> file =
        bankPath.empty() ? std::make_unique<testsupport::MemoryBankFile>()
                         : std::make_unique<testsupport::DiskBankFile>(bankPath);
    file->capacity = capacity;
    UserPresetStore store(*file);
    store.load();
    testsupport::RecordingHost host;
    PresetLinkSession session(store, host);
    FrameParser parser;
    uint8_t reply[kMaxFrame];

    const auto start = std::chrono::steady_clock::now();
    const auto nowMs = [&]() {
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    };
    const auto log = [&](const char *text) {
        if (chatty)
        {
            std::fputs(text, stdout);
            std::fflush(stdout);
        }
    };

    log("[PRESETS] simulator ready\n");
    int c;
    while ((c = std::getchar()) != EOF)
    {
        const uint32_t now = nowMs();
        switch (parser.push(static_cast<uint8_t>(c), now))
        {
        case FrameParser::Push::Frame:
        {
            log("[DIAG C0] ids=0,1,2,3 steps=42\n");
            const size_t n = session.handle(parser.frame(), now, reply, sizeof reply);
            std::fwrite(reply, 1, n, stdout);
            std::fflush(stdout);
            log("[PRESETS] request handled\n");
            break;
        }
        case FrameParser::Push::Dropped:
            log("[PRESETS] dropped a damaged frame\n");
            break;
        default:
            break;
        }
        session.poll(now);
    }
    return 0;
}
