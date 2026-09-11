#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstdint>
void audio_i2s_debug_stage(uint32_t) {}
#include "src/audio/audio_i2s.c"
#include "src/audio/audio.cpp"

TEST_CASE("I2S passes rendered buffers to DMA and recovers from starvation", "[audio][i2s]")
{
    audio_format_t format{48000, AUDIO_BUFFER_FORMAT_PCM_S16, 2};
    audio_buffer_format_t bufferFormat{&format, 4};
    auto* producer = audio_new_producer_pool(&bufferFormat, 4, 256);
    const audio_i2s_config_t config{9, 10, PICO_AUDIO_I2S_DMA_CHANNEL_AUTO, 0};
    REQUIRE(audio_i2s_setup(&format, &config));
    REQUIRE(audioDmaConfig.highPriority);
    REQUIRE(audio_i2s_connect_extra(producer, false, 0, 256, nullptr));
    REQUIRE(audio_i2s_consumer->free_list == nullptr);

    std::array<audio_buffer_t*, 4> buffers{};
    for (uint i = 0; i < buffers.size(); ++i) {
        buffers[i] = take_audio_buffer(producer, false);
        REQUIRE(buffers[i]);
        buffers[i]->sample_count = 256;
        reinterpret_cast<uint32_t*>(buffers[i]->buffer->bytes)[0] = 0x12340000u + i;
        give_audio_buffer(producer, buffers[i]);
    }
    REQUIRE(take_audio_buffer(producer, false) == nullptr);
    audio_i2s_set_enabled(true);
    REQUIRE(audio_i2s_underrun_count() == 0);
    REQUIRE(audio_i2s_tx_stall_count() == 0);
    REQUIRE(audioDmaSource == buffers[0]->buffer->bytes);
    REQUIRE(audioDmaCount == 256);
    REQUIRE(take_audio_buffer(producer, false) == nullptr); // DMA still owns it

    const auto finishTransfer = [] {
        audioDmaPending = true;
        audioDmaHandler();
        REQUIRE_FALSE(audioDmaPending);
        REQUIRE_FALSE(audioIrqsDisabled);
    };
    // Recycle only completed buffers. The same four pointers must remain in
    // FIFO order for hundreds of IRQ/render handoffs, with no copying.
    for (uint frame = 1; frame <= 400; ++frame) {
        finishTransfer();
        REQUIRE(audioDmaSource == buffers[frame % 4]->buffer->bytes);
        REQUIRE(audioDmaConfig.increment);
        auto* released = take_audio_buffer(producer, false);
        REQUIRE(released == buffers[(frame - 1) % 4]);
        REQUIRE(take_audio_buffer(producer, false) == nullptr);
        give_audio_buffer(producer, released);
    }
    REQUIRE(audio_i2s_underrun_count() == 0);

    // Stop producing. Finish the playing buffer and drain the three queued
    // buffers; only the fourth completion should substitute silence.
    for (int i = 0; i < 3; ++i) finishTransfer();
    REQUIRE(audio_i2s_underrun_count() == 0);
    finishTransfer();
    REQUIRE(audio_i2s_underrun_count() == 1);
    REQUIRE_FALSE(audioDmaConfig.increment);
    REQUIRE(*static_cast<const uint32_t*>(audioDmaSource) == 0);
    REQUIRE(audioDmaCount == PICO_AUDIO_I2S_SILENCE_BUFFER_SAMPLE_LENGTH);

    auto* recovered = take_audio_buffer(producer, false);
    REQUIRE(recovered);
    give_audio_buffer(producer, recovered);
    // Distinguish a physical PIO stall from an empty software queue, and
    // clear only our SM's write-one-to-clear flag.
    audioPio.fdebug.bits = (1u << PIO_FDEBUG_TXSTALL_LSB) | (2u << PIO_FDEBUG_TXSTALL_LSB);
    finishTransfer();
    REQUIRE(audioDmaSource == recovered->buffer->bytes);
    REQUIRE(audioDmaConfig.increment);
    REQUIRE(audio_i2s_underrun_count() == 1);
    REQUIRE(audio_i2s_tx_stall_count() == 1);
    REQUIRE(audioPio.fdebug.bits == (2u << PIO_FDEBUG_TXSTALL_LSB));
    audio_i2s_set_enabled(false);

    // Pools are permanent on firmware; release their allocations on the host.
    for (auto* buffer : buffers) {
        std::free(buffer->buffer->bytes);
        std::free(buffer->buffer);
    }
    std::free(buffers[0]); // contiguous audio_buffer_t array
    std::free(producer);
    std::free(audio_i2s_consumer);
}
