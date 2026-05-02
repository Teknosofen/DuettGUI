#pragma once

// ── Sensor simulation ────────────────────────────────────────────────────────
// Set SIM_ENABLE to 1 to compile the synthetic drive cycle.
// Set to 0 to use real sensor data — all sim functions compile away entirely.
#define SIM_ENABLE 1

#if SIM_ENABLE
void sim_update();          // call every loop(); overwrites vdata
void sim_set(bool on);      // runtime enable / disable
bool sim_running();         // true while simulation is active
#else
inline void sim_update()    {}
inline void sim_set(bool)   {}
inline bool sim_running()   { return false; }
#endif
