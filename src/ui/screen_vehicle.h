#pragma once
#include "screen.h"

class ScreenVehicle : public Screen {
public:
    void        init(uint16_t contentW, uint16_t contentH) override;
    void        update(lgfx::LovyanGFX& gfx,
                       uint16_t contentW, uint16_t contentH) override;
    const char* name() const override { return "Vehicle"; }

private:
    int  _startY = 0;

    // Left column (engine): RPM, Throttle, MAP, Ambient
    char _fmtRpm[10]      = "";
    char _fmtThrottle[10] = "";
    char _fmtMap[10]      = "";
    char _fmtAmbient[10]  = "";

    // Right column (fuel): flow, per-100km, used
    char _fmtFuelFlow[10]  = "";
    char _fmtFuel100[10]   = "";
    char _fmtFuelUsed[10]  = "";
};
