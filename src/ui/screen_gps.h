#pragma once
#include "screen.h"

class ScreenGPS : public Screen {
public:
    void        init(uint16_t contentW, uint16_t contentH) override;
    void        update(lgfx::LovyanGFX& gfx,
                       uint16_t contentW, uint16_t contentH) override;
    const char* name() const override { return "GPS"; }

private:
    uint16_t _contentW = 0;

    void drawStatic(lgfx::LovyanGFX& gfx, uint16_t w, uint16_t h);

    // Stored formatted strings for change detection
    char _fmtSpeed[12]   = "";
    char _fmtHeading[12] = "";
    char _fmtCompass[6]  = "";
    char _fmtLat[30]     = "";
    char _fmtLon[30]     = "";
    char _fmtAlt[14]     = "";
    char _fmtFuel[22]    = "";
    bool _prevGpsValid   = false;
    bool _gpsStatusDrawn = false;
};
