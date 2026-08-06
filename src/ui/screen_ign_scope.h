#pragma once
#include "screen.h"

// Oscilloscope-style sweep of RPM (green) and ignition advance (cyan) over a
// fixed 60 s window. Unlike a scrolling trend chart, the trace does not move —
// a beam sweeps left-to-right and wraps back to x=0, overwriting the previous
// lap as it goes (like an ECG/scope display). No history buffer is kept: the
// screen pixels themselves are the history, so a page revisit starts a fresh sweep.
class ScreenIgnScope : public Screen {
public:
    const char* name() const override { return "Scope"; }
    void init(uint16_t w, uint16_t h) override;
    void update(lgfx::LovyanGFX& gfx, uint16_t w, uint16_t h) override;

private:
    void drawStatic(lgfx::LovyanGFX& gfx, uint16_t w, uint16_t h);
    void drawHeaderValues(lgfx::LovyanGFX& gfx);
    void redrawGridInRange(lgfx::LovyanGFX& gfx, int xFrom, int xTo);
    int  valueToY(float val, float lo, float hi) const;

    char _fmtRpm[10] = "";
    char _fmtAdv[10] = "";

    int _lastX    = -1;  // -1 = no previous point (fresh sweep / just wrapped)
    int _lastRpmY = 0;
    int _lastAdvY = 0;

    uint16_t _plotX0 = 0, _plotY0 = 0, _plotW = 0, _plotH = 0;
    int      _gridX[5] = {0, 0, 0, 0, 0};  // 10/20/30/40/50 s marks

    uint32_t _sweepStartMs = 0;

    static constexpr uint32_t SWEEP_MS = 60000;  // 1-minute full sweep
    static constexpr int      GAP_PX   = 6;       // blank gap ahead of the beam
    static constexpr float    RPM_LO = 0.0f,  RPM_HI = 6000.0f;
    static constexpr float    ADV_LO = -10.0f, ADV_HI = 40.0f;
};
