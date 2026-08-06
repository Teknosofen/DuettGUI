#include "screen_ign_scope.h"
#include "widgets.h"
#include "../data/vehicle_data.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// ── Layout ────────────────────────────────────────────────────────────────────
//
//  y=0–32   Header: "RPM" + value (green)         "ADV" + value (cyan)
//  y=32     hRule
//  y=36–?   Plot area — oscilloscope sweep, 60 s window
//  bottom   Axis strip: 0s/10s/.../50s tick labels (static, outside swept area)
//

static constexpr uint32_t COL_RPM  = 0x00FF88;
static constexpr uint32_t COL_ADV  = 0x00BFFF;
static constexpr uint32_t COL_GRID = 0x303030;
static constexpr uint32_t COL_GRAY = 0x606060;

static void setColor(lgfx::LovyanGFX& gfx, uint32_t c) {
    gfx.setTextColor(gfx.color888((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF), TFT_BLACK);
}

void ScreenIgnScope::init(uint16_t /*w*/, uint16_t /*h*/) {}

int ScreenIgnScope::valueToY(float val, float lo, float hi) const {
    if (val < lo) val = lo;
    if (val > hi) val = hi;
    float frac = (val - lo) / (hi - lo);
    return _plotY0 + _plotH - (int)(frac * _plotH);
}

void ScreenIgnScope::drawStatic(lgfx::LovyanGFX& gfx, uint16_t w, uint16_t h) {
    gfx.fillRect(0, 0, w, h, TFT_BLACK);

    // Header legend (static swatches; live values drawn by drawHeaderValues)
    gfx.setFont(&lgfx::fonts::DejaVu18);
    gfx.setTextSize(1);
    setColor(gfx, COL_RPM);
    gfx.setCursor(20, 6);
    gfx.print("RPM");
    setColor(gfx, COL_ADV);
    {
        int tw = (int)gfx.textWidth("ADV 00.0\xB0");
        gfx.setCursor(w - 20 - tw, 6);
        gfx.print("ADV");
    }

    Widget::hRule(gfx, 32, w);

    // Plot geometry: leave room for header above and a tick-label strip below
    _plotX0 = 0;
    _plotY0 = 40;
    _plotW  = w;
    _plotH  = (h > 64) ? (h - _plotY0 - 24) : 100;

    // Frame
    gfx.drawRect(_plotX0, _plotY0, _plotW, _plotH, gfx.color888(0x40, 0x40, 0x40));

    // Vertical grid lines every 10 s (5 interior lines across the 60 s sweep)
    for (int i = 1; i <= 5; i++) {
        _gridX[i - 1] = _plotX0 + (int)((_plotW - 1) * (i / 6.0f));
        gfx.drawFastVLine(_gridX[i - 1], _plotY0 + 1, _plotH - 2,
                          gfx.color888(0x30, 0x30, 0x30));
    }
    // Horizontal mid-line (generic scope-grid reference, not tied to a value)
    gfx.drawFastHLine(_plotX0 + 1, _plotY0 + _plotH / 2, _plotW - 2,
                      gfx.color888(0x30, 0x30, 0x30));

    // Static tick labels below the plot — outside the swept region, never erased
    gfx.setFont(&lgfx::fonts::DejaVu12);
    gfx.setTextSize(1);
    setColor(gfx, COL_GRAY);
    int labelY = _plotY0 + _plotH + 4;
    for (int i = 0; i < 5; i++) {
        char lbl[6];
        snprintf(lbl, sizeof(lbl), "%ds", (i + 1) * 10);
        int tw = (int)gfx.textWidth(lbl);
        gfx.setCursor(_gridX[i] - tw / 2, labelY);
        gfx.print(lbl);
    }
    gfx.setCursor(_plotX0 + 2, labelY);
    gfx.print("0s");
    {
        const char* lbl = "60s";
        int tw = (int)gfx.textWidth(lbl);
        gfx.setCursor(_plotX0 + _plotW - tw - 2, labelY);
        gfx.print(lbl);
    }

    // Range hints in the plot corners
    gfx.setFont(&lgfx::fonts::DejaVu12);
    gfx.setTextSize(1);
    setColor(gfx, COL_RPM);
    gfx.setCursor(_plotX0 + 4, _plotY0 + 2);
    gfx.print("6000");
    gfx.setCursor(_plotX0 + 4, _plotY0 + _plotH - 14);
    gfx.print("0");
    setColor(gfx, COL_ADV);
    {
        const char* hi = "40\xB0";
        int tw = (int)gfx.textWidth(hi);
        gfx.setCursor(_plotX0 + _plotW - tw - 4, _plotY0 + 2);
        gfx.print(hi);
        const char* lo = "-10\xB0";
        tw = (int)gfx.textWidth(lo);
        gfx.setCursor(_plotX0 + _plotW - tw - 4, _plotY0 + _plotH - 14);
        gfx.print(lo);
    }

    // Fresh sweep: no previous point, restart the beam at t=0
    _lastX        = -1;
    _sweepStartMs = millis();
    _fmtRpm[0] = _fmtAdv[0] = '\0';
}

void ScreenIgnScope::drawHeaderValues(lgfx::LovyanGFX& gfx) {
    char buf[10];

    snprintf(buf, sizeof(buf), "%.0f", vdata.rpm);
    if (strcmp(_fmtRpm, buf) != 0) {
        strncpy(_fmtRpm, buf, sizeof(_fmtRpm) - 1); _fmtRpm[sizeof(_fmtRpm) - 1] = '\0';
        gfx.setFont(&lgfx::fonts::DejaVu18);
        gfx.setTextSize(1);
        gfx.fillRect(70, 4, 90, 24, TFT_BLACK);
        setColor(gfx, COL_RPM);
        gfx.setCursor(70, 6);
        gfx.print(buf);
    }

    snprintf(buf, sizeof(buf), "%.1f\xB0", vdata.ign_advance_deg);
    if (strcmp(_fmtAdv, buf) != 0) {
        strncpy(_fmtAdv, buf, sizeof(_fmtAdv) - 1); _fmtAdv[sizeof(_fmtAdv) - 1] = '\0';
        gfx.setFont(&lgfx::fonts::DejaVu18);
        gfx.setTextSize(1);
        int labelW = (int)gfx.textWidth("ADV 00.0\xB0") + 20;
        int x0 = (int)_plotW - labelW;
        gfx.fillRect(x0 + 42, 4, labelW - 42, 24, TFT_BLACK);
        setColor(gfx, COL_ADV);
        gfx.setCursor(x0 + 42, 6);
        gfx.print(buf);
    }
}

// Redraw any grid (vertical marks + horizontal mid-line) that falls inside
// [xFrom, xTo) — called right after that strip was blanked ahead of the beam,
// so the graticule survives being swept over.
void ScreenIgnScope::redrawGridInRange(lgfx::LovyanGFX& gfx, int xFrom, int xTo) {
    uint32_t gridCol = gfx.color888(0x30, 0x30, 0x30);
    for (int i = 0; i < 5; i++) {
        if (_gridX[i] >= xFrom && _gridX[i] < xTo)
            gfx.drawFastVLine(_gridX[i], _plotY0 + 1, _plotH - 2, gridCol);
    }
    int midY = _plotY0 + _plotH / 2;
    int x0 = xFrom > _plotX0 + 1 ? xFrom : _plotX0 + 1;
    int x1 = xTo   < _plotX0 + _plotW - 1 ? xTo : _plotX0 + _plotW - 1;
    if (x1 > x0)
        gfx.drawFastHLine(x0, midY, x1 - x0, gridCol);
}

void ScreenIgnScope::update(lgfx::LovyanGFX& gfx, uint16_t contentW, uint16_t contentH) {
    if (_needsRedraw) {
        drawStatic(gfx, contentW, contentH);
        _needsRedraw = false;
    }

    drawHeaderValues(gfx);

    uint32_t elapsed = millis() - _sweepStartMs;
    float frac = (float)(elapsed % SWEEP_MS) / (float)SWEEP_MS;
    int x = _plotX0 + (int)(frac * (_plotW - 1));
    if (x < _plotX0) x = _plotX0;
    if (x > _plotX0 + _plotW - 1) x = _plotX0 + _plotW - 1;

    bool wrapped = (_lastX >= 0) && (x < _lastX);
    if (x == _lastX && !wrapped) return;  // beam hasn't moved a full pixel yet

    // Blank a small gap ahead of the beam (also erases the previous lap's trace)
    int gapFrom = x + 1;
    int gapTo   = x + 1 + GAP_PX;
    if (gapTo > _plotX0 + _plotW) gapTo = _plotX0 + _plotW;
    if (gapTo > gapFrom) {
        gfx.fillRect(gapFrom, _plotY0 + 1, gapTo - gapFrom, _plotH - 2, TFT_BLACK);
        redrawGridInRange(gfx, gapFrom, gapTo);
    }

    int rpmY = valueToY(vdata.rpm,            RPM_LO, RPM_HI);
    int advY = valueToY(vdata.ign_advance_deg, ADV_LO, ADV_HI);

    if (_lastX >= 0 && !wrapped) {
        gfx.drawLine(_lastX, _lastRpmY, x, rpmY, gfx.color888(0x00, 0xFF, 0x88));
        gfx.drawLine(_lastX, _lastAdvY, x, advY, gfx.color888(0x00, 0xBF, 0xFF));
    } else {
        gfx.drawPixel(x, rpmY, gfx.color888(0x00, 0xFF, 0x88));
        gfx.drawPixel(x, advY, gfx.color888(0x00, 0xBF, 0xFF));
    }

    _lastX = x; _lastRpmY = rpmY; _lastAdvY = advY;
}
