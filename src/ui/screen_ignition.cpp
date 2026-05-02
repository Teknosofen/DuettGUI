#include "screen_ignition.h"
#include "widgets.h"
#include "../data/vehicle_data.h"
#include "../sensors/ignition_bt.h"
#include <stdio.h>
#include <string.h>

// ── Layout ────────────────────────────────────────────────────────────────────
//
//  y=0–40   Status bar:  ● state-text          "123-ignition"
//  y=40     hRule
//  y=40–165 RPM (left)           |  Advance (right)
//  y=165    hRule
//  y=165–278 Temperature (left)  |  Voltage (right)
//  y=278    hRule
//  y=278–316 MAP / vacuum
//  y=316    hRule
//  y=316–432 Control buttons:  [− ADV]  [TUNE MODE]  [ADV +]
//

// ── Colour palette ────────────────────────────────────────────────────────────
static constexpr uint32_t COL_RPM   = 0x00FF88;
static constexpr uint32_t COL_ADV   = 0x00BFFF;
static constexpr uint32_t COL_TEMP  = 0xFF8000;
static constexpr uint32_t COL_VOLT  = 0xFFFF40;
static constexpr uint32_t COL_PRES  = 0xE0E0E0;
static constexpr uint32_t COL_GRAY  = 0x606060;

static constexpr int VCOL = 400;  // vertical divider x

// ── Helpers ───────────────────────────────────────────────────────────────────

