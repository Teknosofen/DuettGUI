#include "storage.h"
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

// SD card SPI pins — Elecrow CrowPanel 5.0" (DIS07050H)
static constexpr int SD_PIN_CS   = 10;
static constexpr int SD_PIN_MOSI = 11;
static constexpr int SD_PIN_SCK  = 12;
static constexpr int SD_PIN_MISO = 13;

static SPIClass _sdSPI(FSPI);
static bool     _sdMounted = false;

// ── LittleFS ─────────────────────────────────────────────────────────────────

bool storage_init()
{
    if (!LittleFS.begin(true)) {
        Serial.println("[fs] LittleFS mount failed");
        return false;
    }
    storage_info();
    return true;
}

bool storage_exists(const char* path)  { return LittleFS.exists(path); }

String storage_read(const char* path)
{
    File f = LittleFS.open(path, "r");
    if (!f) return String();
    String s = f.readString();
    f.close();
    return s;
}

bool storage_write(const char* path, const String& data)
{
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.print(data);
    f.close();
    return true;
}

bool storage_remove(const char* path) { return LittleFS.remove(path); }

void storage_info()
{
    Serial.printf("[fs] LittleFS  %u KB used / %u KB total\n",
        (unsigned)(LittleFS.usedBytes()  / 1024),
        (unsigned)(LittleFS.totalBytes() / 1024));
}

// ── SD card ──────────────────────────────────────────────────────────────────

bool sd_init()
{
    _sdSPI.begin(SD_PIN_SCK, SD_PIN_MISO, SD_PIN_MOSI, SD_PIN_CS);
    if (!SD.begin(SD_PIN_CS, _sdSPI)) {
        Serial.println("[sd] no card or init failed");
        _sdMounted = false;
        return false;
    }
    _sdMounted = true;
    sd_info();
    return true;
}

bool sd_available() { return _sdMounted; }

bool sd_exists(const char* path)
{
    if (!_sdMounted) return false;
    return SD.exists(path);
}

String sd_read(const char* path)
{
    if (!_sdMounted) return String();
    File f = SD.open(path, FILE_READ);
    if (!f) return String();
    String s = f.readString();
    f.close();
    return s;
}

bool sd_write(const char* path, const String& data)
{
    if (!_sdMounted) return false;
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    f.print(data);
    f.close();
    return true;
}

bool sd_remove(const char* path)
{
    if (!_sdMounted) return false;
    return SD.remove(path);
}

void sd_info()
{
    if (!_sdMounted) { Serial.println("[sd] not mounted"); return; }
    uint64_t total = SD.totalBytes();
    uint64_t used  = SD.usedBytes();
    Serial.printf("[sd] %llu MB used / %llu MB total  (type: %s)\n",
        used  / (1024 * 1024),
        total / (1024 * 1024),
        SD.cardType() == CARD_SDHC ? "SDHC" : "SD");
}
