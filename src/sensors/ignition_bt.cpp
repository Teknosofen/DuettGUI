#include "ignition_bt.h"
#include "../data/vehicle_data.h"
#include "../data/sim.h"
#include "../net/wifi_log.h"
#include <NimBLEDevice.h>
#include <Arduino.h>


// ── Handshake commands ────────────────────────────────────────────────────────
// Determined by capturing the real 123Tune+ Android app's BLE HCI snoop log —
// the previously-assumed 6-step sequence (borrowed from the unrelated ESP32
// simulator project) was wrong. The real app sends only 3 commands, in this
// order, each with a trailing 0x24 ('$') that our earlier attempts omitted:
//   "v@\r$"  -> version (curve-format echo reply)
//   "11@\r$" -> adv-curve-hi (curve-format echo reply)
//   "10@\r$" -> adv-curve-lo — device sends NO echo reply to this one and
//               instead goes straight into a continuous, unprompted stream
//               of real 5-byte live packets. 12@/13@ (MAP curve) are never
//               sent by the real app and are not needed.
static const uint8_t CMD_KEEPALIVE[]     = { 0x24 };                         // "$" — bare ping, seen ~every 0.1-1.5 s
static const uint8_t CMD_VERSION[]       = { 0x76, 0x40, 0x0D, 0x24 };       // "v@\r$"
static const uint8_t CMD_ADV_CURVE_HI[]  = { 0x31, 0x31, 0x40, 0x0D, 0x24 }; // "11@\r$"
static const uint8_t CMD_ADV_CURVE_LO[]  = { 0x31, 0x30, 0x40, 0x0D, 0x24 }; // "10@\r$" -> triggers live stream

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
static uint32_t                 _retryMs      = 0;
static uint32_t                 _pollMs       = 0;
static uint32_t                 _wlogMs       = 0;
static uint32_t                 _serialMs     = 0;
static constexpr uint32_t       RETRY_MS   = 8000;
static constexpr uint32_t       HS_GAP_MS  = 300;

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

static uint32_t _notifyCount = 0;

static void notifyCB(NimBLERemoteCharacteristic* /*pChar*/,
                     uint8_t* pData, size_t length, bool /*isNotify*/) {
    _notifyCount++;
    // Log first 20 notifications in full, 20 bytes per log line
    if (_notifyCount <= 200) {
        size_t off = 0;
        while (off < length) {
            char hex[70]; int n = 0;
            size_t end = off + 20; if (end > length) end = length;
            for (size_t i = off; i < end; i++)
                n += snprintf(hex + n, sizeof(hex) - n, "%02X ", pData[i]);
            if (off == 0)
                wlog("[ign] notify #%lu  len=%u  %s", _notifyCount, (unsigned)length, hex);
            else
                wlog("[ign]   +%02u  %s", (unsigned)off, hex);
            off = end;
        }
    }
    // Only parse live packets during ACTIVE — handshake responses are curve/version
    // config data in a different ASCII format that produces garbage if fed to the
    // 5-byte binary parser.
    if (_state == IgnBtState::ACTIVE)
        parseStream(pData, length);
}

class ClientCB : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient*) override {
        wlog("[ign] BLE connected");
        // connectTask drives all post-connect work; nothing to do here
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

class ScanCB : public NimBLEScanCallbacks {
    void onDiscovered(const NimBLEAdvertisedDevice* dev) override {
        // Match by name only. There is no custom advertised service to match on —
        // confirmed via a real BLE HCI snoop: the device's only GATT services are
        // GAP/GATT/NUS(6e400001)/TxPower/DeviceInfo/Battery.
        bool matchName = dev->haveName() &&
                         strstr(dev->getName().c_str(), "123") != nullptr;

        wlog("[ign] scan: '%s'  %s  RSSI=%d  name=%s",
             dev->haveName() ? dev->getName().c_str() : "(no name)",
             dev->getAddress().toString().c_str(),
             dev->getRSSI(),
             matchName ? "MATCH" : "no");

        if (matchName) {
            wlog("[ign] >> SELECTED  addr=%s", dev->getAddress().toString().c_str());
            NimBLEDevice::getScan()->stop();
            _devAddr   = dev->getAddress();
            _addrFound = true;
            _state     = IgnBtState::CONNECTING;
        }
    }
    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        wlog("[ign] scan ended  found=%d  reason=%d", results.getCount(), reason);
    }
};
static ScanCB _scanCB;

// ── FreeRTOS connect+setup task ───────────────────────────────────────────────
// All blocking BLE operations run here so loop()/WiFi are never stalled.

