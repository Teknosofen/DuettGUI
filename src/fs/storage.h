#pragma once
#include <Arduino.h>

// ── LittleFS (internal flash, ~1.9 MB) ──────────────────────────────────────
bool   storage_init();
bool   storage_exists(const char* path);
String storage_read(const char* path);
bool   storage_write(const char* path, const String& data);
bool   storage_remove(const char* path);
void   storage_info();

// ── SD card (SPI: CS=10  MOSI=11  SCK=12  MISO=13) ─────────────────────────
bool   sd_init();
bool   sd_available();          // true if card mounted and readable
bool   sd_exists(const char* path);
String sd_read(const char* path);
bool   sd_write(const char* path, const String& data);
bool     sd_remove(const char* path);
void     sd_info();
uint64_t sd_total_bytes();
uint64_t sd_used_bytes();
