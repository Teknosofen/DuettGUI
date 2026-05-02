#pragma once
#include "screen.h"

class ScreenSettings : public Screen {
public:
    const char* name() const override { return "Settings"; }
    void update(lgfx::LovyanGFX& gfx, uint16_t w, uint16_t h) override;
    void onTouch(uint16_t x, uint16_t y) override;

private:
    void drawStatic(lgfx::LovyanGFX& gfx, uint16_t w);
    void drawSimBtn(lgfx::LovyanGFX& gfx);

    bool     _prevSim  = false;
    uint32_t _lastBtnMs = 0;

    static constexpr int BTN_X = 260, BTN_Y = 115, BTN_W = 280, BTN_H = 60;
};
