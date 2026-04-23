#include <Arduino.h>
#include "display/display.h"
#include "fs/storage.h"
#include "ui/screen_manager.h"
#include "ui/screen_cube.h"
#include "ui/screen_vehicle.h"
#include "ui/screen_gps.h"

static ScreenManager mgr;

static ScreenCube    screenCube;
static ScreenVehicle screenVehicle;
static ScreenGPS     screenGPS;

void setup()
{
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n=== DuettGUI boot ===");
    Serial.printf("CPU  : %u MHz\n",  getCpuFrequencyMhz());
    Serial.printf("Flash: %u KB\n",   (unsigned)(ESP.getFlashChipSize() / 1024));
    Serial.printf("PSRAM: %u KB\n",   (unsigned)(ESP.getPsramSize()     / 1024));
    Serial.printf("Heap : %u KB\n",   (unsigned)(ESP.getFreeHeap()      / 1024));
    Serial.flush();

    Serial.println("[1/4] display_init ...");  Serial.flush();
    display_init();
    Serial.println("[1/4] display_init OK");   Serial.flush();

    Serial.println("[2/4] storage_init ...");  Serial.flush();
    storage_init();
    sd_init();                                 // SD card (optional — boots without it)
    Serial.println("[2/4] storage OK");        Serial.flush();

    Serial.println("[3/4] screen pages ...");  Serial.flush();
    mgr.addPage(&screenCube);
    mgr.addPage(&screenVehicle);
    mgr.addPage(&screenGPS);
    Serial.println("[3/4] pages registered"); Serial.flush();

    Serial.println("[4/4] ScreenManager::begin ...");  Serial.flush();
    mgr.begin();
    Serial.println("[4/4] ScreenManager OK");          Serial.flush();

    Serial.println("=== boot complete ===");
}

static uint32_t _lastHeartbeat = 0;

void loop()
{
    mgr.update();

    if (millis() - _lastHeartbeat > 5000) {
        _lastHeartbeat = millis();
        Serial.printf("[heartbeat] heap=%u KB  psram=%u KB\n",
            (unsigned)(ESP.getFreeHeap()  / 1024),
            (unsigned)(ESP.getFreePsram() / 1024));
    }
}
