#pragma once
#include "screen.h"

class ScreenDash : public Screen {
public:
    const char* name() const override { return "Dashboard"; }
    void init(uint16_t contentW, uint16_t contentH) override;
    void update(lgfx::LovyanGFX& gfx, uint16_t contentW, uint16_t contentH) override;

private:
    int _sCX = 0, _sCY = 0;
    int _rCX = 0, _rCY = 0;
    int _R   = 0;
    int _extCX = 0;
    int _extY0 = 0;

    // Arc change-detection: angle threshold + minimum redraw interval.
    // Arc is only redrawn when BOTH conditions are met (value changed AND
    // enough time has passed) to keep flicker low on a direct-draw display.
    float    _prevSpeedAngle  = -999.f;
    float    _prevRpmAngle    = -999.f;
    uint32_t _lastSpeedDrawMs = 0;
    uint32_t _lastRpmDrawMs   = 0;

    // Previous values for incremental (delta-sector) arc updates.
    // -1 signals "no previous draw" and forces a full redraw.
    float _prevSpeedVal = -1.f;
    float _prevRpmVal   = -1.f;

    // Centre number: redrawn only when the formatted string changes.
    char _fmtSpeedNum[8]  = {};
    char _fmtRpmNum[8]    = {};

    // Extras: stored as "label|value" so a unit-mode switch also triggers redraw.
    char _fmtThrottle[32] = {};
    char _fmtFuel[32]     = {};
    char _fmtMap[32]      = {};

    void drawStaticDial(lgfx::LovyanGFX& gfx, int cx, int cy,
                        float maxV, float majorStep, float minorStep,
                        bool kiloLabels) const;

    // prevVal < 0 forces a full redraw; otherwise only the delta sector is painted.
    void drawArc(lgfx::LovyanGFX& gfx, int cx, int cy,
                 float val, float prevVal, float maxV,
                 float majorStep, float minorStep,
                 float warnFrac, float redFrac,
                 uint32_t normalCol) const;

    // Redraws tick lines whose angle falls within [vLo, vHi].
    void drawTicksInRange(lgfx::LovyanGFX& gfx, int cx, int cy,
                          float vLo, float vHi, float maxV,
                          float majorStep, float minorStep) const;

    void drawNumber(lgfx::LovyanGFX& gfx, int cx, int cy,
                    float val, const char* unit) const;

    // label should include the unit, e.g. "Throttle [%]".
    // stored holds "label|value" — redraws when either label or value changes.
    static void updateExtra(lgfx::LovyanGFX& gfx,
                            int cx, int y,
                            const char* label,
                            char* stored, size_t storedSz,
                            const char* numVal, uint32_t valCol);
};
