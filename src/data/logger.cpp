#include "logger.h"
#include "vehicle_data.h"
#include "../fs/storage.h"
#include "../net/wifi_log.h"
#include <SD.h>
#include <Arduino.h>
#include <stdio.h>

static File     _logFile;
static bool     _enabled       = false;
static uint16_t _sequence      = 0;
static uint32_t _records       = 0;
static char     _filename[20]  = "";
static uint32_t _lastWriteMs   = 0;

void logger_init()
{
    if (!sd_available()) {
        wlog("[logger] SD not available — logging disabled");
        return;
    }

    // Read sequence counter from LittleFS
    String seqStr = storage_read("/logseq.txt");
    uint16_t seq = 1;
    if (seqStr.length() > 0) {
        seq = (uint16_t)seqStr.toInt();
        if (seq == 0) seq = 1;
    }

    // Persist incremented sequence number
    storage_write("/logseq.txt", String(seq + 1));

    // Build filename
    snprintf(_filename, sizeof(_filename), "/LOG_%04u.CSV", seq);

    // Open log file on SD
    _logFile = SD.open(_filename, FILE_WRITE);
    if (!_logFile) {
        wlog("[logger] failed to open %s", _filename);
        return;
    }

    // Write CSV header
    _logFile.println(
        "seq,timestamp,speed_kmh,rpm,throttle_pct,map_kpa,ambient_kpa,"
        "fuel_flow_lph,fuel_per_100km,fuel_used_l,gps_valid,lat,lon,"
        "altitude_m,heading_deg");
    _logFile.flush();

    _enabled   = true;
    _sequence  = seq;
    _records   = 0;
    wlog("[logger] logging to %s (seq %u)", _filename, seq);
}

void logger_update()
{
    if (!_enabled || !_logFile) return;
    if (millis() - _lastWriteMs < LOG_INTERVAL_MS) return;
    _lastWriteMs = millis();

    const char* ts = (vdata.timestamp[0] != '\0') ? vdata.timestamp : "T+?";

    char buf[160];
    snprintf(buf, sizeof(buf),
             "%lu,%s,%.1f,%.0f,%.1f,%.1f,%.1f,%.2f,%.1f,%.3f,%d,%.6lf,%.6lf,%.1f,%.1f",
             (unsigned long)(_records + 1),
             ts,
             vdata.speed_kmh,
             vdata.rpm,
             vdata.throttle_pct,
             vdata.map_kpa,
             vdata.ambient_kpa,
             vdata.fuel_flow_lph,
             vdata.fuel_per_100km,
             vdata.fuel_used_l,
             (int)vdata.gps_valid,
             vdata.lat,
             vdata.lon,
             vdata.altitude_m,
             vdata.heading_deg);

    _logFile.println(buf);
    _records++;

    if (_records % 10 == 0) _logFile.flush();
}

void logger_enable(bool on)
{
    if (!on && _logFile) _logFile.flush();
    _enabled = on;
}

bool        logger_enabled()    { return _enabled; }
uint16_t    logger_sequence()   { return _sequence; }
uint32_t    logger_records()    { return _records; }
const char* logger_filename()   { return _filename; }
uint64_t    logger_file_bytes() { return _logFile ? (uint64_t)_logFile.size() : 0; }
