#include "screen_gps.h"
#include "widgets.h"
#include "../data/vehicle_data.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// ── helpers ──────────────────────────────────────────────────────────────────

static const char* compassPoint(float deg)
{
    static const char* pts[16] = {
        "N","NNE","NE","ENE","E","ESE","SE","SSE",
        "S","SSW","SW","WSW","W","WNW","NW","NNW"
    };
    return pts[(int)((deg + 11.25f) / 22.5f) % 16];
}

static void fmtCoord(char* buf, size_t len, double dd, bool isLat)
{
    char   dir    = isLat ? (dd >= 0 ? 'N' : 'S') : (dd >= 0 ? 'E' : 'W');
    double absDD  = fabs(dd);
    int    deg    = (int)absDD;
    double min    = (absDD - deg) * 60.0;
    snprintf(buf, len, "%d\xB0 %08.4f' %c", deg, min, dir);
}

// Erase a rectangle then draw text at (x, y) in the given size and colour.
static void redrawText(lgfx::LovyanGFX& gfx,
                       int x, int y, int eraseW, int eraseH,
                       const char* text, int textSize, uint32_t col)
{
    gfx.fillRect(x, y, eraseW, eraseH, TFT_BLACK);
    gfx.setTextSize(textSize);
    gfx.setTextColor(
        gfx.color888((col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF),
        TFT_BLACK);
    gfx.setCursor(x, y);
    gfx.print(text);
}

// Same but right-aligned: text ends at x=alignRight.
static void redrawTextRight(lgfx::LovyanGFX& gfx,
                             int eraseX, int y, int eraseW, int eraseH,
                             const char* text, int textSize, uint32_t col,
                             int alignRight)
{
    gfx.fillRect(eraseX, y, eraseW, eraseH, TFT_BLACK);
    gfx.setTextSize(textSize);
    gfx.setTextColor(
        gfx.color888((col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF),
        TFT_BLACK);
    int tw = (int)gfx.textWidth(text);
    gfx.setCursor(alignRight - tw, y);
    gfx.print(text);
}

// Update a stored string; call redraw only when the value changes.
// Returns true if redrawn.
static bool updateField(lgfx::LovyanGFX& gfx,
                        char* stored, size_t storedSz, const char* newVal,
                        int x, int y, int eraseW, int eraseH,
                        int textSize, uint32_t col)
{
    if (strcmp(stored, newVal) == 0) return false;
    redrawText(gfx, x, y, eraseW, eraseH, newVal, textSize, col);
    strncpy(stored, newVal, storedSz - 1);
    stored[storedSz - 1] = '\0';
    return true;
}

static bool updateFieldRight(lgfx::LovyanGFX& gfx,
                              char* stored, size_t storedSz, const char* newVal,
                              int eraseX, int y, int eraseW, int eraseH,
                              int textSize, uint32_t col, int alignRight)
{
    if (strcmp(stored, newVal) == 0) return false;
    redrawTextRight(gfx, eraseX, y, eraseW, eraseH, newVal, textSize, col, alignRight);
    strncpy(stored, newVal, storedSz - 1);
    stored[storedSz - 1] = '\0';
    return true;
}

// ── Screen interface ─────────────────────────────────────────────────────────

void ScreenGPS::init(uint16_t contentW, uint16_t contentH)
{
    _contentW = contentW;
}

void ScreenGPS::drawStatic(lgfx::LovyanGFX& gfx, uint16_t w, uint16_t h)
{
    // Section labels
    Widget::sectionLabel(gfx, 30,      18,  "Speed");
    Widget::sectionLabel(gfx, 450,     18,  "Heading");
    Widget::sectionLabel(gfx, 30,      222, "Position");

    // Units that never change
    gfx.setTextSize(3);
    gfx.setTextColor(gfx.color888(0x50, 0x50, 0x50), TFT_BLACK);
    gfx.setCursor(30,  170); gfx.print("km/h");
    gfx.setCursor(450, 145); gfx.print("deg");

    // Static "Altitude " prefix on the altitude line
    gfx.setTextSize(2);
    gfx.setTextColor(gfx.color888(0x70, 0x70, 0x70), TFT_BLACK);
    gfx.setCursor(30, 350); gfx.print("Altitude");

    // Vertical divider between speed and heading columns
    gfx.drawFastVLine(430, 10, 195, gfx.color888(0x30, 0x30, 0x30));

    // Separators
    Widget::hRule(gfx, 210, w);
    Widget::hRule(gfx, 380, w);
}

void ScreenGPS::update(lgfx::LovyanGFX& gfx, uint16_t contentW, uint16_t contentH)
{
    char buf[30];

    if (_needsRedraw) {
        gfx.fillRect(0, 0, contentW, contentH, TFT_BLACK);
        drawStatic(gfx, contentW, contentH);

        // Force redraw of all dynamic fields by clearing stored strings
        _fmtSpeed[0] = _fmtHeading[0] = _fmtCompass[0] = '\0';
        _fmtLat[0]   = _fmtLon[0]     = _fmtAlt[0]     = _fmtFuel[0] = '\0';
        _gpsStatusDrawn = false;
        _needsRedraw = false;
    }

    // ── Speed (size 7, green) ─────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "%.1f", vdata.speed_kmh);
    updateField(gfx, _fmtSpeed, sizeof(_fmtSpeed), buf,
                30, 50, 216, 60,
                7, 0x00FF88);

    // ── Heading (size 5, cyan) ────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "%.1f", vdata.heading_deg);
    updateField(gfx, _fmtHeading, sizeof(_fmtHeading), buf,
                450, 52, 156, 44,
                5, 0x00BFFF);

    // ── Compass point (size 5, amber, right-aligned) ──────────────────────
    updateFieldRight(gfx, _fmtCompass, sizeof(_fmtCompass),
                     compassPoint(vdata.heading_deg),
                     contentW - 116, 82, 112, 44,
                     5, 0xFFBF00, contentW - 20);

    // ── Latitude ──────────────────────────────────────────────────────────
    fmtCoord(buf, sizeof(buf), vdata.lat, true);
    updateField(gfx, _fmtLat, sizeof(_fmtLat), buf,
                30, 254, 276, 28,
                3, 0xE0E0E0);

    // ── Longitude ─────────────────────────────────────────────────────────
    fmtCoord(buf, sizeof(buf), vdata.lon, false);
    updateField(gfx, _fmtLon, sizeof(_fmtLon), buf,
                30, 298, 276, 28,
                3, 0xE0E0E0);

    // ── Altitude value (after the static "Altitude" label) ───────────────
    snprintf(buf, sizeof(buf), "  %.0f m", vdata.altitude_m);
    updateField(gfx, _fmtAlt, sizeof(_fmtAlt), buf,
                30 + 8 * 12, 350, 110, 18,   // "Altitude" = 8 chars × 12px
                2, 0x90D0FF);

    // ── Fuel economy / flow ───────────────────────────────────────────────
    if (vdata.gps_valid && vdata.speed_kmh > 2.0f)
        snprintf(buf, sizeof(buf), "%.1f L/100km", vdata.fuel_per_100km);
    else
        snprintf(buf, sizeof(buf), "%.1f L/h", vdata.fuel_flow_lph);

    updateField(gfx, _fmtFuel, sizeof(_fmtFuel), buf,
                30, 397, 200, 18,
                2, 0xFF8000);

    // ── GPS status dot + label ────────────────────────────────────────────
    if (!_gpsStatusDrawn || vdata.gps_valid != _prevGpsValid) {
        bool ok     = vdata.gps_valid;
        int  dotX   = (int)contentW - 28;
        int  dotY   = 404;

        gfx.fillCircle(dotX, dotY, 10,
            ok ? gfx.color888(0x00, 0xE0, 0x00)
               : gfx.color888(0xE0, 0x00, 0x00));

        const char* statusTxt = ok ? "GPS fix" : "No fix";
        gfx.setTextSize(2);
        gfx.setTextColor(gfx.color888(0x80, 0x80, 0x80), TFT_BLACK);
        int stw = (int)gfx.textWidth(statusTxt);
        // Erase text area then redraw
        gfx.fillRect(dotX - 20 - stw, 397, stw + 4, 18, TFT_BLACK);
        gfx.setCursor(dotX - 18 - stw, 397);
        gfx.print(statusTxt);

        _prevGpsValid   = ok;
        _gpsStatusDrawn = true;
    }
}
