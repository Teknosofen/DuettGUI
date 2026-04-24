#include "screen_manager.h"
#include "../net/wifi_log.h"
#include <Arduino.h>

// Navigation bar palette
static constexpr uint8_t NAV_BG_R  = 0x1A, NAV_BG_G  = 0x1A, NAV_BG_B  = 0x2E;
static constexpr uint8_t NAV_SEP_R = 0x40, NAV_SEP_G = 0x40, NAV_SEP_B = 0x60;
static constexpr uint8_t ACT_R     = 0x00, ACT_G     = 0xBF, ACT_B     = 0xFF; // active arrow
static constexpr uint8_t DIM_R     = 0x40, DIM_G     = 0x40, DIM_B     = 0x40; // inactive arrow

// ── public ──────────────────────────────────────────────────────────────────

bool ScreenManager::addPage(Screen* s)
{
    if (!s || _count >= MAX_PAGES) return false;
    _pages[_count++] = s;
    return true;
}

void ScreenManager::begin()
{
    _contentW = (uint16_t)display->width();
    _contentH = (uint16_t)(display->height() - NAV_H);

    tryAllocSprite();

    display->fillScreen(TFT_BLACK);
    goTo(0);
}

void ScreenManager::update()
{
    uint32_t now = millis();

    // ── Touch — runs every loop() iteration for crisp response ─────────────
    int32_t tx, ty;
    bool touching = display->getTouch(&tx, &ty);
    bool risingEdge = touching && !_touchActive;
    _touchActive = touching;

    if (touching) {
        if (ty < (int32_t)_contentH) {
            // Content area: fire every frame so drag (e.g. cube) stays smooth
            _pages[_current]->onTouch((uint16_t)tx, (uint16_t)ty);
        } else if (risingEdge && (now - _lastNavMs) >= NAV_COOLDOWN_MS) {
            // Nav bar: rising edge AND cooldown elapsed since last page switch
            if (tx < 160 && _current > 0) {
                _lastNavMs = now;
                goTo(_current - 1);
            } else if (tx >= 640 && _current < _count - 1) {
                _lastNavMs = now;
                goTo(_current + 1);
            }
        }
    }

    // ── Render — throttled to RENDER_INTERVAL_MS ────────────────────────────
    if (now - _lastRenderMs < RENDER_INTERVAL_MS) return;
    _lastRenderMs = now;
    lgfx::LovyanGFX& target = _hasSprite
        ? static_cast<lgfx::LovyanGFX&>(*_sprite)
        : static_cast<lgfx::LovyanGFX&>(*display);

    // Check before update() so we know if a full-screen clear is about to happen
    bool pageFullRedraw = _pages[_current]->needsFullRedraw();

    _pages[_current]->update(target, _contentW, _contentH);

    if (_hasSprite)
        _sprite->pushSprite(0, 0);

    // Nav bar is drawn only when something changed — page switch or first render.
    // In direct-draw mode a full-page clear would wipe it, so catch that too.
    if (_navDirty || pageFullRedraw) {
        drawNavBar();
        _navDirty = false;
    }
}

// ── private ─────────────────────────────────────────────────────────────────

void ScreenManager::tryAllocSprite()
{
    size_t bytes = (size_t)_contentW * _contentH * 2;

    _sprite = new lgfx::LGFX_Sprite(display);
    _sprite->setColorDepth(16);

    void* ptr = _sprite->createSprite(_contentW, _contentH);

    if (ptr) {
        _hasSprite = true;
        wlog("[mgr] content sprite OK  %u x %u  (%u KB)",
             _contentW, _contentH, (unsigned)(bytes / 1024));
    } else {
        wlog("[mgr] content sprite FAILED — pages draw directly");
        delete _sprite;
        _sprite    = nullptr;
        _hasSprite = false;
    }
}

void ScreenManager::goTo(int index)
{
    if (index < 0 || index >= _count) return;
    _current = index;
    _touchActive = false;  // prevent the nav-tap bleeding into the new page
    wlog("page → %s (%d/%d)", _pages[index]->name(), index + 1, _count);

    if (!_inited[index]) {
        _pages[index]->init(_contentW, _contentH);
        _inited[index] = true;
    }

    _pages[index]->setNeedsRedraw();  // page will do a full redraw on next update()
    _navDirty = true;                 // nav bar redrawn in next update()

    // Clear the content area so no old content shows through on partial-redraw pages
    lgfx::LovyanGFX& target = _hasSprite
        ? static_cast<lgfx::LovyanGFX&>(*_sprite)
        : static_cast<lgfx::LovyanGFX&>(*display);
    target.fillRect(0, 0, _contentW, _contentH, TFT_BLACK);
    if (_hasSprite) _sprite->pushSprite(0, 0);
}

void ScreenManager::drawNavBar()
{
    const int y0 = (int)_contentH;
    const int W  = display->width();

    // Background
    display->fillRect(0, y0, W, NAV_H,
        display->color888(NAV_BG_R, NAV_BG_G, NAV_BG_B));

    // Top separator line
    display->drawFastHLine(0, y0, W,
        display->color888(NAV_SEP_R, NAV_SEP_G, NAV_SEP_B));

    // Left arrow ( ◀ )
    {
        bool active = (_current > 0);
        uint32_t col = display->color888(
            active ? ACT_R : DIM_R,
            active ? ACT_G : DIM_G,
            active ? ACT_B : DIM_B);
        int cx = 60, cy = y0 + NAV_H / 2;
        display->fillTriangle(cx - 16, cy,
                              cx + 10, cy - 13,
                              cx + 10, cy + 13, col);
    }

    // Right arrow ( ▶ )
    {
        bool active = (_current < _count - 1);
        uint32_t col = display->color888(
            active ? ACT_R : DIM_R,
            active ? ACT_G : DIM_G,
            active ? ACT_B : DIM_B);
        int cx = W - 60, cy = y0 + NAV_H / 2;
        display->fillTriangle(cx + 16, cy,
                              cx - 10, cy - 13,
                              cx - 10, cy + 13, col);
    }

    // Page name  +  "n / N"  centred
    char label[48];
    snprintf(label, sizeof(label), "%s   %d / %d",
             _pages[_current]->name(), _current + 1, _count);

    display->setFont(&lgfx::fonts::DejaVu18);
    display->setTextSize(1);
    display->setTextColor(
        display->color888(0xE0, 0xE0, 0xE0),
        display->color888(NAV_BG_R, NAV_BG_G, NAV_BG_B));

    int tw = (int)display->textWidth(label);
    int th = (int)display->fontHeight();
    display->setCursor((W - tw) / 2, y0 + (NAV_H - th) / 2);
    display->print(label);
}