static bool setupChars() {
    // Enumerate ALL services with a forced GATT discovery pass.
    // getService() alone can return nullptr even when the service exists,
    // and calling getCharacteristic() on a service whose characteristic list
    // has not been fetched yet always returns nullptr.  The correct sequence
    // is: getServices(true) → find target service → getCharacteristics(true)
    // on that specific service → then look up by UUID.
    auto& svcs = _client->getServices(true);
    wlog("[ign] device exposes %d service(s):", (int)svcs.size());

    NimBLERemoteService* svc = nullptr;
    for (auto* s : svcs) {
        const std::string ustr = s->getUUID().toString();
        // Match by substring — NimBLE 2.x operator== and strcasecmp both fail due
        // to internal byte-order mismatch between wire-received and string-parsed UUIDs.
        // "6e400001" is unique to the Nordic UART Service.
        bool isNus = (strstr(ustr.c_str(), "6e400001") != nullptr) ||
                     (strstr(ustr.c_str(), "6E400001") != nullptr);
        wlog("[ign]   [len=%d] %s%s", (int)ustr.size(), ustr.c_str(), isNus ? "  <- NUS" : "");
        if (isNus) svc = s;
    }

    if (!svc) { wlog("[ign] NUS service not found"); return false; }

    // Discover characteristics and pick RX/TX by strstr — same NimBLE 2.x byte-order
    // issue affects getCharacteristic(uuid) lookups, so we match from the returned list.
    auto& chars = svc->getCharacteristics(true);
    wlog("[ign] NUS has %d char(s):", (int)chars.size());
    for (auto* c : chars) {
        const std::string custr = c->getUUID().toString();
        wlog("[ign]   %s  write=%s notify=%s indicate=%s",
             custr.c_str(),
             c->canWrite()    ? "yes" : "no",
             c->canNotify()   ? "yes" : "no",
             c->canIndicate() ? "yes" : "no");
        if (strstr(custr.c_str(), "6e400002") || strstr(custr.c_str(), "6E400002"))
            _rxChar = c;
        if (strstr(custr.c_str(), "6e400003") || strstr(custr.c_str(), "6E400003"))
            _txChar = c;
    }

    if (!_rxChar) { wlog("[ign] RX char (6e400002) not found"); return false; }
    if (!_txChar) { wlog("[ign] TX char (6e400003) not found"); return false; }
    wlog("[ign] chars OK");
    return true;
}

static void connectTask(void*) {
    if (!_client) {
        _client = NimBLEDevice::createClient();
        _client->setClientCallbacks(&_clientCB, false);
        _client->setConnectionParams(12, 12, 0, 600);  // 6 s supervision timeout
    }

    wlog("[ign] connecting to %s  heap=%u KB",
         _devAddr.toString().c_str(), (unsigned)(ESP.getFreeHeap() / 1024));
    bool ok = _client->connect(_devAddr);
    wlog("[ign] connect %s", ok ? "OK" : "FAILED");
    if (!ok) {
        NimBLEDevice::deleteClient(_client);
        _client  = nullptr;
        _state   = IgnBtState::IDLE;
        _retryMs = millis();
        vTaskDelete(nullptr);
        return;
    }

    // All post-connect work runs here — never in the Arduino loop task.
    _state = IgnBtState::HANDSHAKING;

    if (!setupChars()) {
        _client->disconnect();
        vTaskDelete(nullptr);
        return;
    }

    // Attempt encrypted/bonded connection — device may require pairing before streaming
    wlog("[ign] requesting secure connection...");
    bool sec = _client->secureConnection();
    wlog("[ign] secure: %s", sec ? "ok" : "not required / failed");

    // Subscribe to TX — also try RX in case the device returns swapped roles
    bool subTx = false, subRx = false;
    if (_txChar->canNotify())   subTx = _txChar->subscribe(true,  notifyCB);
    if (_txChar->canIndicate()) subTx = _txChar->subscribe(false, notifyCB);
    if (_rxChar->canNotify())   subRx = _rxChar->subscribe(true,  notifyCB);
    wlog("[ign] subscribe TX=%s RX=%s", subTx ? "ok" : "no", subRx ? "ok" : "no");
    if (!subTx && !subRx) {
        wlog("[ign] no subscribe succeeded — disconnecting");
        _client->disconnect();
        vTaskDelete(nullptr);
        return;
    }

    // 3-step handshake, matching the real app's captured BLE traffic exactly.
    // (No separate Info/Body read step: GATT primary service discovery in the
    // snoop log shows only GAP/GATT/NUS/TxPower/DeviceInfo/Battery on this
    // device — no custom service to read from.)
    vTaskDelay(pdMS_TO_TICKS(500));

    _rxChar->writeValue(CMD_VERSION,      sizeof(CMD_VERSION),      false);
    wlog("[ign] hs 1/3 version (v@\\r$)");
    vTaskDelay(pdMS_TO_TICKS(HS_GAP_MS));

    _rxChar->writeValue(CMD_ADV_CURVE_HI, sizeof(CMD_ADV_CURVE_HI), false);
    wlog("[ign] hs 2/3 adv-curve-hi (11@\\r$)");
    vTaskDelay(pdMS_TO_TICKS(HS_GAP_MS));

    _rxChar->writeValue(CMD_ADV_CURVE_LO, sizeof(CMD_ADV_CURVE_LO), false);
    wlog("[ign] hs 3/3 adv-curve-lo (10@\\r$) — expect live stream to start now");
    // No echo reply to this one in the real capture — it goes straight into
    // live streaming. Short wait anyway in case a stray reply arrives, so it
    // isn't mistaken for a live packet by parseStream.
    vTaskDelay(pdMS_TO_TICKS(100));

    // Discard any bytes buffered during handshake and clear stale ign values.
    // Handshake responses are ASCII curve/version data — not live packets.
    _bufLen               = 0;
    vdata.rpm             = 0.0f;
    vdata.ign_advance_deg = 0.0f;
    vdata.ign_temp_c      = 0.0f;
    vdata.ign_voltage_v   = 0.0f;
    vdata.ign_pressure_kpa = 0.0f;
    vdata.ign_ampere      = 0.0f;

    _notifyCount  = 0;
    _pollMs       = millis();
    _wlogMs       = millis();
    _serialMs     = millis();
    vdata.ign_connected = true;
    _state = IgnBtState::ACTIVE;
    wlog("[ign] ACTIVE — expecting unprompted live stream, \"$\" keepalive every 1 s");
    vTaskDelete(nullptr);
}

