#pragma once

// ── Wiring ───────────────────────────────────────────────────────────────────
//
// GPS connects to the UART0/UART1 HY2.0-4P connector on the board edge.
// That connector uses GPIO 43 (TX) / GPIO 44 (RX) — the same pins as the
// CH340 USB-UART bridge.  Serial debug output is therefore disabled; UART0
// is initialised at 9600 baud exclusively for the GPS.
//
// Connector pinout (board silkscreen): 3V3 · GND · RX · TX
//   Board RX (GPIO 44) ← GPS module TX
//   Board TX (GPIO 43) → GPS module RX
//
// Do NOT connect the GPS and flash firmware at the same time — their signals
// collide on GPIO 44.

// Call once in setup() — initialises UART0 at 9600 baud for the GPS.
// Must be called before Serial.print() is used anywhere, since UART0 is
// handed to the GPS and debug output is suppressed.
void gps_init();

// Call every loop() iteration — reads one byte from the GPS and, when a
// complete NMEA sentence is parsed, updates vdata.
void gps_update();
