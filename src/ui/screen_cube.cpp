#include "screen_cube.h"
#include "../display/display.h"
#include <Arduino.h>
#include <math.h>

// Sprite sized to fit in internal heap (300×300×2 = 175 KB; heap ≈ 348 KB free)
static const int SPRITE_W = 300;
static const int SPRITE_H = 300;
static const int SPRITE_X = (800 - SPRITE_W) / 2;  // centred on 800-wide display
static const int SPRITE_Y = (480 - SPRITE_H) / 2;

static lgfx::LGFX_Sprite* canvas = nullptr;

static const float V[8][3] = {
    {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
    {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
};
static const uint8_t E[12][2] = {
    {0,1},{1,2},{2,3},{3,0},
    {4,5},{5,6},{6,7},{7,4},
    {0,4},{1,5},{2,6},{3,7}
};

static float ang_x = 0.4f, ang_y = 0.0f;
static float pos_x = SPRITE_W / 2.0f;
static float pos_y = SPRITE_H / 2.0f;

static void rotXY(float ix, float iy, float iz, float rx, float ry,
                  float &ox, float &oy, float &oz)
{
    float y1 = iy * cosf(rx) - iz * sinf(rx);
    float z1 = iy * sinf(rx) + iz * cosf(rx);
    ox =  ix * cosf(ry) + z1 * sinf(ry);
    oy =  y1;
    oz = -ix * sinf(ry) + z1 * cosf(ry);
}

static void project(float x, float y, float z, int &px, int &py)
{
    // FOV=180, ZDIST=5 → cube vertices project ≈ ±95 px from centre — fits in 300×300
    const float FOV = 180.0f, ZDIST = 5.0f;
    float s = FOV / (z + ZDIST);
    px = (int)(x * s + pos_x);
    py = (int)(y * s + pos_y);
}

void screen_cube_init()
{
    display->fillScreen(TFT_BLACK);  // clear border area around sprite once

    size_t needed = (size_t)SPRITE_W * SPRITE_H * 2;
    Serial.printf("  sprite needs %u KB, heap free %u KB\n",
        (unsigned)(needed / 1024),
        (unsigned)(ESP.getFreeHeap() / 1024));
    Serial.flush();

    canvas = new lgfx::LGFX_Sprite(display);
    canvas->setColorDepth(16);
    void* ptr = canvas->createSprite(SPRITE_W, SPRITE_H);

    if (ptr) {
        Serial.println("  sprite OK (double-buffered)");
    } else {
        Serial.println("  sprite FAILED — drawing directly (expect flicker)");
        delete canvas;
        canvas = nullptr;
    }
    Serial.flush();
}

void screen_cube_update()
{
    uint32_t t0 = millis();

    int32_t tx, ty;
    if (display->getTouch(&tx, &ty)) {
        // Map display touch coords into sprite-local coords
        pos_x = constrain((float)(tx - SPRITE_X), 20.0f, (float)(SPRITE_W - 20));
        pos_y = constrain((float)(ty - SPRITE_Y), 20.0f, (float)(SPRITE_H - 20));
    }

    ang_x += 0.018f;
    ang_y += 0.026f;

    int   px[8], py[8];
    float pz[8];
    for (int i = 0; i < 8; i++) {
        float ox, oy, oz;
        rotXY(V[i][0], V[i][1], V[i][2], ang_x, ang_y, ox, oy, oz);
        pz[i] = oz;
        project(ox, oy, oz, px[i], py[i]);
    }

    lgfx::LGFXBase* target = canvas ? (lgfx::LGFXBase*)canvas
                                     : (lgfx::LGFXBase*)display;

    target->fillScreen(TFT_BLACK);

    for (int i = 0; i < 12; i++) {
        int a = E[i][0], b = E[i][1];
        float midz = (pz[a] + pz[b]) * 0.5f;
        uint8_t br = (uint8_t)((1.0f - midz) * 87.5f + 80.0f);
        target->drawLine(px[a], py[a], px[b], py[b],
                         target->color888(br, br, br));
    }

    target->setTextColor(target->color888(80, 80, 80), TFT_BLACK);
    target->setTextSize(1);
    target->setCursor(5, 5);
    target->print("Touch: move cube");

    if (canvas) canvas->pushSprite(SPRITE_X, SPRITE_Y);

    int dt = (int)(millis() - t0);
    if (dt < 16) delay(16 - dt);
}
