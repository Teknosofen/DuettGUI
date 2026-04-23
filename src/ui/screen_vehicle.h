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

    // Stored formatted strings — compared each frame to skip unchanged values
    char _fmtRpm[10]      = "";
    char _fmtThrottle[10] = "";
    char _fmtMap[10]      = "";
    char _fmtAmbient[10]  = "";
    char _fmtFuel[10]     = "";
};
