#pragma once

#include <cstdint>

// ──────────────────────────────────────────────────────────────
// A bitmap font, in the column-major form a page-addressed OLED wants.
//
// A glyph is `width` bytes, one per column, and each byte holds `height` bits
// running DOWNWARD from the low bit. That is the same orientation an SSD1306
// page uses, which is the whole point: blitting a glyph is a copy, never a
// transpose.
//
// The struct owns nothing — `table` points at storage that outlives it, which
// in practice means an `inline constexpr` array in the font's own header.
// ──────────────────────────────────────────────────────────────

struct FontDef
{
    const uint8_t *table;   // first byte of the first glyph
    uint8_t width;          // bytes (columns) per glyph
    uint8_t height;         // bits used per column
    uint8_t firstChar;      // first character the table covers, usually 32
    uint8_t lastChar;       // last character the table covers, inclusive

    /// The glyph for `c`, or nullptr when the font does not cover it.
    /// Callers must handle the nullptr — a font is allowed to be partial.
    constexpr const uint8_t *GetGlyph(char c) const
    {
        const uint8_t u = static_cast<uint8_t>(c);
        if (u < firstChar || u > lastChar) return nullptr;
        return table + (u - firstChar) * width;
    }
};
