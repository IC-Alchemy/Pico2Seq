#include <catch2/catch_test_macros.hpp>
#include "utils/StackWatermark.h"

#include <array>
#include <cstddef>
#include <cstdint>

// The paint/scan logic behind [DIAG MEM]'s stack headroom. The firmware binding
// (StackWatermark.cpp, linker symbols) is board-only; these pin the arithmetic it
// relies on, using an array as a stand-in stack (index 0 = lowest address).

using namespace StackWatermark;

namespace
{
constexpr uint32_t kUsedWord = 0xC0FFEE01u; // any value that is not the pattern
constexpr size_t kBytes = sizeof(uint32_t);
} // namespace

TEST_CASE("paint fills exactly the words below its limit", "[stack]")
{
    std::array<uint32_t, 16> stack;
    stack.fill(kUsedWord);
    paint(stack.data(), stack.data() + 10);
    for (size_t i = 0; i < 10; ++i)
        CHECK(stack[i] == kPattern);
    for (size_t i = 10; i < stack.size(); ++i)
        CHECK(stack[i] == kUsedWord); // the live frames above the limit are untouched
}

TEST_CASE("paint with an empty or inverted range writes nothing", "[stack]")
{
    std::array<uint32_t, 8> stack;
    stack.fill(kUsedWord);
    paint(stack.data() + 4, stack.data() + 4);
    paint(stack.data() + 6, stack.data() + 2);
    for (uint32_t word : stack)
        CHECK(word == kUsedWord);
}

TEST_CASE("untouchedBytes counts the pattern up from the low end", "[stack]")
{
    std::array<uint32_t, 32> stack;
    stack.fill(kUsedWord);
    paint(stack.data(), stack.data() + 24); // the top 8 words model the live frames

    CHECK(untouchedBytes(stack.data(), stack.data() + stack.size()) == 24 * kBytes);

    // The stack grows down into the painted area: 5 words deeper.
    for (size_t i = 19; i < 24; ++i)
        stack[i] = kUsedWord;
    CHECK(untouchedBytes(stack.data(), stack.data() + stack.size()) == 19 * kBytes);

    // Reaching the lowest word leaves no headroom.
    stack[0] = kUsedWord;
    CHECK(untouchedBytes(stack.data(), stack.data() + stack.size()) == 0);
}

TEST_CASE("the deepest write sets the reading, however old", "[stack]")
{
    std::array<uint32_t, 16> stack;
    stack.fill(kUsedWord);
    paint(stack.data(), stack.data() + 12);
    stack[3] = kUsedWord; // one deep excursion...
    stack[11] = kUsedWord; // ...and a shallow one afterwards
    stack[7] = kPattern;   // a frame that later returned cannot restore the mark
    CHECK(untouchedBytes(stack.data(), stack.data() + stack.size()) == 3 * kBytes);
}

TEST_CASE("an unpainted stack reads as fully used, a fully painted one as untouched", "[stack]")
{
    std::array<uint32_t, 8> stack{};
    CHECK(untouchedBytes(stack.data(), stack.data() + stack.size()) == 0);
    stack.fill(kPattern);
    CHECK(untouchedBytes(stack.data(), stack.data() + stack.size()) == stack.size() * kBytes);
}

TEST_CASE("an empty region has no headroom", "[stack]")
{
    std::array<uint32_t, 4> stack;
    stack.fill(kPattern);
    CHECK(untouchedBytes(stack.data(), stack.data()) == 0);
}

TEST_CASE("only a run of pattern-valued words can hide depth, by its own length", "[stack]")
{
    // Documented limit of the method: words 8..11 are in use (true headroom: 8
    // words), but the program wrote kPattern itself into the deepest two, so the
    // scan walks through them and reads optimistic by exactly that run.
    std::array<uint32_t, 16> stack;
    stack.fill(kUsedWord);
    paint(stack.data(), stack.data() + 12);
    stack[10] = kUsedWord;
    stack[11] = kUsedWord; // words 8 and 9 still hold the pattern value
    CHECK(untouchedBytes(stack.data(), stack.data() + stack.size()) == 10 * kBytes);
}
