#include <Arduino.h>
#include "display/display.h"
#include "fs/storage.h"
#include "sensors/gps_reader.h"
#include "sensors/ignition_bt.h"
#include "data/sim.h"
#include "data/logger.h"
#include "net/wifi_log.h"
#include "net/ota.h"
#include "ui/screen_manager.h"
#include "ui/screen_dash.h"
#include "ui/screen_ignition.h"
#include "ui/screen_vehicle.h"
#include "ui/screen_gps.h"
#include "ui/screen_storage.h"
#include "ui/screen_settings.h"
#include "ui/screen_cube.h"

static ScreenManager mgr;

static ScreenDash     screenDash;
static ScreenIgnition screenIgnition;
static ScreenVehicle  screenVehicle;
static ScreenGPS      screenGPS;
static ScreenStorage  screenStorage;
static ScreenSettings screenSettings;
static ScreenCube     screenCube;

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

    wlog("[1/5] display_init");
    display_init();
    wlog("[1/5] display OK");

    wlog("[2/5] storage_init");
    storage_init();
    sd_init();
    logger_init();
    wlog("[2/5] storage OK");

    wlog("[3/5] gps_init");
    gps_init();
    wlog("[3/5] GPS OK");

    wlog("[4/5] ignition_bt_init");
    ignition_bt_init();
    wlog("[4/5] BLE OK");

    wlog("[5/5] screen pages");
    mgr.addPage(&screenDash);      // 1 — Dashboard (dials)
    mgr.addPage(&screenIgnition);  // 2 — 123-ignition data + controls
    mgr.addPage(&screenVehicle);   // 3 — Engine / fuel table
    mgr.addPage(&screenGPS);       // 4 — GPS
    mgr.addPage(&screenStorage);   // 5 — Storage / logging
    mgr.addPage(&screenSettings);  // 6 — Settings (sim toggle)
    mgr.addPage(&screenCube);      // 7 — Rotating cube demo
    mgr.begin();
    wlog("[5/5] ScreenManager OK");

    wlog("=== boot complete  heap %u KB ===",
         (unsigned)(ESP.getFreeHeap() / 1024));
}

static uint32_t _lastHeartbeat = 0;

void loop()
{
    wifi_log_update();
    ota_update();
    gps_update();
    ignition_bt_update();
    sim_update();      // writes vdata when sim is ON; skips when OFF or SIM_ENABLE=0
    logger_update();
    mgr.update();

    if (millis() - _lastHeartbeat > 30000) {
        _lastHeartbeat = millis();
        wlog("[heartbeat] heap %u KB  ign=%s  sim=%s",
             (unsigned)(ESP.getFreeHeap() / 1024),
             ignition_bt_state_str(),
             sim_running() ? "ON" : "OFF");
    }
}
