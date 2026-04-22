#include <Arduino.h>
#include "lgfx_user.h"
#include <math.h>

static LGFX              display;
static lgfx::LGFX_Sprite canvas(&display);

// Unit cube: 8 vertices
static const float V[8][3] = {
    {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
    {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
};
// 12 edges (back face, front face, connectors)
static const uint8_t E[12][2] = {
    {0,1},{1,2},{2,3},{3,0},
    {4,5},{5,6},{6,7},{7,4},
    {0,4},{1,5},{2,6},{3,7}
};

static float ang_x = 0.4f, ang_y = 0.0f;
static float pos_x = 400.0f, pos_y = 240.0f;

// Rotate around X then Y
static void rotXY(float ix, float iy, float iz, float rx, float ry,
                  float &ox, float &oy, float &oz)
{
    float y1 = iy * cosf(rx) - iz * sinf(rx);
    float z1 = iy * sinf(rx) + iz * cosf(rx);
    ox =  ix * cosf(ry) + z1 * sinf(ry);
    oy =  y1;
    oz = -ix * sinf(ry) + z1 * cosf(ry);
}

// Perspective projection — camera at z = -ZDIST
static void project(float x, float y, float z, int &px, int &py)
{
    const float FOV = 260.0f, ZDIST = 3.5f;
    float s = FOV / (z + ZDIST);
    px = (int)(x * s + pos_x);
    py = (int)(y * s + pos_y);
}

void setup()
{
    Serial.begin(115200);
    display.init();
    display.setRotation(0);
    display.setBrightness(200);

    canvas.setColorDepth(16);
    canvas.createSprite(display.width(), display.height());
}

void loop()
{
    uint32_t t0 = millis();

    // Touch → reposition cube
    int32_t tx, ty;
    if (display.getTouch(&tx, &ty)) {
        pos_x = (float)tx;
        pos_y = (float)ty;
    }

    ang_x += 0.018f;
    ang_y += 0.026f;

    // Transform + project all 8 vertices
    int   px[8], py[8];
    float pz[8];
    for (int i = 0; i < 8; i++) {
        float ox, oy, oz;
        rotXY(V[i][0], V[i][1], V[i][2], ang_x, ang_y, ox, oy, oz);
        pz[i] = oz;
        project(ox, oy, oz, px[i], py[i]);
    }

    canvas.fillScreen(TFT_BLACK);

    // Draw edges with depth shading: front (low z) = bright, back = dim
    for (int i = 0; i < 12; i++) {
        int a = E[i][0], b = E[i][1];
        float midz = (pz[a] + pz[b]) * 0.5f;          // ∈ [-1, 1]
        uint8_t br = (uint8_t)((1.0f - midz) * 87.5f + 80.0f); // [80..255]
        canvas.drawLine(px[a], py[a], px[b], py[b],
                        canvas.color888(br, br, br));
    }

    canvas.setTextColor(canvas.color888(80, 80, 80), TFT_BLACK);
    canvas.setTextSize(1);
    canvas.setCursor(5, 5);
    canvas.print("Touch: move cube");

    canvas.pushSprite(0, 0);

    int dt = (int)(millis() - t0);
    if (dt < 16) delay(16 - dt);
}
