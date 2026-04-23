#include "widgets.h"

void Widget::dataRow(lgfx::LovyanGFX& gfx, int y,
                     const char* label, const char* value, const char* unit,
                     uint32_t valueColor)
{
    int midY = y + ROW_H / 2;

    // Label — left, size 2, mid-gray
    gfx.setTextSize(2);
    gfx.setTextColor(gfx.color888(0x90, 0x90, 0x90), TFT_BLACK);
    gfx.setCursor(30, midY - 8);
    gfx.print(label);

    // Value — size 4, right-aligned to x=580
    gfx.setTextSize(4);
    gfx.setTextColor(
        gfx.color888((valueColor >> 16) & 0xFF,
                     (valueColor >>  8) & 0xFF,
                      valueColor        & 0xFF),
        TFT_BLACK);
    int vw = (int)gfx.textWidth(value);
    gfx.setCursor(580 - vw, midY - 16);
    gfx.print(value);

    // Unit — size 2, dim gray
    gfx.setTextSize(2);
    gfx.setTextColor(gfx.color888(0x60, 0x60, 0x60), TFT_BLACK);
    gfx.setCursor(592, midY - 8);
    gfx.print(unit);
}

bool Widget::updateRowValue(lgfx::LovyanGFX& gfx, int rowY,
                             char* stored, size_t storedSize,
                             const char* newVal, uint32_t color)
{
    if (strcmp(stored, newVal) == 0) return false;

    int midY = rowY + ROW_H / 2;

    // Erase only the value column (x 452–591, size-4 text height ±2 px)
    gfx.fillRect(452, midY - 18, 140, 36, TFT_BLACK);

    // Redraw value right-aligned to x=580
    gfx.setTextSize(4);
    gfx.setTextColor(
        gfx.color888((color >> 16) & 0xFF,
                     (color >>  8) & 0xFF,
                      color        & 0xFF),
        TFT_BLACK);
    int vw = (int)gfx.textWidth(newVal);
    gfx.setCursor(580 - vw, midY - 16);
    gfx.print(newVal);

    strncpy(stored, newVal, storedSize - 1);
    stored[storedSize - 1] = '\0';
    return true;
}

void Widget::hRule(lgfx::LovyanGFX& gfx, int y, int w)
{
    gfx.drawFastHLine(20, y, w - 40, gfx.color888(0x30, 0x30, 0x30));
}

void Widget::sectionLabel(lgfx::LovyanGFX& gfx, int x, int y, const char* text)
{
    gfx.setTextSize(2);
    gfx.setTextColor(gfx.color888(0x70, 0x70, 0x70), TFT_BLACK);
    gfx.setCursor(x, y);
    gfx.print(text);
}
