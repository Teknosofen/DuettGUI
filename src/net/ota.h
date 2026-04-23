#pragma once

// OTA hostname (shows up in PlatformIO's device list and mDNS).
#define OTA_HOSTNAME "DuettGUI"

// Password used by ArduinoOTA — must match upload_flags = --auth=<password>
// in the [env:ota] section of platformio.ini.
#define OTA_PASSWORD "duett1964"

// Call once in setup(), after wifi_log_init().
void ota_init();

// Call every loop() — handles the OTA negotiation and data transfer.
void ota_update();
