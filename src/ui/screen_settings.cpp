#include "screen_settings.h"
#include "widgets.h"
#include "../data/sim.h"

// ── Layout (800 × 432 content area) ──────────────────────────────────────────
//
//  y=18    "Settings" title (DejaVu24)
//  y=50    hRule
//  y=68    "Sensor Simulation" section label
//  y=115   [  ● SIM ON  ] or [  ○ SIM OFF  ] toggle button (centred)
//  y=195   Description text (DejaVu12, gray)
//  y=290   hRule
//  y=308   "RPM source" section label
//  y=335   Info lines about current RPM source

static void setGray(lgfx::LovyanGFX& gfx) {
    gfx.setTextColor(gfx.color888(0x70, 0x70, 0x70), TFT_BLACK);
}

void ScreenSettings::drawStatic(lgfx::LovyanGFX& gfx, uint16_t /*w*/) {
    gfx.setFont(&lgfx::fonts::DejaVu24);
    gfx.setTextSize(1);
    gfx.setTextColor(gfx.color888(0xE0, 0xE0, 0xE0), TFT_BLACK);
    gfx.setCursor(20, 18);
    gfx.print("Settings");

    Widget::hRule(gfx, 52, 800);
    Widget::sectionLabel(gfx, 20, 70, "Sensor Simulation");

    Widget::hRule(gfx, 290, 800);
    Widget::sectionLabel(gfx, 20, 308, "RPM source");

    // RPM source explanation
    gfx.setFont(&lgfx::fonts::DejaVu12);
    gfx.setTextSize(1);
    setGray(gfx);
    gfx.setCursor(20, 336); gfx.print("Simulation ON  \xBB  synthetic drive cycle (overrides all sensors)");
    gfx.setCursor(20, 358); gfx.print("Simulation OFF + 123-ignition connected  \xBB  RPM from BLE");
    gfx.setCursor(20, 380); gfx.print("Simulation OFF + no BLE  \xBB  RPM = 0");
}

void ScreenSettings::drawSimBtn(lgfx::LovyanGFX& gfx) {
    bool on = sim_running();
    uint32_t bgR = on ? 0x00 : 0x28;
    uint32_t bgG = on ? 0x7F : 0x28;
    uint32_t bgB = on ? 0x00 : 0x28;

    gfx.fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8,
                      gfx.color888(bgR, bgG, bgB));
    gfx.setFont(&lgfx::fonts::DejaVu18);
    gfx.setTextSize(1);
    gfx.setTextColor(TFT_WHITE, gfx.color888(bgR, bgG, bgB));

    const char* label = on ? "\xB7 SIM ON" : "\xB7 SIM OFF";
    int tw = (int)gfx.textWidth(label);
    int th = (int)gfx.fontHeight();
    gfx.setCursor(BTN_X + (BTN_W - tw) / 2, BTN_Y + (BTN_H - th) / 2);
    gfx.print(label);

    // Description below button
    gfx.fillRect(20, 188, 760, 90, TFT_BLACK);
    gfx.setFont(&lgfx::fonts::DejaVu12);
    gfx.setTextSize(1);
    setGray(gfx);
    if (on) {
        gfx.setCursor(20, 196);
        gfx.print("Simulation is active. Synthetic speed, RPM, GPS and fuel data");
        gfx.setCursor(20, 214);
        gfx.print("override all real sensors. BLE ignition data is still received");
        gfx.setCursor(20, 232);
        gfx.print("but RPM is not written to the dashboard while sim is running.");
    } else {
        gfx.setCursor(20, 196);
        gfx.print("Simulation is OFF. Dashboard uses real sensor data.");
        gfx.setCursor(20, 214);
        gfx.print("RPM comes from the 123-ignition BLE module when connected.");
    }
}

void ScreenSettings::update(lgfx::LovyanGFX& gfx, uint16_t w, uint16_t h) {
    if (_needsRedraw) {
        gfx.fillRect(0, 0, w, h, TFT_BLACK);
        drawStatic(gfx, w);
        _prevSim = !sim_running();  // force button redraw
        _needsRedraw = false;
    }

    bool simNow = sim_running();
    if (simNow != _prevSim) {
        _prevSim = simNow;
        drawSimBtn(gfx);
    }
}

void ScreenSettings::onTouch(uint16_t x, uint16_t y) {
    if (x < BTN_X || x >= BTN_X + BTN_W) return;
    if (y < BTN_Y || y >= BTN_Y + BTN_H) return;
    if (millis() - _lastBtnMs < 400) return;
    _lastBtnMs = millis();
    sim_set(!sim_running());
}
