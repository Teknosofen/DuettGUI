#include "screen_dash.h"
#include "../data/vehicle_data.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// ── Constants ────────────────────────────────────────────────────────────────

// Minimum degrees of arc change before a redraw is triggered.
static constexpr float    ARC_THRESHOLD = 1.0f;

// Minimum ms between arc redraws — caps flicker on a direct-draw display.
static constexpr uint32_t ARC_MIN_MS    = 40;    // ≈ 25 fps

// ── Angle conventions ────────────────────────────────────────────────────────
//
// ptAt / tick placement  : 0° = 12-o'clock, CW positive  (DIAL_START)
// LovyanGFX fillArc      : 0° =  3-o'clock, CW positive  (ARC_START)
//
// The two conventions are offset by exactly 90°.
// Gauge arc: 7:30-o'clock → 4:30-o'clock, 270° clockwise sweep.

static constexpr float DIAL_START = 225.0f;   // ptAt: 7:30-o'clock
static constexpr float ARC_START  = 135.0f;   // fillArc: same position (225-90)
static constexpr float DIAL_SWEEP = 270.0f;

// Map value to ptAt angle (12-o'clock convention).
static float valToAngle(float v, float maxV)
{
    float t = v / maxV;
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    return DIAL_START + t * DIAL_SWEEP;
}

// Map value to fillArc angle (3-o'clock convention).
static float valToArcAngle(float v, float maxV)
{
    float t = v / maxV;
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    return ARC_START + t * DIAL_SWEEP;
}

// Screen coordinates for a point at ptAt angle θ on a circle of radius r.
static void ptAt(int cx, int cy, int r, float deg, int& px, int& py)
{
    float rad = deg * (float)M_PI / 180.f;
    px = cx + (int)(r * sinf(rad) + 0.5f);
    py = cy - (int)(r * cosf(rad) + 0.5f);
}

// ── Colour helpers ───────────────────────────────────────────────────────────

