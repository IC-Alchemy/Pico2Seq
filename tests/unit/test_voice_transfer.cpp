#include <catch2/catch_test_macros.hpp>
#include "utils/SpscQueue.h"
#include "voice/Voice.h"
#include "voice/VoiceManager.h"
#include <array>
#include <atomic>
#include <cmath>
#include <thread>

namespace {
VoiceConfig testConfig()
{
    VoiceConfig config;
    config.oscillatorCount = 1;
    config.oscWaveforms[0] = WAVE_SIN;
    config.hasFilter = false;
    return config;
}

VoiceState noteState(unsigned note, bool gate = true)
{
    VoiceState state;
    state.noteIndex = static_cast<float>(note);
    state.velocityLevel = note / 32.0f;
    state.filterCutoff = note / 64.0f;
    state.attackTimeSeconds = note / 128.0f;
    state.isGateHigh = gate;
    return state;
}
}

TEST_CASE("Queue preserves occupied slots when full and wraps in FIFO order", "[voice_transfer]")
{
    SpscQueue<std::array<unsigned, 3>, 2> queue;
    std::array<unsigned, 3> out{99, 99, 99};
    REQUIRE_FALSE(queue.tryPop(out));
    REQUIRE(out[0] == 99);
    for (unsigned i = 0; i < 100; ++i)
    {
        REQUIRE(queue.tryPush({i, i + 1, i + 2}));
        REQUIRE(queue.tryPush({i + 3, i + 4, i + 5}));
        REQUIRE_FALSE(queue.tryPush({0, 0, 0}));
        REQUIRE(queue.tryPop(out));
        REQUIRE(out == std::array<unsigned, 3>{i, i + 1, i + 2});
        REQUIRE(queue.tryPop(out));
        REQUIRE(out == std::array<unsigned, 3>{i + 3, i + 4, i + 5});
        REQUIRE_FALSE(queue.tryPop(out));
    }
}

TEST_CASE("Concurrent queue transfers never tear multiword payloads", "[voice_transfer][concurrency]")
{
    constexpr unsigned count = 50000;
    SpscQueue<std::array<unsigned, 64>, 4> queue;
    std::thread producer([&] {
        for (unsigned sequence = 0; sequence < count; ++sequence)
        {
            std::array<unsigned, 64> packet;
            for (unsigned i = 0; i < packet.size(); ++i)
                packet[i] = sequence * 67 + i;
            while (!queue.tryPush(packet))
                std::this_thread::yield();
        }
    });
    bool intact = true;
    for (unsigned sequence = 0; sequence < count; ++sequence)
    {
        std::array<unsigned, 64> packet;
        while (!queue.tryPop(packet))
            std::this_thread::yield();
        for (unsigned i = 0; i < packet.size(); ++i)
            intact &= packet[i] == sequence * 67 + i;
    }
    producer.join();
    REQUIRE(intact);
}

TEST_CASE("Queued gate edges each reach an audio sample", "[voice][voice_transfer]")
{
    Voice voice(1, testConfig());
    voice.init(48000);
    voice.updateParameters(noteState(4));
    voice.updateParameters(noteState(5, false));
    voice.updateParameters(noteState(6));
    REQUIRE_FALSE(voice.getGate());
    voice.process();
    REQUIRE(voice.getGate());
    REQUIRE(voice.getState().noteIndex == 4);
    voice.process();
    REQUIRE_FALSE(voice.getGate());
    REQUIRE(voice.getState().noteIndex == 5);
    voice.process();
    REQUIRE(voice.getGate());
    REQUIRE(voice.getState().noteIndex == 6);
}

TEST_CASE("Full voice queue retries final gate-off and config without new input", "[voice][voice_transfer]")
{
    Voice voice(1, testConfig());
    voice.init(48000);
    for (unsigned i = 0; i < Voice::CONTROL_QUEUE_CAPACITY; ++i)
        voice.updateParameters(noteState(i));
    VoiceConfig config = testConfig();
    config.outputLevel = 0.25f;
    voice.setConfig(config);
    voice.updateParameters(noteState(21, false));
    REQUIRE(voice.hasPendingControlUpdates());
    REQUIRE_FALSE(voice.flushControlUpdates());
    // Mutating the caller's copy cannot alter a queued/pending config.
    config.outputLevel = 0.9f;
    for (unsigned i = 0; i < Voice::CONTROL_QUEUE_CAPACITY; ++i)
    {
        voice.process();
        REQUIRE(voice.getState().noteIndex == i);
    }
    REQUIRE(voice.flushControlUpdates());
    REQUIRE_FALSE(voice.hasPendingControlUpdates());
    voice.process();
    REQUIRE_FALSE(voice.getGate());
    REQUIRE(voice.getState().noteIndex == 21);
    REQUIRE(voice.getConfig().outputLevel == 0.25f);
}

