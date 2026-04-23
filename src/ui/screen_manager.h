#pragma once
#include "../display/display.h"
#include "screen.h"

// Minimum time between nav-bar page switches (ms).
// Raise if double-tap still occurs; lower for snappier response.
#define NAV_COOLDOWN_MS  250

// Owns all pages, handles touch routing and the fixed navigation bar.
//
// Layout (800 × 480):
//   y 0 … contentH-1  — content area, passed to the active page
//   y contentH … 479  — navigation bar (NAV_H px, always drawn on display)
//
// Nav bar touch zones:
//   x 0  … 159        — previous page
//   x 640 … 799       — next page
class ScreenManager {
public:
    static constexpr int MAX_PAGES = 10;
    static constexpr int NAV_H     = 48;

    // Register a page; call before begin(). Does NOT take ownership.
    bool addPage(Screen* s);

    // Must be called after display_init(). Shows the first page.
    void begin();

    // Call every iteration of loop().
    void update();

private:
    Screen* _pages[MAX_PAGES] = {};
    bool    _inited[MAX_PAGES] = {};
    int     _count   = 0;
    int     _current = 0;

    uint16_t _contentW = 0;
    uint16_t _contentH = 0;

    lgfx::LGFX_Sprite* _sprite       = nullptr;
    bool                _hasSprite   = false;
    bool                _touchActive = false;   // finger currently on screen
    uint32_t            _lastNavMs   = 0;       // millis() of last nav action
    bool                _navDirty    = true;    // nav bar needs a redraw

    void tryAllocSprite();
    void goTo(int index);
    void drawNavBar();
};
