#include "sim.h"

#if SIM_ENABLE

#include "vehicle_data.h"
#include <Arduino.h>
#include <math.h>

static bool _simOn = true;

void sim_set(bool on) { _simOn = on; }
bool sim_running()    { return _simOn; }

// Drive cycle: accelerate 0→100 km/h over ACCEL_MS, hold briefly, then
// decelerate back to 0 over DECEL_MS.  Repeats continuously.
static constexpr uint32_t ACCEL_MS = 5000;
static constexpr uint32_t HOLD_MS  =  500;
static constexpr uint32_t DECEL_MS = 5000;
static constexpr uint32_t CYCLE_MS = ACCEL_MS + HOLD_MS + DECEL_MS;

static constexpr float MAX_SPEED   = 100.f;   // km/h
static constexpr float IDLE_RPM    =  850.f;
static constexpr float MAX_RPM     = 3500.f;   // at MAX_SPEED, top gear

// Smoothstep — ease in/out, feels more like a real dial than linear.
static float smoothstep(float t) { return t * t * (3.f - 2.f * t); }

void sim_update()
{
    if (!_simOn) return;

    uint32_t t   = millis() % CYCLE_MS;
    float    spd = 0.f;
    float    thr = 0.f;

    if (t < ACCEL_MS) {
        float phase = smoothstep((float)t / ACCEL_MS);
        spd = phase * MAX_SPEED;
        thr = 20.f + phase * 65.f;           // throttle builds with speed
    } else if (t < ACCEL_MS + HOLD_MS) {
        spd = MAX_SPEED;
        thr = 30.f;                           // light throttle to hold speed
    } else {
        float phase = smoothstep((float)(t - ACCEL_MS - HOLD_MS) / DECEL_MS);
        spd = (1.f - phase) * MAX_SPEED;
        thr = 0.f;                            // lift off
    }

    vdata.speed_kmh    = spd;
    vdata.rpm          = IDLE_RPM + (spd / MAX_SPEED) * (MAX_RPM - IDLE_RPM);
    vdata.throttle_pct = thr;

    // MAP: rises toward ambient at WOT, drops to vacuum on overrun
    vdata.map_kpa      = (thr > 0.f) ? (45.f + thr * 0.6f) : 32.f;
    vdata.ambient_kpa  = 101.3f;

    // Fuel: higher under load
    vdata.fuel_flow_lph  = (thr > 0.f) ? (1.5f + thr * 0.12f) : 0.8f;
    vdata.fuel_per_100km = (spd > 2.f)
                           ? vdata.fuel_flow_lph / spd * 100.f
                           : 0.f;

    // GPS: fixed position, Stockholm, valid fix
    vdata.gps_valid   = true;
    vdata.lat         =  59.3293;
    vdata.lon         =  18.0686;
    vdata.altitude_m  =  28.f;
    vdata.heading_deg =  45.f;

    uint32_t secs = millis() / 1000;
    snprintf(vdata.timestamp, sizeof(vdata.timestamp), "T+%lus", (unsigned long)secs);
}

#endif // SIM_ENABLE
