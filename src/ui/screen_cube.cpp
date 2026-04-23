#include "screen_cube.h"
#include "../display/display.h"
#include <Arduino.h>
#include <math.h>

static const float V[8][3] = {
    {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
    {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
};
static const uint8_t E[12][2] = {
    {0,1},{1,2},{2,3},{3,0},
    {4,5},{5,6},{6,7},{7,4},
    {0,4},{1,5},{2,6},{3,7}
};

static void rotXY(float ix, float iy, float iz, float rx, float ry,
                  float& ox, float& oy, float& oz)
{
    float y1 = iy * cosf(rx) - iz * sinf(rx);
    float z1 = iy * sinf(rx) + iz * cosf(rx);
    ox =  ix * cosf(ry) + z1 * sinf(ry);
    oy =  y1;
    oz = -ix * sinf(ry) + z1 * cosf(ry);
}

// ── Screen interface ─────────────────────────────────────────────────────────

ScreenCube::~ScreenCube()
{
    delete _canvas;
}

void ScreenCube::init(uint16_t contentW, uint16_t contentH)
{
    // Centre the sprite inside the content area
    _spriteX = (contentW  - SPRITE_W) / 2;
    _spriteY = (contentH - SPRITE_H) / 2;

    size_t bytes = (size_t)SPRITE_W * SPRITE_H * 2;
    Serial.printf("[cube] sprite %d×%d = %u KB, heap free %u KB\n",
        SPRITE_W, SPRITE_H, (unsigned)(bytes / 1024),
        (unsigned)(ESP.getFreeHeap() / 1024));

    _canvas = new lgfx::LGFX_Sprite(display);
    _canvas->setColorDepth(16);
    void* ptr = _canvas->createSprite(SPRITE_W, SPRITE_H);

    if (ptr) {
        Serial.println("[cube] sprite OK");
    } else {
        Serial.println("[cube] sprite FAILED — direct draw");
        delete _canvas;
        _canvas = nullptr;
    }
}

void ScreenCube::update(lgfx::LovyanGFX& gfx, uint16_t contentW, uint16_t contentH)
{
    _needsRedraw = false;  // cube redraws itself every frame; no full-redraw signal needed
    uint32_t t0 = millis();

    _angX += 0.018f;
    _angY += 0.026f;

    // Project all 8 vertices
    int   px[8], py[8];
    float pz[8];
    for (int i = 0; i < 8; i++) {
        float ox, oy, oz;
        rotXY(V[i][0], V[i][1], V[i][2], _angX, _angY, ox, oy, oz);
        pz[i] = oz;

        // FOV=180, ZDIST=5 → vertices project ≈ ±95 px from centre
        const float FOV = 180.0f, ZDIST = 5.0f;
        float s = FOV / (oz + ZDIST);
        px[i] = (int)(ox * s + _posX);
        py[i] = (int)(oy * s + _posY);
    }

    // Choose render target: own sprite (pushed to gfx) or gfx directly
    lgfx::LovyanGFX* target = _canvas
        ? static_cast<lgfx::LovyanGFX*>(_canvas)
        : &gfx;

    target->fillRect(0, 0,
        _canvas ? SPRITE_W : contentW,
        _canvas ? SPRITE_H : contentH, TFT_BLACK);

    for (int i = 0; i < 12; i++) {
        int a = E[i][0], b = E[i][1];
        float midZ = (pz[a] + pz[b]) * 0.5f;
        uint8_t br = (uint8_t)((1.0f - midZ) * 87.5f + 80.0f);
        target->drawLine(px[a], py[a], px[b], py[b],
                         target->color888(br, br, br));
    }

    target->setTextColor(target->color888(80, 80, 80), TFT_BLACK);
    target->setTextSize(1);
    target->setCursor(5, 5);
    target->print("Touch: move cube");

    if (_canvas)
        _canvas->pushSprite(_spriteX, _spriteY);  // pushes to parent (display)

    int dt = (int)(millis() - t0);
    if (dt < 16) delay(16 - dt);
}

void ScreenCube::onTouch(uint16_t x, uint16_t y)
{
    // Map content-area touch coords into sprite-local coords
    _posX = constrain((float)x - _spriteX, 20.0f, (float)(SPRITE_W - 20));
    _posY = constrain((float)y - _spriteY, 20.0f, (float)(SPRITE_H - 20));
}
