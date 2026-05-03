#include "display.h"
#include <Arduino.h>

LGFX* display = nullptr;

void display_init()
{
    Serial.println("  new LGFX ...");  Serial.flush();
    display = new LGFX();

    Serial.println("  display->init() ...");  Serial.flush();
    display->init();
    Serial.println("  display->init() returned");  Serial.flush();

    display->setRotation(0);
    display->setBrightness(200);
    Serial.printf("  resolution: %d x %d\n", display->width(), display->height());
    Serial.flush();

    // Visual self-test: RED → GREEN → BLUE → BLACK
    // display->fillScreen(TFT_RED);   delay(400);
    // display->fillScreen(TFT_GREEN); delay(400);
    // display->fillScreen(TFT_BLUE);  delay(400);
    // display->fillScreen(TFT_BLACK);
    // Serial.println("  colour test done");  Serial.flush();
}
