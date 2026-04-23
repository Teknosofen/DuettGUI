#pragma once
#include <LovyanGFX.hpp>

// Abstract base class for all display pages.
//
// Lifecycle:
//   init()         — called once on first activation (allocate sprites here)
//   setNeedsRedraw()— called by ScreenManager on every page activation
//   update()       — called every frame; check _needsRedraw to decide full vs partial render
//   onTouch()      — content-area touch, coords relative to top-left of content area
class Screen {
public:
    virtual ~Screen() = default;

    virtual void        init(uint16_t contentW, uint16_t contentH) {}
    virtual void        update(lgfx::LovyanGFX& gfx,
                               uint16_t contentW, uint16_t contentH) = 0;
    virtual void        onTouch(uint16_t x, uint16_t y) {}
    virtual const char* name() const = 0;

    void setNeedsRedraw()     { _needsRedraw = true; }
    bool needsFullRedraw() const { return _needsRedraw; }

protected:
    bool _needsRedraw = true;  // true until the first update() clears it
};
