#include "screen_vehicle.h"
#include "widgets.h"
#include "../data/vehicle_data.h"
#include <stdio.h>
#include <math.h>

// Format a float into a local buffer — call once per statement only.
static char _vbuf[12];
static const char* fmt(float v, int dec)
{
    if (dec == 0) snprintf(_vbuf, sizeof(_vbuf), "%d",   (int)roundf(v));
    else          snprintf(_vbuf, sizeof(_vbuf), "%.*f", dec, v);
    return _vbuf;
}

// Row descriptors: label, decimals, unit, colour, column
struct RowDef { const char* label; int dec; const char* unit; uint32_t col; int column; };

// Left column (col=0): RPM, Throttle, MAP, Ambient
// Right column (col=1): Fuel flow, L/100km, Fuel used
static constexpr RowDef ROWS[7] = {
    { "RPM",       0, "rpm", 0x00FF88, 0 },
    { "Throttle",  1, "%",   0x00BFFF, 0 },
    { "MAP",       1, "kPa", 0xFF8C00, 0 },
    { "Ambient",   1, "kPa", 0xFF8C00, 0 },
    { "Flow",      1, "L/h", 0xFF5555, 1 },
    { "L/100km",   1, "",    0xFF5555, 1 },
    { "Fuel used", 2, "L",   0xFF5555, 1 },
};

// ─────────────────────────────────────────────────────────────────────────────

void ScreenVehicle::init(uint16_t contentW, uint16_t contentH)
{
    _startY = ((int)contentH - 4 * Widget::ROW_H) / 2;
}

void ScreenVehicle::update(lgfx::LovyanGFX& gfx,
                            uint16_t contentW, uint16_t contentH)
{
    constexpr size_t SZ = sizeof(_fmtRpm);  // all buffers same size
    char* stored[7] = {
        _fmtRpm, _fmtThrottle, _fmtMap, _fmtAmbient,
        _fmtFuelFlow, _fmtFuel100, _fmtFuelUsed
    };

    float values[7] = {
        vdata.rpm,
        vdata.throttle_pct,
        vdata.map_kpa,
        vdata.ambient_kpa,
        vdata.fuel_flow_lph,
        vdata.fuel_per_100km,
        vdata.fuel_used_l
    };

    if (_needsRedraw) {
        // Full render: background + vertical divider + all rows
        gfx.fillRect(0, 0, contentW, contentH, TFT_BLACK);

        Widget::vRule(gfx, Widget::COL_W, contentH);

        // Row indices per column
        // Col 0: rows 0–3, Col 1: rows 4–6
        int rowInCol[2] = { 0, 0 };
        for (int i = 0; i < 7; i++) {
            int c = ROWS[i].column;
            int y = _startY + rowInCol[c] * Widget::ROW_H;
            const char* val = fmt(values[i], ROWS[i].dec);
            Widget::dataRow(gfx, y, ROWS[i].label, val, ROWS[i].unit,
                            ROWS[i].col, c);
            strncpy(stored[i], val, SZ - 1);
            stored[i][SZ - 1] = '\0';
            rowInCol[c]++;
        }

        _needsRedraw = false;
        return;
    }

    // Partial update: only redraw values that changed
    int rowInCol[2] = { 0, 0 };
    for (int i = 0; i < 7; i++) {
        int c = ROWS[i].column;
        int y = _startY + rowInCol[c] * Widget::ROW_H;
        Widget::updateRowValue(gfx, y, stored[i], SZ,
                               fmt(values[i], ROWS[i].dec), ROWS[i].col, c);
        rowInCol[c]++;
    }
}
