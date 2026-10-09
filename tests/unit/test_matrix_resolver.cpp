#include <catch2/catch_test_macros.hpp>

#include "matrix/MatrixResolver.h"

// Row/column ghost rejection for the MPR121 pad grid: a pad already held is
// assumed to stay held, so the new electrodes identify the newly pressed pad.

namespace
{
using MatrixResolver::padBit;
using MatrixResolver::resolve;

constexpr uint8_t row(uint8_t r) { return static_cast<uint8_t>(1u << r); }
constexpr uint8_t col(uint8_t c) { return static_cast<uint8_t>(1u << c); }
} // namespace

TEST_CASE("Single touches resolve to their crossing", "[matrix_resolver]")
{
    CHECK(resolve(0, 0, 0) == 0u);
    CHECK(resolve(row(1), col(5), 0) == padBit(1, 5));
    // A row or column alone is not a pad.
    CHECK(resolve(row(2), 0, 0) == 0u);
    CHECK(resolve(0, col(3), 0) == 0u);
}

TEST_CASE("Two pads sharing a row or column are unambiguous", "[matrix_resolver]")
{
    CHECK(resolve(row(0), col(1) | col(6), 0) == (padBit(0, 1) | padBit(0, 6)));
    CHECK(resolve(row(0) | row(3), col(2), 0) == (padBit(0, 2) | padBit(3, 2)));
}

TEST_CASE("Held pad plus a diagonal press yields two pads, not four", "[matrix_resolver]")
{
    const uint32_t held = padBit(0, 1);
    const uint32_t pressed = resolve(row(0) | row(2), col(1) | col(5), held);
    CHECK(pressed == (padBit(0, 1) | padBit(2, 5)));

    // Same electrodes, other pad held first: the other diagonal is chosen.
    const uint32_t heldOther = padBit(0, 5);
    CHECK(resolve(row(0) | row(2), col(1) | col(5), heldOther) == (padBit(0, 5) | padBit(2, 1)));

    // Stable while both stay down.
    CHECK(resolve(row(0) | row(2), col(1) | col(5), pressed) == pressed);
}

TEST_CASE("Releasing either diagonal pad keeps the other", "[matrix_resolver]")
{
    const uint32_t both = padBit(0, 1) | padBit(2, 5);
    CHECK(resolve(row(2), col(5), both) == padBit(2, 5));
    CHECK(resolve(row(0), col(1), both) == padBit(0, 1));
    CHECK(resolve(0, 0, both) == 0u);
}

TEST_CASE("Diagonal pads touched in one scan with nothing held register nothing", "[matrix_resolver]")
{
    CHECK(resolve(row(0) | row(2), col(1) | col(5), 0) == 0u);
}

TEST_CASE("New row on the single held column resolves", "[matrix_resolver]")
{
    // Held (0,1) and (0,4) share row 0; touching row 3 on column 1 is ambiguous
    // (could be (3,1) or (3,4)), so it is refused.
    const uint32_t heldRow = padBit(0, 1) | padBit(0, 4);
    CHECK(resolve(row(0) | row(3), col(1) | col(4), heldRow) == heldRow);

    // Held (0,1) and (2,1) share column 1; a new column 6 on both rows is ambiguous too.
    const uint32_t heldCol = padBit(0, 1) | padBit(2, 1);
    CHECK(resolve(row(0) | row(2), col(1) | col(6), heldCol) == heldCol);
}

TEST_CASE("Third press on fresh electrodes extends the held set", "[matrix_resolver]")
{
    uint32_t held = padBit(0, 0);
    held = resolve(row(0) | row(1), col(0) | col(3), held);
    REQUIRE(held == (padBit(0, 0) | padBit(1, 3)));
    held = resolve(row(0) | row(1) | row(3), col(0) | col(3) | col(7), held);
    CHECK(held == (padBit(0, 0) | padBit(1, 3) | padBit(3, 7)));
}

TEST_CASE("Ghost-free pad resolves once the ambiguous partner lifts", "[matrix_resolver]")
{
    // Simultaneous diagonal touch: nothing registered...
    uint32_t held = resolve(row(1) | row(2), col(2) | col(6), 0);
    REQUIRE(held == 0u);
    // ...then one finger lifts and the other registers alone.
    held = resolve(row(2), col(6), held);
    CHECK(held == padBit(2, 6));
}
