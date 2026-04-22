#include <Arduino.h>
#include "display/display.h"
#include "ui/screen_cube.h"
#include "fs/storage.h"

void setup()
{
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 5000) delay(50);

    Serial.println("\n=== DuettGUI boot ===");
    Serial.printf("CPU  : %u MHz\n",  getCpuFrequencyMhz());
    Serial.printf("Flash: %u KB\n",   (unsigned)(ESP.getFlashChipSize() / 1024));
    Serial.printf("PSRAM: %u KB\n",   (unsigned)(ESP.getPsramSize()     / 1024));
    Serial.printf("Heap : %u KB\n",   (unsigned)(ESP.getFreeHeap()      / 1024));
    Serial.flush();

    Serial.println("[1/3] display_init ...");  Serial.flush();
    display_init();
    Serial.println("[1/3] display_init OK");   Serial.flush();

    Serial.println("[2/3] storage_init ...");  Serial.flush();
    storage_init();
    Serial.println("[2/3] storage_init OK");   Serial.flush();

    Serial.println("[3/3] screen_cube_init ..."); Serial.flush();
    screen_cube_init();
    Serial.println("[3/3] screen_cube_init OK");  Serial.flush();

    Serial.println("=== boot complete ===");
}

static uint32_t last_heartbeat = 0;

void loop()
{
    screen_cube_update();

    if (millis() - last_heartbeat > 5000) {
        last_heartbeat = millis();
        Serial.printf("[heartbeat] heap=%u KB  psram=%u KB\n",
            (unsigned)(ESP.getFreeHeap()  / 1024),
            (unsigned)(ESP.getFreePsram() / 1024));
    }
}
