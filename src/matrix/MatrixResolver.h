#ifndef MATRIX_RESOLVER_H
#define MATRIX_RESOLVER_H

#include <cstdint>

/**
 * @brief Ghost-free pad resolution for the 4x8 row/column touch grid.
 *
 * The MPR121 only reports which row and column electrodes are touched, so two
 * fingers on different rows and columns light 2 rows x 2 columns and the
 * naive row-AND-column test reports four pads. The resolver breaks that tie
 * with history: a pad already held is assumed to stay held while its row and
 * column are still touched, and the newly touched electrodes are explained by
 * the newly pressed pad(s). Holding pad (r1,c1) then touching (r2,c2) gives
 * exactly those two pads, never the ghosts (r1,c2) and (r2,c1).
 *
 * When history cannot decide (e.g. two diagonal pads touched in the same scan
 * with nothing held, or a new row whose column could be either of two held
 * columns), the new presses are not registered: no ghost press is ever sent.
 *
 * Pure and portable (no Arduino); tested by tests/unit/test_matrix_resolver.cpp.
 */
namespace MatrixResolver
{
constexpr uint8_t kRows = 4;
constexpr uint8_t kCols = 8;

// Pad index is row-major, matching Matrix.cpp: pad = row * 8 + col.
constexpr uint32_t padBit(uint8_t row, uint8_t col)
{
    return 1u << (row * kCols + col);
}

inline uint8_t bitCount(uint32_t v)
{
    uint8_t n = 0;
    for (; v; v &= v - 1u)
        ++n;
    return n;
}

// Every pad at a touched row x touched column crossing.
inline uint32_t crossMask(uint8_t rowMask, uint8_t colMask)
{
    uint32_t pads = 0;
    for (uint8_t r = 0; r < kRows; ++r)
    {
        if (!(rowMask & (1u << r)))
            continue;
        pads |= static_cast<uint32_t>(colMask) << (r * kCols);
    }
    return pads;
}

// Row / column electrodes used by a set of pads.
inline uint8_t rowsOf(uint32_t pads)
{
    uint8_t rows = 0;
    for (uint8_t r = 0; r < kRows; ++r)
        if ((pads >> (r * kCols)) & 0xFFu)
            rows |= static_cast<uint8_t>(1u << r);
    return rows;
}

inline uint8_t colsOf(uint32_t pads)
{
    uint8_t cols = 0;
    for (uint8_t r = 0; r < kRows; ++r)
        cols |= static_cast<uint8_t>((pads >> (r * kCols)) & 0xFFu);
    return cols;
}

/**
 * @param rowMask touched physical rows (bit r = row r, 0..3)
 * @param colMask touched physical columns (bit c = column c, 0..7)
 * @param held    pads resolved as pressed on the previous scan
 * @return pads pressed now (bit = pad index)
 */
inline uint32_t resolve(uint8_t rowMask, uint8_t colMask, uint32_t held)
{
    rowMask &= 0x0Fu;
    const uint32_t candidates = crossMask(rowMask, colMask);

    // One row or one column touched: every crossing is a real finger.
    if (bitCount(rowMask) <= 1 || bitCount(colMask) <= 1)
        return candidates;

    // Pads already held stay held while their row and column are still touched.
    uint32_t pressed = held & candidates;
    const uint8_t keptRows = rowsOf(pressed);
    const uint8_t keptCols = colsOf(pressed);

    // Electrodes the held pads do not explain belong to new presses.
    const uint8_t newRows = static_cast<uint8_t>(rowMask & ~keptRows);
    const uint8_t newCols = static_cast<uint8_t>(colMask & ~keptCols);

    if (newRows && newCols)
    {
        // New finger(s) on a fresh row and fresh column. Unambiguous while one
        // side is a single electrode; otherwise refuse rather than guess.
        if (bitCount(newRows) == 1 || bitCount(newCols) == 1)
            pressed |= crossMask(newRows, newCols);
    }
    else if (newRows)
    {
        // New row on an already-touched column: only decidable with one held column.
        if (bitCount(keptCols) == 1)
            pressed |= crossMask(newRows, keptCols);
    }
    else if (newCols)
    {
        if (bitCount(keptRows) == 1)
            pressed |= crossMask(keptRows, newCols);
    }
    return pressed;
}
} // namespace MatrixResolver

#endif // MATRIX_RESOLVER_H
