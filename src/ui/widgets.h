#pragma once
#include <LovyanGFX.hpp>
#include <string.h>

namespace Widget {

    static constexpr int ROW_H = 80;   // standard data-row height (px)
    static constexpr int COL_W = 400;  // each column is half the 800px content width

    // Full data row. col=0: left half (x 0–399), col=1: right half (x 400–799).
    void dataRow(lgfx::LovyanGFX& gfx, int y,
                 const char* label, const char* value, const char* unit,
                 uint32_t valueColor = 0xE0E0E0, int col = 0);

    // Partial update: erases only the value area and redraws it when newVal changes.
    bool updateRowValue(lgfx::LovyanGFX& gfx, int rowY,
                        char* stored, size_t storedSize,
                        const char* newVal, uint32_t color, int col = 0);

    void hRule(lgfx::LovyanGFX& gfx, int y, int w);
    void vRule(lgfx::LovyanGFX& gfx, int x, int h);
    void sectionLabel(lgfx::LovyanGFX& gfx, int x, int y, const char* text);
}
