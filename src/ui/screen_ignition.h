#pragma once
#include "screen.h"
#include "../sensors/ignition_bt.h"

class ScreenIgnition : public Screen {
public:
    const char* name() const override { return "Ignition"; }
    void init(uint16_t w, uint16_t h) override;
    void update(lgfx::LovyanGFX& gfx, uint16_t w, uint16_t h) override;
    void onTouch(uint16_t x, uint16_t y) override;

private:
    void drawStatic(lgfx::LovyanGFX& gfx);
    void drawStatus(lgfx::LovyanGFX& gfx, bool force);
    void drawControls(lgfx::LovyanGFX& gfx, bool force);

    // Stored strings for partial-update comparison
    char _fmtRpm[8]     = "";
    char _fmtAdv[8]     = "";
    char _fmtTemp[8]    = "";
    char _fmtVolt[8]    = "";
    char _fmtPres[8]    = "";
    char _fmtStatus[32] = "";

    IgnBtState _prevState = IgnBtState::IDLE;
    bool       _prevTune  = false;

    // Touch debounce per button (0=−, 1=tune, 2=+)
    uint32_t _btnMs[3] = { 0, 0, 0 };

    // Layout constants
    static constexpr int BTN_Y  = 328;
    static constexpr int BTN_H  = 70;
    static constexpr int BTN0_X = 10,  BTN0_W = 230;  // advance −
    static constexpr int BTN1_X = 250, BTN1_W = 300;  // tune mode
    static constexpr int BTN2_X = 560, BTN2_W = 230;  // advance +
};
