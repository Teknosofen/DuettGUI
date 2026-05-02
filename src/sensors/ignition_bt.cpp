#include "ignition_bt.h"
#include "../data/vehicle_data.h"
#include "../data/sim.h"
#include "../net/wifi_log.h"
#include <NimBLEDevice.h>
#include <Arduino.h>

// ── UUIDs (from reverse-engineering of 123ignition TUNE+ firmware) ────────────
static const char* SVC_UUID = "da2b84f1-6279-48de-bdc0-afbea0226079";
static const char* RX_UUID  = "BF03260C-7205-4C25-AF43-93B1C299D159";  // write commands
static const char* TX_UUID  = "18CDA784-4BD3-4370-85BB-BFED91EC86AF";  // notify data

// ── Handshake commands ────────────────────────────────────────────────────────
// Three commands sent in sequence after connecting (300 ms apart).
// The device starts streaming RPM/Advance/Temp/Voltage notifications after step 3.
static const uint8_t CMD_KEEPALIVE[]     = { 0x0D };
static const uint8_t CMD_ADV_CURVE[]     = { 0x31, 0x30, 0x40, 0x0D }; // "10@\r"
static const uint8_t CMD_CONFIG[]        = { 0x31, 0x31, 0x40, 0x0D }; // "11@\r"

// ── Realtime control commands ─────────────────────────────────────────────────
static const uint8_t CMD_ADV_PLUS[]      = { 0x61 }; // 'a' — increase advance in tune mode
static const uint8_t CMD_ADV_MINUS[]     = { 0x72 }; // 'r' — decrease advance in tune mode
static const uint8_t CMD_TUNE_TOGGLE[]   = { 0x74 }; // 't' — toggle tune mode on/off

// ── State ─────────────────────────────────────────────────────────────────────
static IgnBtState               _state     = IgnBtState::IDLE;
static NimBLEClient*            _client    = nullptr;
static NimBLEAddress            _devAddr;
static bool                     _addrFound = false;
static NimBLERemoteCharacteristic* _rxChar = nullptr;
static NimBLERemoteCharacteristic* _txChar = nullptr;
static uint8_t                  _hsStep    = 0;
static uint32_t                 _hsMs      = 0;
static uint32_t                 _retryMs   = 0;
static constexpr uint32_t       RETRY_MS   = 8000;
static constexpr uint32_t       HS_GAP_MS  = 300;  // delay between handshake steps

// ── Packet stream buffer ──────────────────────────────────────────────────────
// Protocol: 5-byte packets  [cmd][MSB][LSB][csum][0x20|0x0D]
//   cmd  0x30 = RPM        MSB/LSB are single ASCII hex nibbles ('0'-'F')
//        0x31 = Advance       RPM formula:  (nibble(MSB)*800) + (nibble(LSB)*50)
//        0x32 = Pressure      Adv formula:  (nibble(MSB)*3.2) + (nibble(LSB)*0.2)
//        0x33 = Temperature   For Temp/Volt/Pres/Amp: treat MSB+LSB as 2-char hex string
//        0x34 = Tune mode flag
//        0x35 = Ampere
//        0x41 = Voltage
//   0x24 padding bytes are silently discarded.
static uint8_t _buf[80];
static uint8_t _bufLen = 0;

