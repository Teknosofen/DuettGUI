#pragma once

// ── Sensor simulation ────────────────────────────────────────────────────────
// Set SIM_ENABLE to 1 to animate vdata with a synthetic drive cycle.
// Set to 0 to use real sensor data — sim_update() compiles away entirely.
#define SIM_ENABLE 1

#if SIM_ENABLE
void sim_update();
#else
inline void sim_update() {}
#endif
