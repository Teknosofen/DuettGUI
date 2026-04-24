#include "widgets.h"

void Widget::dataRow(lgfx::LovyanGFX& gfx, int y,
                     const char* label, const char* value, const char* unit,
                     uint32_t valueColor, int col)
{
    int dx   = col * COL_W;
    int midY = y + ROW_H / 2;

    // Label
    gfx.setFont(&lgfx::fonts::DejaVu18);
    gfx.setTextSize(1);
    gfx.setTextColor(gfx.color888(0x90, 0x90, 0x90), TFT_BLACK);
    int lblH = (int)gfx.fontHeight();
    gfx.setCursor(dx + 20, midY - lblH / 2);
    gfx.print(label);

    // Value — right-aligned to dx+360
    gfx.setFont(&lgfx::fonts::DejaVu40);
    gfx.setTextSize(1);
    gfx.setTextColor(
        gfx.color888((valueColor >> 16) & 0xFF,
                     (valueColor >>  8) & 0xFF,
                      valueColor        & 0xFF),
        TFT_BLACK);
    int valH = (int)gfx.fontHeight();
    int vw   = (int)gfx.textWidth(value);
    gfx.setCursor(dx + 360 - vw, midY - valH / 2);
    gfx.print(value);

    // Unit
    gfx.setFont(&lgfx::fonts::DejaVu18);
    gfx.setTextSize(1);
    gfx.setTextColor(gfx.color888(0x60, 0x60, 0x60), TFT_BLACK);
    int unitH = (int)gfx.fontHeight();
    gfx.setCursor(dx + 368, midY - unitH / 2);
    gfx.print(unit);
}

bool Widget::updateRowValue(lgfx::LovyanGFX& gfx, int rowY,
                             char* stored, size_t storedSize,
                             const char* newVal, uint32_t color, int col)
{
    if (strcmp(stored, newVal) == 0) return false;

    int dx   = col * COL_W;
    int midY = rowY + ROW_H / 2;

    gfx.setFont(&lgfx::fonts::DejaVu40);
    gfx.setTextSize(1);
    int fh = (int)gfx.fontHeight();

    // Erase value area: x=dx+190, width=175
    gfx.fillRect(dx + 190, midY - fh / 2 - 2, 175, fh + 4, TFT_BLACK);

    gfx.setTextColor(
        gfx.color888((color >> 16) & 0xFF,
                     (color >>  8) & 0xFF,
                      color        & 0xFF),
        TFT_BLACK);
    int vw = (int)gfx.textWidth(newVal);
    gfx.setCursor(dx + 360 - vw, midY - fh / 2);
    gfx.print(newVal);

    strncpy(stored, newVal, storedSize - 1);
    stored[storedSize - 1] = '\0';
    return true;
}

void Widget::hRule(lgfx::LovyanGFX& gfx, int y, int w)
{
    gfx.drawFastHLine(20, y, w - 40, gfx.color888(0x30, 0x30, 0x30));
}

void Widget::vRule(lgfx::LovyanGFX& gfx, int x, int h)
{
    gfx.drawFastVLine(x, 10, h - 20, gfx.color888(0x30, 0x30, 0x30));
}

void Widget::sectionLabel(lgfx::LovyanGFX& gfx, int x, int y, const char* text)
{
    gfx.setFont(&lgfx::fonts::DejaVu18);
    gfx.setTextSize(1);
    gfx.setTextColor(gfx.color888(0x70, 0x70, 0x70), TFT_BLACK);
    gfx.setCursor(x, y);
    gfx.print(text);
}