static void setColor(lgfx::LovyanGFX& gfx, uint32_t c) {
    gfx.setTextColor(
        gfx.color888((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF),
        TFT_BLACK);
}

// Erase a rect then draw text left-aligned at (x, y)
static void drawL(lgfx::LovyanGFX& gfx,
                  int x, int y, int eraseW, int eraseH,
                  const char* text, const lgfx::IFont* font, uint32_t col)
{
    gfx.setFont(font);
    gfx.setTextSize(1);
    int fh = (int)gfx.fontHeight();
    gfx.fillRect(x, y, eraseW, fh > eraseH ? fh : eraseH, TFT_BLACK);
    setColor(gfx, col);
    gfx.setCursor(x, y);
    gfx.print(text);
}

// Erase a rect then draw text right-aligned to x=alignRight
static void drawR(lgfx::LovyanGFX& gfx,
                  int eraseX, int y, int eraseW, int eraseH,
                  const char* text, const lgfx::IFont* font, uint32_t col,
                  int alignRight)
{
    gfx.setFont(font);
    gfx.setTextSize(1);
    int fh = (int)gfx.fontHeight();
    gfx.fillRect(eraseX, y, eraseW, fh > eraseH ? fh : eraseH, TFT_BLACK);
    setColor(gfx, col);
    int tw = (int)gfx.textWidth(text);
    gfx.setCursor(alignRight - tw, y);
    gfx.print(text);
}

// Update a stored string; call draw fn only when value changes. Returns true if redrawn.
static bool updateL(lgfx::LovyanGFX& gfx,
                    char* stored, size_t sz, const char* newVal,
                    int x, int y, int eraseW, int eraseH,
                    const lgfx::IFont* font, uint32_t col)
{
    if (strcmp(stored, newVal) == 0) return false;
    drawL(gfx, x, y, eraseW, eraseH, newVal, font, col);
    strncpy(stored, newVal, sz - 1); stored[sz - 1] = '\0';
    return true;
}

static bool updateR(lgfx::LovyanGFX& gfx,
                    char* stored, size_t sz, const char* newVal,
                    int eraseX, int y, int eraseW, int eraseH,
                    const lgfx::IFont* font, uint32_t col, int alignRight)
{
    if (strcmp(stored, newVal) == 0) return false;
    drawR(gfx, eraseX, y, eraseW, eraseH, newVal, font, col, alignRight);
    strncpy(stored, newVal, sz - 1); stored[sz - 1] = '\0';
    return true;
}

// Filled rounded button with centred label
static void drawBtn(lgfx::LovyanGFX& gfx,
                    int x, int y, int w, int h,
                    const char* label, uint32_t bgR, uint32_t bgG, uint32_t bgB)
{
    gfx.fillRoundRect(x, y, w, h, 8, gfx.color888(bgR, bgG, bgB));
    gfx.setFont(&lgfx::fonts::DejaVu18);
    gfx.setTextSize(1);
    gfx.setTextColor(TFT_WHITE, gfx.color888(bgR, bgG, bgB));
    int tw = (int)gfx.textWidth(label);
    int th = (int)gfx.fontHeight();
    gfx.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    gfx.print(label);
}

// ── Screen interface ──────────────────────────────────────────────────────────

void ScreenIgnition::init(uint16_t /*w*/, uint16_t /*h*/) {}

void ScreenIgnition::drawStatic(lgfx::LovyanGFX& gfx) {
    // Title — right-aligned
    gfx.setFont(&lgfx::fonts::DejaVu18);
    gfx.setTextSize(1);
    setColor(gfx, COL_GRAY);
    int tw = (int)gfx.textWidth("123-ignition");
    gfx.setCursor(790 - tw, 8);
    gfx.print("123-ignition");

    Widget::hRule(gfx, 40, 800);

    // RPM section
    Widget::sectionLabel(gfx, 20,  48, "RPM");
    Widget::sectionLabel(gfx, VCOL + 15, 48, "Advance");
    gfx.setFont(&lgfx::fonts::DejaVu18);
    gfx.setTextSize(1);
    setColor(gfx, COL_GRAY);
    gfx.setCursor(20, 140);    gfx.print("rpm");
    gfx.setCursor(VCOL + 15, 133); gfx.print("\xB0 BTDC");

    // Vertical divider between RPM and Advance
    gfx.drawFastVLine(VCOL, 40, 125, gfx.color888(0x30, 0x30, 0x30));

    Widget::hRule(gfx, 165, 800);

    // Temp / Voltage section
    Widget::sectionLabel(gfx, 20, 173, "Temperature");
    Widget::sectionLabel(gfx, VCOL + 15, 173, "Voltage");
    gfx.setFont(&lgfx::fonts::DejaVu18);
    gfx.setTextSize(1);
    setColor(gfx, COL_GRAY);
    gfx.setCursor(20, 250);    gfx.print("\xB0\x43");   // °C
    gfx.setCursor(VCOL + 15, 250); gfx.print("V");

    // Vertical divider between Temp and Voltage
    gfx.drawFastVLine(VCOL, 165, 113, gfx.color888(0x30, 0x30, 0x30));

    Widget::hRule(gfx, 278, 800);

    // MAP row
    Widget::sectionLabel(gfx, 20, 288, "MAP");
    gfx.setFont(&lgfx::fonts::DejaVu18);
    gfx.setTextSize(1);
    setColor(gfx, COL_GRAY);
    gfx.setCursor(215, 288);
    gfx.print("kPa");

    Widget::hRule(gfx, 316, 800);
}

void ScreenIgnition::drawStatus(lgfx::LovyanGFX& gfx, bool force) {
    IgnBtState st  = ignition_bt_state();
    const char* txt = ignition_bt_state_str();

    if (!force && st == _prevState && strcmp(_fmtStatus, txt) == 0) return;
    _prevState = st;
    strncpy(_fmtStatus, txt, sizeof(_fmtStatus) - 1);

    // Dot color
    uint32_t dotR, dotG, dotB;
    switch (st) {
        case IgnBtState::ACTIVE:
            dotR = 0x00; dotG = 0xE0; dotB = 0x00; break;
        case IgnBtState::SCANNING:
        case IgnBtState::CONNECTING:
        case IgnBtState::HANDSHAKING:
            dotR = 0xFF; dotG = 0xBF; dotB = 0x00; break;  // amber
        default:
            dotR = 0x60; dotG = 0x60; dotB = 0x60; break;  // gray
    }
    gfx.fillCircle(18, 22, 9, gfx.color888(dotR, dotG, dotB));

    // Status text (erase then redraw)
    gfx.fillRect(34, 6, 500, 26, TFT_BLACK);
    gfx.setFont(&lgfx::fonts::DejaVu18);
    gfx.setTextSize(1);
    setColor(gfx, st == IgnBtState::ACTIVE ? 0x00FF88 : COL_GRAY);
    gfx.setCursor(34, 8);
    gfx.print(txt);
    if (st == IgnBtState::ACTIVE && vdata.ign_tune_mode) {
        gfx.setFont(&lgfx::fonts::DejaVu12);
        gfx.setTextSize(1);
        setColor(gfx, 0xFFBF00);
        gfx.setCursor(34 + (int)gfx.textWidth(txt) + 12, 12);
        gfx.print("TUNE MODE");
    }
}

void ScreenIgnition::drawControls(lgfx::LovyanGFX& gfx, bool force) {
    bool tune = vdata.ign_tune_mode;
    if (!force && tune == _prevTune) return;
    _prevTune = tune;

    drawBtn(gfx, BTN0_X, BTN_Y, BTN0_W, BTN_H,
            "- ADV", 0x28, 0x28, 0x28);
    drawBtn(gfx, BTN1_X, BTN_Y, BTN1_W, BTN_H,
            tune ? "TUNE  ON" : "TUNE MODE",
            tune ? 0x6F : 0x28,
            tune ? 0x4F : 0x28,
            tune ? 0x00 : 0x28);
    drawBtn(gfx, BTN2_X, BTN_Y, BTN2_W, BTN_H,
            "ADV +", 0x28, 0x28, 0x28);
}

void ScreenIgnition::update(lgfx::LovyanGFX& gfx, uint16_t contentW, uint16_t contentH) {
    char buf[12];

    if (_needsRedraw) {
        gfx.fillRect(0, 0, contentW, contentH, TFT_BLACK);
        drawStatic(gfx);
        _fmtRpm[0] = _fmtAdv[0] = _fmtTemp[0] = '\0';
        _fmtVolt[0] = _fmtPres[0] = _fmtStatus[0] = '\0';
        _prevTune  = !vdata.ign_tune_mode;  // force control redraw
        _prevState = IgnBtState::ERROR;     // force status redraw
        _needsRedraw = false;
    }

    // Status bar
    drawStatus(gfx, false);

    // ── RPM — DejaVu56, right-aligned to x=388 ───────────────────────────────
    snprintf(buf, sizeof(buf), "%.0f", vdata.rpm);
    updateR(gfx, _fmtRpm, sizeof(_fmtRpm), buf,
            160, 68, 228, 64,
            &lgfx::fonts::DejaVu56, COL_RPM, 388);

    // ── Advance — DejaVu40, left of right column ──────────────────────────────
    snprintf(buf, sizeof(buf), "%.1f", vdata.ign_advance_deg);
    updateL(gfx, _fmtAdv, sizeof(_fmtAdv), buf,
            VCOL + 15, 76, 175, 48,
            &lgfx::fonts::DejaVu40, COL_ADV);

    // ── Temperature — DejaVu40, right-aligned ────────────────────────────────
    snprintf(buf, sizeof(buf), "%.0f", vdata.ign_temp_c);
    updateR(gfx, _fmtTemp, sizeof(_fmtTemp), buf,
            160, 194, 228, 48,
            &lgfx::fonts::DejaVu40, COL_TEMP, 388);

    // ── Voltage — DejaVu40, left of right column ─────────────────────────────
    snprintf(buf, sizeof(buf), "%.1f", vdata.ign_voltage_v);
    updateL(gfx, _fmtVolt, sizeof(_fmtVolt), buf,
            VCOL + 15, 194, 175, 48,
            &lgfx::fonts::DejaVu40, COL_VOLT);

    // ── MAP pressure — DejaVu24 ───────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "%.0f", vdata.ign_pressure_kpa);
    updateL(gfx, _fmtPres, sizeof(_fmtPres), buf,
            95, 282, 115, 30,
            &lgfx::fonts::DejaVu24, COL_PRES);

    // Control buttons (redrawn only when tune mode toggles)
    drawControls(gfx, false);
}

void ScreenIgnition::onTouch(uint16_t x, uint16_t y) {
    if (y < BTN_Y || y >= (BTN_Y + BTN_H)) return;

    int btn;
    if      (x < BTN1_X)           btn = 0;  // advance −
    else if (x < BTN2_X)           btn = 1;  // tune toggle
    else                            btn = 2;  // advance +

    uint32_t debounce = (btn == 1) ? 600 : 350;
    if (millis() - _btnMs[btn] < debounce) return;
    _btnMs[btn] = millis();

    switch (btn) {
        case 0: ignition_send_advance_minus(); break;
        case 1: ignition_send_tune_toggle();   break;
        case 2: ignition_send_advance_plus();  break;
    }
}
