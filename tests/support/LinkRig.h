// LinkRig - Catch-free helpers for driving the preset link from a test or from the
// device simulator: a byte-wise wire reader and a recording host.
#pragma once

#include "presetlink/PresetLinkProtocol.h"
#include "presetlink/PresetLinkSession.h"
#include "voice/UserPresetCodec.h"
#include "voice/VoiceConfig.h"
#include "voice/VoicePresets.h"
#include <cstring>
#include <string>
#include <vector>

namespace testsupport
{

// Feeds bytes to a FrameParser; collects frames (payloads copied) and console bytes.
struct Wire
{
    presetlink::FrameParser parser;
    uint32_t now = 1000;
    struct Got
    {
        uint8_t type;
        uint8_t seq;
        std::vector<uint8_t> payload;
    };
    std::vector<Got> frames;
    std::string console;
    int dropped = 0;

    void feed(const uint8_t *bytes, size_t n, uint32_t stepMs = 0)
    {
        using P = presetlink::FrameParser::Push;
        for (size_t i = 0; i < n; ++i)
        {
            now += stepMs;
            switch (parser.push(bytes[i], now))
            {
            case P::Console: console.push_back(static_cast<char>(bytes[i])); break;
            case P::Frame:
            {
                const auto &f = parser.frame();
                frames.push_back({f.type, f.seq, std::vector<uint8_t>(f.payload, f.payload + f.length)});
                break;
            }
            case P::Dropped: ++dropped; break;
            case P::Pending: break;
            }
        }
    }
    void feed(const std::vector<uint8_t> &bytes, uint32_t stepMs = 0) { feed(bytes.data(), bytes.size(), stepMs); }
};

// A host with four plain voices and no hardware.
struct RecordingHost : presetlink::PresetLinkHost
{
    bool running = true;
    int started = 0, finished = 0;
    bool lastCommitted = false;
    int auditions = 0;
    uint8_t auditionVoice = 0xFF;
    persistence::UserPresetRecord auditioned{};
    bool failAudition = false;
    VoiceConfig voices[4];
    uint8_t bases[4]; // the factory preset each voice was last built on

    RecordingHost()
    {
        for (int i = 0; i < 4; ++i)
        {
            bases[i] = static_cast<uint8_t>(i + 2);
            voices[i] = VoicePresets::getPresetConfig(bases[i]);
        }
    }
    bool transportRunning() override { return running; }
    void uploadStarted() override { ++started; }
    void uploadFinished(bool committed) override
    {
        ++finished;
        lastCommitted = committed;
    }
    bool audition(uint8_t voice, const persistence::UserPresetRecord &r) override
    {
        if (failAudition)
            return false;
        ++auditions;
        auditionVoice = voice;
        auditioned = r;
        bases[voice] = r.baseIndex;
        usercodec::toConfig(r, voices[voice]);
        return true;
    }
    bool captureVoice(uint8_t voice, persistence::UserPresetRecord &out) override
    {
        usercodec::fromConfig(voices[voice], bases[voice], out);
        std::strncpy(out.name, "Captured", sizeof out.name - 1);
        usercodec::canonicalize(out);
        return true;
    }
};

// Sends one request through a session and returns the raw reply frame bytes.
inline std::vector<uint8_t> roundTrip(presetlink::PresetLinkSession &session, uint8_t type, uint8_t seq,
                                      const std::vector<uint8_t> &payload, uint32_t now,
                                      std::vector<uint8_t> *requestOut = nullptr)
{
    std::vector<uint8_t> request(presetlink::kFrameOverhead + payload.size());
    const size_t n = presetlink::encodeFrame(type, seq, payload.data(), payload.size(), request.data(),
                                             request.size());
    request.resize(n);
    if (requestOut)
        *requestOut = request;
    Wire in;
    in.feed(request);
    if (in.frames.empty())
        return {};
    presetlink::FrameParser::Frame frame;
    frame.type = in.frames[0].type;
    frame.seq = in.frames[0].seq;
    frame.length = static_cast<uint16_t>(in.frames[0].payload.size());
    frame.payload = in.frames[0].payload.data();
    uint8_t out[presetlink::kMaxFrame];
    const size_t len = session.handle(frame, now, out, sizeof out);
    return std::vector<uint8_t>(out, out + len);
}

} // namespace testsupport