static uint8_t hexNibble(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

static uint8_t hexByte(uint8_t hi, uint8_t lo) {
    return (hexNibble(hi) << 4) | hexNibble(lo);
}

static void dispatchPacket(const uint8_t* p) {
    uint8_t cmd = p[0];
    uint8_t msb = p[1];
    uint8_t lsb = p[2];

    switch (cmd) {
        case 0x30: // RPM — each nibble weighted separately
            if (!sim_running())
                vdata.rpm = hexNibble(msb) * 800.0f + hexNibble(lsb) * 50.0f;
            break;
        case 0x31: // Ignition advance (° BTDC)
            vdata.ign_advance_deg = hexNibble(msb) * 3.2f + hexNibble(lsb) * 0.2f;
            break;
        case 0x32: // MAP / vacuum pressure (kPa absolute)
            vdata.ign_pressure_kpa = (float)hexByte(msb, lsb);
            break;
        case 0x33: // Temperature (°C), offset −30
            vdata.ign_temp_c = (float)hexByte(msb, lsb) - 30.0f;
            break;
        case 0x34: // Tune mode flag (lsb == '1' when active)
            vdata.ign_tune_mode = (lsb == '1');
            break;
        case 0x35: // Coil current (A), scale 16/1.85
            vdata.ign_ampere = hexByte(msb, lsb) / (16.0f / 1.85f);
            break;
        case 0x41: // Supply voltage (V), scale 0x40/14.1
            vdata.ign_voltage_v = hexByte(msb, lsb) / (0x40 / 14.1f);
            break;
        default:
            break; // 0x42 shift-light and unknown commands silently ignored
    }
}

static void parseStream(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (data[i] == 0x24) continue;  // skip padding
        if (_bufLen < sizeof(_buf))
            _buf[_bufLen++] = data[i];
    }
    // Extract complete 5-byte packets; resync on bad terminator
    while (_bufLen >= 5) {
        if (_buf[4] == 0x20 || _buf[4] == 0x0D) {
            dispatchPacket(_buf);
            _bufLen -= 5;
            memmove(_buf, _buf + 5, _bufLen);
        } else {
            // not aligned — drop one byte and try again
            _bufLen--;
            memmove(_buf, _buf + 1, _bufLen);
        }
    }
}

// ── BLE callbacks ─────────────────────────────────────────────────────────────

static void notifyCB(NimBLERemoteCharacteristic* /*pChar*/,
                     uint8_t* pData, size_t length, bool /*isNotify*/) {
    parseStream(pData, length);
}

class ClientCB : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient*) override {
        wlog("[ign] BLE connected — starting handshake");
        _state  = IgnBtState::HANDSHAKING;
        _hsStep = 0;
        _hsMs   = 0;
    }
    void onDisconnect(NimBLEClient*, int reason) override {
        wlog("[ign] BLE disconnected (reason %d) — will retry in %u s",
             reason, (unsigned)(RETRY_MS / 1000));
        vdata.ign_connected = false;
        _rxChar  = nullptr;
        _txChar  = nullptr;
        _bufLen  = 0;
        _state   = IgnBtState::IDLE;
        _retryMs = millis();
    }
};
static ClientCB _clientCB;

class ScanCB : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* dev) override {
        bool matchSvc  = dev->haveServiceUUID() &&
                         dev->isAdvertisingService(NimBLEUUID(SVC_UUID));
        bool matchName = dev->haveName() &&
                         strstr(dev->getName().c_str(), "123") != nullptr;
        if (matchSvc || matchName) {
            wlog("[ign] found: %s  addr=%s",
                 dev->getName().c_str(), dev->getAddress().toString().c_str());
            NimBLEDevice::getScan()->stop();
            _devAddr   = dev->getAddress();
            _addrFound = true;
            _state     = IgnBtState::CONNECTING;
        }
    }
};
static ScanCB _scanCB;

// ── State machine helpers ─────────────────────────────────────────────────────

static void startScan() {
    wlog("[ign] BLE scan started");
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&_scanCB, false);
    scan->setInterval(100);
    scan->setWindow(50);
    scan->setActiveScan(true);
    scan->start(0, false);   // continuous, non-blocking
    _state = IgnBtState::SCANNING;
}

// Runs in a short-lived FreeRTOS task so loop() stays responsive during connect
static void connectTask(void*) {
    if (!_client) {
        _client = NimBLEDevice::createClient();
        _client->setClientCallbacks(&_clientCB, false);
        _client->setConnectionParams(12, 12, 0, 51);  // fast connection params
    }
    wlog("[ign] connecting to %s ...", _devAddr.toString().c_str());
    if (!_client->connect(_devAddr)) {
        wlog("[ign] connect failed");
        NimBLEDevice::deleteClient(_client);
        _client  = nullptr;
        _state   = IgnBtState::IDLE;
        _retryMs = millis();
    }
    // On success, ClientCB::onConnect() sets state = HANDSHAKING
    vTaskDelete(nullptr);
}

