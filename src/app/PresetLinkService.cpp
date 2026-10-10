#include "PresetLinkService.h"
#include "AppState.h"
#include "ClockService.h"
#include "UserPresetStorage.h"
#include "VoiceEditor.h"
#include "../presetlink/PresetLinkSession.h"
#include "../ui/UIConstants.h"
#include "../voice/UserPresetCodec.h"
#include "../voice/VoiceEditParameters.h"
#include "../voice/VoicePresets.h"
#include <Arduino.h>
#include <cstring>
#include <uClock.h>

namespace
{
// Serial bytes taken per loop pass. The link is stop-and-wait, so one worst-case frame is all
// the editor can have in flight; the factor of two leaves room for console bytes around it.
// The cap keeps a byte flood from starving the rest of the control loop - the rest waits a pass.
constexpr size_t kMaxBytesPerPass = 2 * presetlink::kMaxFrame;
// How long after the editor's last request stray bytes are still treated as its traffic, not
// console commands (see editorActive()).
constexpr uint32_t kEditorQuietMs = 3000;
// "Receiving presets" stays up this long: an upload is a stream of writes with no natural end
// to hang a shorter notice on, and uploadFinished() replaces it with the result.
constexpr uint32_t kNoticeMs = 6000;

void setNotice(UIState::OledNoticeKind kind, uint32_t durationMs, uint16_t value = 0)
{
    uiState.oledNoticeValue = value;
    uiState.oledNoticeKind = kind;
    uiState.oledNoticeUntil = millis() + durationMs;
}

class FirmwareHost final : public presetlink::PresetLinkHost
{
public:
    bool busy() const { return uploading_; }

    bool transportRunning() override { return isClockRunning; }

    void uploadStarted() override
    {
        // Flash writes stall the cores for milliseconds; the song save stops the clock for the
        // same reason, and so does an upload. Put it back when the bank is settled.
        wasRunning_ = isClockRunning;
        if (wasRunning_)
            stopClockForEditor();
        if (voiceManager)
            voiceManager->flushControlUpdates();
        uploading_ = true;
        setNotice(UIState::OledNoticeKind::PresetsReceiving, kNoticeMs);
    }

    void uploadFinished(bool committed) override
    {
        uploading_ = false;
        if (committed)
        {
            UserPresetStorage::refreshAfterBankChange(uiState);
            const uint16_t count = UserPresetStorage::directory().count();
            setNotice(UIState::OledNoticeKind::PresetsSaved, OLED_NOTICE_DURATION_MS, count);
            Serial.printf("[PRESETS] bank updated: %u user presets\n", static_cast<unsigned>(count));
        }
        else
        {
            setNotice(UIState::OledNoticeKind::PresetsFailed, OLED_NOTICE_DURATION_MS);
            Serial.println("[PRESETS] upload abandoned; previous bank kept");
        }
        if (wasRunning_)
        {
            wasRunning_ = false;
            uClock.start(); // onClockStart restarts all four sequencers
        }
    }

    bool audition(uint8_t voice, const persistence::UserPresetRecord &record) override
    {
        if (!voiceManager || voice >= VoiceSystem::MAX_VOICES)
            return false;
        VoiceConfig config;
        if (!usercodec::toConfig(record, config))
            return false;
        // The same publisher an encoder edit uses: staged to the audio core, glide handled.
        uiState.voicePresetIndices[voice] = record.baseIndex;
        // An audition keeps the editor's name on screen but owns no pad: the sound is not
        // in the bank (yet), so a later bank change must not be able to rename it.
        UserPresetStorage::setVoiceOrigin(uiState, voice, persistence::kNoSlot, record.name);
        VoiceEditor::publish(voice, config);
        return true;
    }

    bool captureVoice(uint8_t voice, persistence::UserPresetRecord &out) override
    {
        if (!voiceManager || voice >= VoiceSystem::MAX_VOICES)
            return false;
        const VoiceConfig *config = voiceManager->getVoiceConfig(voiceSystem.getVoiceId(voice));
        if (!config)
            return false;
        usercodec::fromConfig(*config, uiState.voicePresetIndices[voice], out);
        std::strncpy(out.name, UserPresetStorage::voiceLabel(uiState, voice), persistence::kUserPresetNameSize - 1);
        usercodec::canonicalize(out);
        return true;
    }

private:
    bool wasRunning_ = false;
    bool uploading_ = false;
};

FirmwareHost g_host;

presetlink::PresetLinkSession &session()
{
    static presetlink::PresetLinkSession instance(UserPresetStorage::store(), g_host);
    return instance;
}

presetlink::FrameParser g_parser;
uint8_t g_reply[presetlink::kMaxFrame];
} // namespace

namespace PresetLinkService
{
void poll(uint32_t nowMs, void (*onConsoleByte)(uint8_t))
{
    presetlink::PresetLinkSession &link = session();
    size_t budget = kMaxBytesPerPass;
    int waiting = Serial.available();
    while (waiting > 0 && budget > 0)
    {
        const int c = Serial.read();
        if (c < 0)
            break;
        --waiting;
        --budget;
        switch (g_parser.push(static_cast<uint8_t>(c), nowMs))
        {
        case presetlink::FrameParser::Push::Console:
            if (onConsoleByte)
                onConsoleByte(static_cast<uint8_t>(c));
            break;
        case presetlink::FrameParser::Push::Frame:
        {
            const size_t length = link.handle(g_parser.frame(), nowMs, g_reply, sizeof g_reply);
            if (length)
                Serial.write(g_reply, length);
            break;
        }
        default:
            break;
        }
    }
    link.poll(nowMs);
}

bool busy()
{
    return g_host.busy();
}

bool editorActive(uint32_t nowMs)
{
    return g_parser.inFrame() || (session().everSpoke() && nowMs - session().lastRequestMs() < kEditorQuietMs);
}
} // namespace PresetLinkService