static uint16_t rgb(lgfx::LovyanGFX& gfx, uint32_t c)
{
    return (uint16_t)gfx.color888((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

static uint32_t arcRGB(float val, float maxV, float warnFrac, float redFrac,
                       uint32_t normal)
{
    float t = val / maxV;
    if (t >= redFrac)  return 0xFF3333;
    if (t >= warnFrac) return 0xFFBF00;
    return normal;
}

// ── ScreenDash::init ─────────────────────────────────────────────────────────

void ScreenDash::init(uint16_t contentW, uint16_t contentH)
{
    _R    = 140;
    _sCX  = 175;
    _rCX  = (int)contentW - 175;
    _sCY  = _rCY = (int)contentH / 2 + 10;
    _extCX = (int)contentW / 2;
    _extY0 = _sCY - 75;
}

// ── Static dial — tick labels + rim (drawn once per page entry) ──────────────

void ScreenDash::drawStaticDial(lgfx::LovyanGFX& gfx, int cx, int cy,
                                 float maxV, float majorStep, float minorStep,
                                 bool kiloLabels) const
{
    const int LBL_R = _R + 20;

    gfx.drawCircle(cx, cy, _R + 3, gfx.color888(0x38, 0x38, 0x38));

    gfx.setTextSize(2);
    for (float v = 0; v <= maxV + 0.01f; v += majorStep) {
        float angle = valToAngle(v, maxV);   // ptAt convention — tick labels only
        char lbl[6];
        snprintf(lbl, sizeof(lbl), "%.0f", kiloLabels ? v / 1000.f : v);
        int lx, ly;
        ptAt(cx, cy, LBL_R, angle, lx, ly);
        gfx.setTextColor(gfx.color888(0x88, 0x88, 0x88), TFT_BLACK);
        int tw = (int)gfx.textWidth(lbl);
        int th = (int)gfx.fontHeight();
        gfx.setCursor(lx - tw / 2, ly - th / 2);
        gfx.print(lbl);
    }

    if (kiloLabels) {
        gfx.setTextSize(1);
        gfx.setTextColor(gfx.color888(0x44, 0x44, 0x44), TFT_BLACK);
        const char* hint = "x1000 rpm";
        gfx.setCursor(cx - (int)gfx.textWidth(hint) / 2, cy + 46);
        gfx.print(hint);
    }
}

// ── Tick lines within a value range ─────────────────────────────────────────

void ScreenDash::drawTicksInRange(lgfx::LovyanGFX& gfx, int cx, int cy,
                                   float vLo, float vHi, float maxV,
                                   float majorStep, float minorStep) const
{
    const int TRACK_OUT = _R - 1;
    const int MAJ_IN    = _R - 20;
    const int MIN_IN    = _R - 11;
    float aLo = valToAngle(vLo, maxV);
    float aHi = valToAngle(vHi, maxV);
    for (float v = 0; v <= maxV + 0.01f; v += minorStep) {
        float angle = valToAngle(v, maxV);
        if (angle < aLo - 0.5f || angle > aHi + 0.5f) continue;
        bool major = fmodf(v + 0.01f, majorStep) < 0.02f;
        int x0, y0, x1, y1;
        ptAt(cx, cy, TRACK_OUT,              angle, x0, y0);
        ptAt(cx, cy, major ? MAJ_IN : MIN_IN, angle, x1, y1);
        gfx.drawLine(x0, y0, x1, y1,
                     major ? gfx.color888(0xC8, 0xC8, 0xC8)
                           : gfx.color888(0x52, 0x52, 0x52));
    }
}

// ── Arc + tick lines (delta-sector update to eliminate flicker) ──────────────
//
// Full redraw (gray track + colored arc + all ticks) is done only on:
//   - first draw after page entry (prevVal < 0)
//   - color zone change (value crosses warn/red threshold)
// All other frames paint only the sector that changed:
//   - value grew  → fill new segment with color, redraw ticks on top
//   - value shrank → fill vacated segment with gray, redraw ticks on top

void ScreenDash::drawArc(lgfx::LovyanGFX& gfx, int cx, int cy,
                          float val, float prevVal, float maxV,
                          float majorStep, float minorStep,
                          float warnFrac, float redFrac,
                          uint32_t normalCol) const
{
    const int TRACK_OUT = _R - 1;
    const int TRACK_IN  = _R - 21;

    uint32_t col     = arcRGB(val,     maxV, warnFrac, redFrac, normalCol);
    bool fullRedraw  = (prevVal < 0.f) ||
                       (col != arcRGB(prevVal, maxV, warnFrac, redFrac, normalCol));

    if (fullRedraw) {
        gfx.fillArc(cx, cy, TRACK_IN, TRACK_OUT,
                    ARC_START, ARC_START + DIAL_SWEEP,
                    gfx.color888(0x22, 0x22, 0x22));
        if (val > 0.f) {
            gfx.fillArc(cx, cy, TRACK_IN, TRACK_OUT,
                        ARC_START, valToArcAngle(val, maxV),
                        rgb(gfx, col));
        }
        drawTicksInRange(gfx, cx, cy, 0.f, maxV, maxV, majorStep, minorStep);

    } else if (val > prevVal) {
        // Grew: fill new segment with color, redraw ticks on top.
        gfx.fillArc(cx, cy, TRACK_IN, TRACK_OUT,
                    valToArcAngle(prevVal, maxV), valToArcAngle(val, maxV),
                    rgb(gfx, col));
        drawTicksInRange(gfx, cx, cy, prevVal, val, maxV, majorStep, minorStep);

    } else {
        // Shrank: erase vacated segment to gray, redraw ticks on top.
        gfx.fillArc(cx, cy, TRACK_IN, TRACK_OUT,
                    valToArcAngle(val, maxV), valToArcAngle(prevVal, maxV),
                    gfx.color888(0x22, 0x22, 0x22));
        drawTicksInRange(gfx, cx, cy, val, prevVal, maxV, majorStep, minorStep);
    }
}

// ── Centre number (drawn when formatted string changes) ──────────────────────

void ScreenDash::drawNumber(lgfx::LovyanGFX& gfx, int cx, int cy,
                             float val, const char* unit) const
{
    gfx.fillRect(cx - 68, cy - 42, 136, 76, TFT_BLACK);

    char buf[8];
    snprintf(buf, sizeof(buf), "%.0f", val);
    gfx.setTextSize(5);
    gfx.setTextColor(TFT_WHITE, TFT_BLACK);
    int tw = (int)gfx.textWidth(buf);
    gfx.setCursor(cx - tw / 2, cy - 42);
    gfx.print(buf);

    gfx.setTextSize(2);
    gfx.setTextColor(gfx.color888(0x68, 0x68, 0x68), TFT_BLACK);
    int uw = (int)gfx.textWidth(unit);
    gfx.setCursor(cx - uw / 2, cy + 14);
    gfx.print(unit);
}

// ── Extras (drawn when label or value changes) ───────────────────────────────

void ScreenDash::updateExtra(lgfx::LovyanGFX& gfx,
                              int cx, int y,
                              const char* label,
                              char* stored, size_t storedSz,
                              const char* numVal, uint32_t valCol)
{
    // Key combines label + value so a unit-mode change also triggers redraw.
    char key[48];
    snprintf(key, sizeof(key), "%s|%s", label, numVal);
    if (strcmp(stored, key) == 0) return;
    strncpy(stored, key, storedSz - 1);
    stored[storedSz - 1] = '\0';

    const int COL_W = 140;
    gfx.fillRect(cx - COL_W / 2, y, COL_W, 36, TFT_BLACK);

    // Label line (includes unit)
    gfx.setTextSize(1);
    gfx.setTextColor(gfx.color888(0x55, 0x55, 0x55), TFT_BLACK);
    int lw = (int)gfx.textWidth(label);
    gfx.setCursor(cx - lw / 2, y);
    gfx.print(label);

    // Value line (number only)
    gfx.setTextSize(2);
    gfx.setTextColor(rgb(gfx, valCol), TFT_BLACK);
    int vw = (int)gfx.textWidth(numVal);
    gfx.setCursor(cx - vw / 2, y + 14);
    gfx.print(numVal);
}

// ── ScreenDash::update ───────────────────────────────────────────────────────

void ScreenDash::update(lgfx::LovyanGFX& gfx, uint16_t contentW, uint16_t contentH)
{
    if (_needsRedraw) {
        gfx.fillRect(0, 0, contentW, contentH, TFT_BLACK);

        gfx.setTextSize(2);
        gfx.setTextColor(gfx.color888(0x50, 0x50, 0x50), TFT_BLACK);
        gfx.setCursor(_sCX - (int)gfx.textWidth("Speed") / 2, 8);
        gfx.print("Speed");
        gfx.setCursor(_rCX - (int)gfx.textWidth("Engine RPM") / 2, 8);
        gfx.print("Engine RPM");

        drawStaticDial(gfx, _sCX, _sCY, 140.f,  20.f,   10.f,  false);
        drawStaticDial(gfx, _rCX, _rCY, 6000.f, 1000.f, 500.f, true);

        uint16_t divCol = gfx.color888(0x28, 0x28, 0x28);
        gfx.drawFastVLine(_sCX + _R + 24, 30, contentH - 40, divCol);
        gfx.drawFastVLine(_rCX - _R - 24, 30, contentH - 40, divCol);

        // Invalidate all change-detection state
        _prevSpeedAngle = _prevRpmAngle = -999.f;
        _lastSpeedDrawMs = _lastRpmDrawMs = 0;
        _prevSpeedVal = _prevRpmVal = -1.f;
        _fmtSpeedNum[0] = _fmtRpmNum[0] = '\0';
        _fmtThrottle[0] = _fmtFuel[0] = _fmtMap[0] = '\0';
        _needsRedraw = false;
    }

    uint32_t now = millis();

    // ── Speed dial ───────────────────────────────────────────────────────────
    {
        float angle = valToAngle(vdata.speed_kmh, 140.f);
        if (fabsf(angle - _prevSpeedAngle) >= ARC_THRESHOLD &&
            (now - _lastSpeedDrawMs) >= ARC_MIN_MS) {
            drawArc(gfx, _sCX, _sCY,
                    vdata.speed_kmh, _prevSpeedVal, 140.f, 20.f, 10.f,
                    0.72f, 0.86f, 0x00BFFF);
            _prevSpeedVal    = vdata.speed_kmh;
            _prevSpeedAngle  = angle;
            _lastSpeedDrawMs = now;
        }
        char buf[8];
        snprintf(buf, sizeof(buf), "%.0f", vdata.speed_kmh);
        if (strcmp(buf, _fmtSpeedNum) != 0) {
            drawNumber(gfx, _sCX, _sCY, vdata.speed_kmh, "km/h");
            strncpy(_fmtSpeedNum, buf, sizeof(_fmtSpeedNum) - 1);
        }
    }

    // ── RPM dial ─────────────────────────────────────────────────────────────
    {
        float angle = valToAngle(vdata.rpm, 6000.f);
        if (fabsf(angle - _prevRpmAngle) >= ARC_THRESHOLD &&
            (now - _lastRpmDrawMs) >= ARC_MIN_MS) {
            drawArc(gfx, _rCX, _rCY,
                    vdata.rpm, _prevRpmVal, 6000.f, 1000.f, 500.f,
                    0.75f, 0.92f, 0x00FF88);
            _prevRpmVal    = vdata.rpm;
            _prevRpmAngle  = angle;
            _lastRpmDrawMs = now;
        }
        char buf[8];
        snprintf(buf, sizeof(buf), "%.0f", vdata.rpm);
        if (strcmp(buf, _fmtRpmNum) != 0) {
            drawNumber(gfx, _rCX, _rCY, vdata.rpm, "rpm");
            strncpy(_fmtRpmNum, buf, sizeof(_fmtRpmNum) - 1);
        }
    }

    // ── Centre extras ─────────────────────────────────────────────────────────
    char buf[20];

    snprintf(buf, sizeof(buf), "%.1f", vdata.throttle_pct);
    updateExtra(gfx, _extCX, _extY0,
                "Throttle [%]", _fmtThrottle, sizeof(_fmtThrottle),
                buf, 0x00BFFF);

    const char* fuelLabel;
    if (vdata.gps_valid && vdata.speed_kmh > 2.f) {
        snprintf(buf, sizeof(buf), "%.1f", vdata.fuel_per_100km);
        fuelLabel = "Fuel [L/100km]";
    } else {
        snprintf(buf, sizeof(buf), "%.1f", vdata.fuel_flow_lph);
        fuelLabel = "Fuel [L/h]";
    }
    updateExtra(gfx, _extCX, _extY0 + 55,
                fuelLabel, _fmtFuel, sizeof(_fmtFuel),
                buf, 0xFF8000);

    snprintf(buf, sizeof(buf), "%.0f", vdata.map_kpa);
    updateExtra(gfx, _extCX, _extY0 + 110,
                "MAP [kPa]", _fmtMap, sizeof(_fmtMap),
                buf, 0xFF8C00);
}