static bool setupChars() {
    NimBLERemoteService* svc = _client->getService(SVC_UUID);
    if (!svc) { wlog("[ign] service not found"); return false; }
    _rxChar = svc->getCharacteristic(RX_UUID);
    _txChar = svc->getCharacteristic(TX_UUID);
    if (!_rxChar || !_txChar) { wlog("[ign] characteristics not found"); return false; }
    return true;
}

static void doHandshake() {
    uint32_t now = millis();
    if (now - _hsMs < HS_GAP_MS) return;
    _hsMs = now;

    if (_hsStep == 0) {
        if (!setupChars()) { _client->disconnect(); return; }
        _rxChar->writeValue(CMD_KEEPALIVE, sizeof(CMD_KEEPALIVE), false);
        wlog("[ign] hs 1/3 keepalive");
        _hsStep = 1;
    } else if (_hsStep == 1) {
        _rxChar->writeValue(CMD_ADV_CURVE, sizeof(CMD_ADV_CURVE), false);
        wlog("[ign] hs 2/3 advance curve");
        _hsStep = 2;
    } else {
        _rxChar->writeValue(CMD_CONFIG, sizeof(CMD_CONFIG), false);
        wlog("[ign] hs 3/3 config");
        if (!_txChar->subscribe(true, notifyCB)) {
            wlog("[ign] subscribe failed");
            _client->disconnect();
            return;
        }
        vdata.ign_connected = true;
        _state = IgnBtState::ACTIVE;
        wlog("[ign] ACTIVE — streaming data");
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void ignition_bt_init() {
    NimBLEDevice::init("DuettGUI");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    wlog("[ign] BLE stack ready");
    _state = IgnBtState::IDLE;
}

void ignition_bt_update() {
    switch (_state) {
        case IgnBtState::IDLE:
            if (_retryMs == 0 || millis() - _retryMs >= RETRY_MS) {
                _addrFound = false;
                startScan();
            }
            break;

        case IgnBtState::SCANNING:
            break; // ScanCB::onResult drives transition

        case IgnBtState::CONNECTING:
            if (_addrFound) {
                _addrFound = false;
                xTaskCreate(connectTask, "ign_conn", 4096, nullptr, 1, nullptr);
            }
            break;

        case IgnBtState::HANDSHAKING:
            doHandshake();
            break;

        case IgnBtState::ACTIVE:
            if (_client && !_client->isConnected()) {
                wlog("[ign] connection lost");
                vdata.ign_connected = false;
                _state   = IgnBtState::IDLE;
                _retryMs = millis();
            }
            break;

        case IgnBtState::ERROR:
            if (millis() - _retryMs >= RETRY_MS) {
                _state   = IgnBtState::IDLE;
                _retryMs = 0;
            }
            break;
    }
}

IgnBtState  ignition_bt_state() { return _state; }

const char* ignition_bt_state_str() {
    switch (_state) {
        case IgnBtState::IDLE:        return "Idle";
        case IgnBtState::SCANNING:    return "Scanning...";
        case IgnBtState::CONNECTING:  return "Connecting...";
        case IgnBtState::HANDSHAKING: return "Handshaking...";
        case IgnBtState::ACTIVE:      return "Connected";
        case IgnBtState::ERROR:       return "Error";
    }
    return "?";
}

static void writeCmd(const uint8_t* cmd, size_t len) {
    if (_state == IgnBtState::ACTIVE && _rxChar)
        _rxChar->writeValue(cmd, len, false);
}

void ignition_send_advance_plus()  { writeCmd(CMD_ADV_PLUS,    sizeof(CMD_ADV_PLUS));    }
void ignition_send_advance_minus() { writeCmd(CMD_ADV_MINUS,   sizeof(CMD_ADV_MINUS));   }
void ignition_send_tune_toggle()   { writeCmd(CMD_TUNE_TOGGLE, sizeof(CMD_TUNE_TOGGLE)); }
