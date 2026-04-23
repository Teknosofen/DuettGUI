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

// Row descriptors: label, decimals, unit, colour
struct RowDef { const char* label; int dec; const char* unit; uint32_t col; };
static constexpr RowDef ROWS[5] = {
    { "RPM",              0, "rpm", 0x00FF88 },
    { "Throttle",         1, "%",   0x00BFFF },
    { "Inlet pressure",   1, "kPa", 0xFF8C00 },
    { "Ambient pressure", 1, "kPa", 0xFF8C00 },
    { "Fuel flow",        1, "L/h", 0xFF5555 },
};

// ─────────────────────────────────────────────────────────────────────────────

void ScreenVehicle::init(uint16_t contentW, uint16_t contentH)
{
    _startY = ((int)contentH - 5 * Widget::ROW_H) / 2;
}

void ScreenVehicle::update(lgfx::LovyanGFX& gfx,
                            uint16_t contentW, uint16_t contentH)
{
    char* stored[5] = { _fmtRpm, _fmtThrottle, _fmtMap, _fmtAmbient, _fmtFuel };
    constexpr size_t SZ = sizeof(_fmtRpm);

    float values[5] = {
        vdata.rpm, vdata.throttle_pct,
        vdata.map_kpa, vdata.ambient_kpa, vdata.fuel_flow_lph
    };

    if (_needsRedraw) {
        // ── Full render: background + labels + units + current values ────────
        gfx.fillRect(0, 0, contentW, contentH, TFT_BLACK);

        int y = _startY;
        for (int i = 0; i < 5; i++) {
            const char* val = fmt(values[i], ROWS[i].dec);
            Widget::dataRow(gfx, y, ROWS[i].label, val, ROWS[i].unit, ROWS[i].col);
            strncpy(stored[i], val, SZ - 1);  stored[i][SZ - 1] = '\0';
            y += Widget::ROW_H;
            if (i < 4) Widget::hRule(gfx, y, contentW);
        }
        _needsRedraw = false;
        return;
    }

    // ── Partial update: only redraw values that changed ──────────────────────
    int y = _startY;
    for (int i = 0; i < 5; i++) {
        Widget::updateRowValue(gfx, y, stored[i], SZ,
                               fmt(values[i], ROWS[i].dec), ROWS[i].col);
        y += Widget::ROW_H;
    }
}
