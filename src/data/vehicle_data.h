#pragma once
#include <Arduino.h>

struct VehicleData {
    // Engine
    float rpm            = 0.0f;   // rev/min
    float throttle_pct   = 0.0f;   // 0–100 %
    float map_kpa        = 101.3f; // manifold absolute pressure
    float ambient_kpa    = 101.3f; // barometric pressure

    // Fuel
    float fuel_flow_lph  = 0.0f;   // litres/hour from flow sensor
    float fuel_per_100km = 0.0f;   // calculated: flow / speed * 100
    float fuel_used_l    = 0.0f;   // trip accumulator

    // GPS (from NMEA)
    float  speed_kmh   = 0.0f;
    float  heading_deg = 0.0f;
    double lat         = 0.0;
    double lon         = 0.0;
    float  altitude_m  = 0.0f;
    bool   gps_valid   = false;
};

extern VehicleData vdata;
