#pragma once

#include <cstdint>
#include "FontDef.h"
#include "Font5x7.h"

// ──────────────────────────────────────────────────────────────
// How a run of text is drawn: which font, at what integer scale, in which of
// the two colours a 1-bit panel has.
//
// `size` is a whole-number pixel multiplier, not a point size. There is no
// hinting and no sub-pixel anything on a 1-bit panel — a glyph pixel becomes a
// size x size block, so 2 is exactly twice as tall and nothing in between is
// available.
// ──────────────────────────────────────────────────────────────

struct TextStyle
{
    const FontDef *font;
    uint8_t size;    // integer pixel multiplier, 1 = the font's own size
    bool color;      // true = lit

    constexpr TextStyle(const FontDef *f, uint8_t s = 1, bool c = true)
        : font(f), size(s), color(c) {}

    /// The 5x7 font at `size`, lit. The default for anything that has not got
    /// an opinion.
    static constexpr TextStyle Default(uint8_t size = 1)
    {
        return TextStyle(&Font5x7, size, true);
    }

    /// Dark text, for drawing into an area that has been filled lit.
    static constexpr TextStyle Inverted(uint8_t size = 1)
    {
        return TextStyle(&Font5x7, size, false);
    }

    /// How wide `str` would be, in pixels, including the one-column gap this
    /// style puts after every glyph but excluding the trailing one.
    constexpr int MeasureWidth(const char *str) const
    {
        if (!font || !str) return 0;
        int n = 0;
        for (const char *p = str; *p; ++p) ++n;
        if (n == 0) return 0;
        return n * (font->width + 1) * size - size;
    }

    constexpr int LineHeight() const { return font ? font->height * size : 0; }
};