// ── State machine helpers ─────────────────────────────────────────────────────

static void startScan() {
    wlog("[ign] BLE scan started");
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&_scanCB, false);
    scan->setInterval(100);
    scan->setWindow(50);
    scan->setActiveScan(true);
    scan->start(0, false);   // continuous, non-blocking
    _state = IgnBtState::SCANNING;
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
            break; // ScanCB::onDiscovered drives transition

        case IgnBtState::CONNECTING:
            if (_addrFound) {
                _addrFound = false;
                wlog("[ign] spawning connect task");
                xTaskCreate(connectTask, "ign_conn", 8192, nullptr, 1, nullptr);
            }
            break;

        case IgnBtState::HANDSHAKING:
            break; // connectTask drives this — loop must not block here

        case IgnBtState::ACTIVE:
            if (_client && !_client->isConnected()) {
                wlog("[ign] connection lost");
                vdata.ign_connected = false;
                _state   = IgnBtState::IDLE;
                _retryMs = millis();
                break;
            }
            // Confirmed from the real app's BLE HCI snoop log: once the 3-step
            // handshake completes, the device streams live data continuously and
            // unprompted — no per-metric polling needed at all (the earlier
            // guessed single-byte probes were never real commands). The real app
            // does send an occasional bare "$" (0x24) at irregular ~0.1-1.5 s
            // intervals while streaming; it doesn't look required to sustain the
            // stream, but it's cheap and proven-safe to replicate.
            if (millis() - _pollMs >= 1000) {
                _pollMs = millis();
                if (_rxChar) {
                    bool ok = _rxChar->writeValue(CMD_KEEPALIVE, sizeof(CMD_KEEPALIVE), true);
                    wlog("[ign] keepalive \"$\"  ok=%d  N=%lu", (int)ok, _notifyCount);
                }
            }
            // Status wlog every 10 s
            if (millis() - _wlogMs >= 10000) {
                _wlogMs = millis();
                wlog("[ign] RPM=%.0f  ADV=%.1f  TEMP=%.0f  V=%.2f  A=%.2f  MAP=%.0f  N=%lu",
                     vdata.rpm, vdata.ign_advance_deg, vdata.ign_temp_c,
                     vdata.ign_voltage_v, vdata.ign_ampere, vdata.ign_pressure_kpa,
                     _notifyCount);
            }
            // 1 Hz serial output: RPM / voltage / current / MAP
            if (millis() - _serialMs >= 1000) {
                _serialMs = millis();
                Serial.printf("RPM=%.0f V=%.2f A=%.2f MAP=%.1f ADV=%.1f TEMP=%.0f\n",
                              vdata.rpm, vdata.ign_voltage_v, vdata.ign_ampere,
                              vdata.ign_pressure_kpa, vdata.ign_advance_deg,
                              vdata.ign_temp_c);
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
