#pragma once
#include <stdint.h>

// Connection states exported for the ignition screen
enum class IgnBtState : uint8_t {
    IDLE,         // not yet started or waiting before retry
    SCANNING,     // BLE scan active
    CONNECTING,   // device found, establishing link
    HANDSHAKING,  // connected, sending init sequence
    ACTIVE,       // receiving streaming data
    ERROR         // unrecoverable — will retry after RETRY_MS
};

// ── Lifecycle ────────────────────────────────────────────────────────────────
void        ignition_bt_init();       // call once in setup() after wifi_log_init()
void        ignition_bt_update();     // call every loop()

// ── Status ───────────────────────────────────────────────────────────────────
IgnBtState  ignition_bt_state();
const char* ignition_bt_state_str();  // human-readable, e.g. "Connected"

// ── Tune controls — only effective when state == ACTIVE ───────────────────────
void ignition_send_advance_plus();    // +0.2° advance
void ignition_send_advance_minus();   // −0.2° advance
void ignition_send_tune_toggle();     // enter / exit tune mode
