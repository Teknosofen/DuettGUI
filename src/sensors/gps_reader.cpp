#include "gps_reader.h"
#include "../data/vehicle_data.h"
#include <Adafruit_GPS.h>

// GPS uses UART0 (GPIO 43 TX / 44 RX) — the same hardware as the CH340 USB
// bridge.  This is fine in normal operation: the CH340 TX is idle (high) when
// the PC is not sending, so the GPS owns the RX line without conflict.
// Do not flash firmware or send data from the PC while the GPS is connected.
static Adafruit_GPS GPS(&Serial);

// Convert Adafruit NMEA coordinate (DDDMM.MMMM + direction char) to
// signed decimal degrees.
static double toDecDeg(float nmea, char dir)
{
    double deg = (int)(nmea / 100.0);
    double min = nmea - deg * 100.0;
    double dd  = deg + min / 60.0;
    return (dir == 'S' || dir == 'W') ? -dd : dd;
}

void gps_init()
{
    Serial.begin(9600);   // UART0 at GPS baud rate — debug output disabled
    GPS.begin(9600);
    GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA); // RMC + GGA (includes altitude)
    GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);
    GPS.sendCommand(PGCMD_ANTENNA);
    delay(1000);
}

void gps_update()
{
    GPS.read();

    if (!GPS.newNMEAreceived()) return;
    if (!GPS.parse(GPS.lastNMEA())) return;

    vdata.gps_valid = GPS.fix;
    if (!GPS.fix) {
        snprintf(vdata.timestamp, sizeof(vdata.timestamp),
                 "T+%lus", (unsigned long)(millis() / 1000));
        return;
    }

    vdata.lat         = toDecDeg(GPS.latitude,  GPS.lat);
    vdata.lon         = toDecDeg(GPS.longitude, GPS.lon);
    vdata.speed_kmh   = GPS.speed * 1.852f;   // knots → km/h
    vdata.heading_deg = GPS.angle;
    vdata.altitude_m  = GPS.altitude;

    if (GPS.year > 0) {
        snprintf(vdata.timestamp, sizeof(vdata.timestamp),
                 "20%02u-%02u-%02uT%02u:%02u:%02u",
                 GPS.year, GPS.month, GPS.day,
                 GPS.hour, GPS.minute, GPS.seconds);
    } else {
        snprintf(vdata.timestamp, sizeof(vdata.timestamp),
                 "T+%lus", (unsigned long)(millis() / 1000));
    }
}
