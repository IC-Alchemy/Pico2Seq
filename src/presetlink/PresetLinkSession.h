// PresetLinkSession - answers the PC editor's requests.
// Musical role: "send my presets to the box", "play this sound on voice 2 right now",
// "grab the sound I just tweaked on the hardware". Each one either happens completely or
// is refused with a reason; the box keeps playing its old bank until a new one is committed.
// Technical role: a request/reply state machine over UserPresetStore. It owns no hardware:
// the firmware plugs in the voice actions and transport pause through PresetLinkHost.
// Core 0 control thread only. Portable C++.
#ifndef PICO2SEQ_PRESET_LINK_SESSION_H
#define PICO2SEQ_PRESET_LINK_SESSION_H

#include "PresetLinkProtocol.h"
#include "UserPresetStore.h"

namespace presetlink
{

class PresetLinkHost
{
public:
    virtual ~PresetLinkHost() = default;
    virtual bool transportRunning() = 0;
    // A bank upload started / ended. The firmware stops the clock around flash writes (as
    // the song save does) and refreshes the browser when a new bank became live.
    virtual void uploadStarted() = 0;
    virtual void uploadFinished(bool committed) = 0;
    // Play a record's sound on a voice (0-3) now, the way an encoder edit would.
    virtual bool audition(uint8_t voice, const persistence::UserPresetRecord &record) = 0;
    // Capture a voice (0-3) as a record: base preset, patch values.
    virtual bool captureVoice(uint8_t voice, persistence::UserPresetRecord &out) = 0;
};

class PresetLinkSession
{
public:
    // An upload that goes quiet this long is abandoned (cable pulled, editor closed).
    static constexpr uint32_t kUploadIdleTimeoutMs = 5000;

    PresetLinkSession(UserPresetStore &store, PresetLinkHost &host) : store_(store), host_(host) {}

    // Handles one valid request frame and writes the reply frame into `out`
    // (capacity >= kMaxFrame). Returns the reply length.
    size_t handle(const FrameParser::Frame &request, uint32_t nowMs, uint8_t *out,
                  size_t capacity) noexcept;

    // Call every loop pass: abandons a stalled upload.
    void poll(uint32_t nowMs) noexcept;

    // Time of the last request seen (0 before the first); the firmware uses it to keep
    // stray serial bytes from being mistaken for console commands while the editor is talking.
    uint32_t lastRequestMs() const noexcept { return lastRequestMs_; }
    bool everSpoke() const noexcept { return spoke_; }

private:
    size_t reply(const FrameParser::Frame &request, const uint8_t *payload, size_t length,
                 uint8_t *out, size_t capacity) noexcept;
    size_t error(const FrameParser::Frame &request, ErrorCode code, uint8_t detail, uint8_t aux,
                 uint8_t *out, size_t capacity) noexcept;
    // An error frame with no detail byte and no patch field (most refusals).
    size_t fail(const FrameParser::Frame &request, ErrorCode code, uint8_t *out,
                size_t capacity) noexcept;
    size_t uploadError(const FrameParser::Frame &request, const UserPresetStore::UploadError &e,
                       uint8_t *out, size_t capacity) noexcept;
    size_t record(const FrameParser::Frame &request, const persistence::UserPresetRecord &rec,
                  uint8_t *out, size_t capacity) noexcept;
    void endUpload(bool committed) noexcept;

    UserPresetStore &store_;
    PresetLinkHost &host_;
    persistence::UserPresetRecord work_{};
    uint32_t lastRequestMs_ = 0;
    uint32_t lastUploadActivityMs_ = 0;
    bool spoke_ = false;
};

} // namespace presetlink

#endif
