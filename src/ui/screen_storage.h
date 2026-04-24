#pragma once
#include "screen.h"

class ScreenStorage : public Screen {
public:
    const char* name() const override { return "Storage"; }
    void init(uint16_t contentW, uint16_t contentH) override;
    void update(lgfx::LovyanGFX& gfx, uint16_t contentW, uint16_t contentH) override;
    void onTouch(uint16_t x, uint16_t y) override;

private:
    uint16_t _contentW = 0;

    // Button area for logging toggle (centred, y=155–210)
    static constexpr int BTN_X = 280, BTN_Y = 155, BTN_W = 240, BTN_H = 50;

    // Touch debounce
    uint32_t _lastToggleMs = 0;

    // Partial-update state
    bool     _prevEnabled    = false;
    uint32_t _prevRecords    = 0xFFFFFFFF;
    bool     _prevSdOk       = false;
    bool     _staticDrawn    = false;

    void drawButton(lgfx::LovyanGFX& gfx, bool enabled);
    void drawDynamic(lgfx::LovyanGFX& gfx);
};
