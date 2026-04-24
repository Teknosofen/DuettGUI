#include <Arduino.h>
#include "display/display.h"
#include "fs/storage.h"
#include "sensors/gps_reader.h"
#include "data/sim.h"
#include "data/logger.h"
#include "net/wifi_log.h"
#include "net/ota.h"
#include "ui/screen_manager.h"
#include "ui/screen_dash.h"
#include "ui/screen_vehicle.h"
#include "ui/screen_gps.h"
#include "ui/screen_storage.h"
#include "ui/screen_cube.h"

static ScreenManager mgr;

static ScreenDash    screenDash;
static ScreenVehicle screenVehicle;
static ScreenGPS     screenGPS;
static ScreenStorage screenStorage;
static ScreenCube    screenCube;

void setup()
{
    // WiFi AP + web console — replaces Serial debug (UART0 is used by GPS).
    wifi_log_init();
    ota_init();
    wlog("=== DuettGUI boot ===");
    wlog("CPU %u MHz  Flash %u KB  Heap %u KB",
         getCpuFrequencyMhz(),
         (unsigned)(ESP.getFlashChipSize() / 1024),
         (unsigned)(ESP.getFreeHeap()      / 1024));

    wlog("[1/4] display_init");
    display_init();
    wlog("[1/4] display OK");

    wlog("[2/4] storage_init");
    storage_init();
    sd_init();
    logger_init();
    wlog("[2/4] storage OK");

    wlog("[3/4] gps_init");
    gps_init();
    wlog("[3/4] GPS OK");

    wlog("[4/4] screen pages");
    mgr.addPage(&screenDash);     // 1 — Dashboard (dials)
    mgr.addPage(&screenVehicle);  // 2 — Engine / fuel table
    mgr.addPage(&screenGPS);      // 3 — GPS
    mgr.addPage(&screenStorage);  // 4 — Storage / logging
    mgr.addPage(&screenCube);     // 5 — Rotating cube demo
    mgr.begin();
    wlog("[4/4] ScreenManager OK");

    wlog("=== boot complete  heap %u KB ===",
         (unsigned)(ESP.getFreeHeap() / 1024));
}

static uint32_t _lastHeartbeat = 0;

void loop()
{
    wifi_log_update();
    ota_update();
    gps_update();
    sim_update();     // overrides vdata when SIM_ENABLE 1; compiles away when 0
    logger_update();
    mgr.update();

    if (millis() - _lastHeartbeat > 30000) {
        _lastHeartbeat = millis();
        wlog("[heartbeat] heap %u KB",
             (unsigned)(ESP.getFreeHeap() / 1024));
    }
}
