#pragma once

// AP credentials — change WIFI_LOG_SSID / WIFI_LOG_PASS before first flash.
// Leave WIFI_LOG_PASS as nullptr (or "") for an open (no-password) network.
#define WIFI_LOG_SSID  "DuettGUI"
#define WIFI_LOG_PASS  nullptr

// Maximum log lines kept in the ring buffer (oldest are discarded).
#define WIFI_LOG_LINES 100

// Call once in setup() — starts the AP and HTTP server on port 80.
void wifi_log_init();

// Call every loop() — handles pending HTTP clients.
void wifi_log_update();

// Drop-in for Serial.printf.  Thread-safe with respect to the HTTP handler
// (both run on the Arduino loop thread).
void wlog(const char* fmt, ...);
