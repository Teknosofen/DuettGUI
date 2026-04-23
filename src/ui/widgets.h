#pragma once
#include <LovyanGFX.hpp>
#include <string.h>

// Stateless drawing helpers — every function receives the render target
// so they work identically on a sprite or directly on the display.
namespace Widget {

    static constexpr int ROW_H = 80;   // standard data-row height (px)

    // Full data row (label + value + unit). Use for the initial render.
    //   col 30    label   (size 2, gray)
    //   col ~580  value   (size 4, valueColor, right-aligned)
    //   col 592   unit    (size 2, dim gray)
    void dataRow(lgfx::LovyanGFX& gfx, int y,
                 const char* label, const char* value, const char* unit,
                 uint32_t valueColor = 0xE0E0E0);

    // Partial update: erases only the value column and redraws it.
    // Compares newVal against stored[]; skips if unchanged.
    // stored[] is updated on change. Returns true if a redraw occurred.
    bool updateRowValue(lgfx::LovyanGFX& gfx, int rowY,
                        char* stored, size_t storedSize,
                        const char* newVal, uint32_t color);

    // Thin horizontal rule across the content width
    void hRule(lgfx::LovyanGFX& gfx, int y, int w);

    // Small section heading (size 2, dim gray)
    void sectionLabel(lgfx::LovyanGFX& gfx, int x, int y, const char* text);
}