TEST_CASE("UI config copies preserve consecutive edits before audio consumes them", "[voice_transfer]")
{
    VoiceManager manager(1);
    const auto id = manager.addVoice(testConfig());
    auto config = *manager.getVoiceConfig(id);
    config.hasFilter = true;
    REQUIRE(manager.setVoiceConfig(id, config));
    config = *manager.getVoiceConfig(id);
    REQUIRE(config.hasFilter);
    config.outputLevel = 0.2f;
    REQUIRE(manager.setVoiceConfig(id, config));
    REQUIRE(manager.getVoiceConfig(id)->hasFilter);
    REQUIRE(manager.getVoiceConfig(id)->outputLevel == 0.2f);
    manager.disableVoice(id);
    for (unsigned i = 0; i < 4; ++i)
        manager.processAllVoices();
    manager.enableVoice(id);
    manager.updateVoiceState(id, noteState(5));
    for (unsigned i = 0; i < 4; ++i)
        REQUIRE(std::isfinite(manager.processAllVoices()));
    REQUIRE(manager.isVoiceEnabled(id));
}

TEST_CASE("Coalescing retains a pending retrigger until audio consumes it", "[voice][voice_transfer]")
{
    Voice queued(1, testConfig()), reference(1, testConfig());
    queued.init(48000);
    reference.init(48000);
    const VoiceState held = noteState(8);
    for (unsigned i = 0; i < Voice::CONTROL_QUEUE_CAPACITY; ++i)
    {
        queued.updateParameters(held);
        reference.updateParameters(held);
    }
    auto retrigger = held;
    retrigger.shouldRetrigger = true;
    queued.updateParameters(retrigger); // pending because queue is full
    queued.updateParameters(held);      // continuous controls must keep the event
    for (unsigned i = 0; i < Voice::CONTROL_QUEUE_CAPACITY; ++i)
    {
        queued.process();
        reference.process();
    }
    REQUIRE(queued.flushControlUpdates());
    reference.updateParameters(retrigger);
    for (unsigned i = 0; i < 64; ++i)
        REQUIRE(queued.process() == reference.process());
}

TEST_CASE("Scalar controls wait for audio and disabled voices still consume updates", "[voice][voice_transfer]")
{
    Voice voice(1, testConfig());
    voice.init(48000);
    voice.setEnabled(false);
    REQUIRE(voice.process() == 0.0f);
    voice.setFilterFrequency(2300);
    REQUIRE(voice.getFilterFrequency() != 2300);
    voice.process();
    REQUIRE(voice.getFilterFrequency() == 2300);
    voice.setEnabled(true);
    voice.process();
    REQUIRE(voice.getConfig().enabled);
    voice.updateParameters(noteState(0));
    voice.process();
    const float before = voice.getCachedFrequency(0);
    voice.setPitchBend(12);
    REQUIRE(voice.getCachedFrequency(0) == before);
    voice.process();
    REQUIRE(std::fabs(voice.getCachedFrequency(0) - 2 * before) < 0.01f);
}

TEST_CASE("Scale selection is copied on control core before publication", "[voice][voice_transfer]")
{
    int table[2][48]{};
    for (auto &value : table[1]) value = 12;
    uint8_t scaleIndex = 0;
    Voice voice(1, testConfig());
    voice.setScaleTable(table, 2);
    voice.setCurrentScalePointer(&scaleIndex);
    voice.init(48000);
    voice.updateParameters(noteState(0));
    scaleIndex = 1; // audio must still use the row in the published packet
    voice.process();
    const float before = voice.getCachedFrequency(0);
    REQUIRE(voice.flushControlUpdates());
    voice.process();
    REQUIRE(std::fabs(voice.getCachedFrequency(0) - 2 * before) < 0.01f);
}

TEST_CASE("Concurrent voice controls preserve complete state and config copies", "[voice_transfer][concurrency]")
{
    Voice voice(1, testConfig());
    voice.init(48000);
    voice.updateParameters(noteState(0));
    voice.process();
    std::atomic<bool> done{false};
    std::thread producer([&] {
        for (unsigned i = 0; i < 10000; ++i)
        {
            voice.updateParameters(noteState(i % 22));
            auto config = testConfig();
            config.outputLevel = (i % 8) / 8.0f;
            config.overdriveGain = config.outputLevel;
            voice.setConfig(config);
            voice.setPitchBend(static_cast<float>(i % 3));
            voice.setSlideTime((i % 8) / 8.0f);
            while (!voice.flushControlUpdates())
                std::this_thread::yield();
        }
        done.store(true, std::memory_order_release);
    });
    bool intact = true;
    unsigned trailingSamples = 0;
    while (trailingSamples <= Voice::CONTROL_QUEUE_CAPACITY)
    {
        const float sample = voice.process();
        const auto &state = voice.getState();
        intact &= std::isfinite(sample);
        intact &= state.velocityLevel == state.noteIndex / 32.0f;
        intact &= state.filterCutoff == state.noteIndex / 64.0f;
        intact &= state.attackTimeSeconds == state.noteIndex / 128.0f;
        if (done.load(std::memory_order_acquire)) ++trailingSamples;
    }
    producer.join();
    REQUIRE(intact);
    REQUIRE(voice.getState().noteIndex == 9999 % 22);
    REQUIRE(voice.getConfig().outputLevel == voice.getConfig().overdriveGain);
}
