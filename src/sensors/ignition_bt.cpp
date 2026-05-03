#include "ignition_bt.h"
#include "../data/vehicle_data.h"
#include "../data/sim.h"
#include "../net/wifi_log.h"
#include <NimBLEDevice.h>
#include <Arduino.h>

// ── UUIDs (from iafilius/123Tune-plus-Simulator, ESP32 reverse-engineering port) ─
// Custom 128-bit UUIDs published by 123ignition TUNE+ firmware. The simulator
// re-creates these byte-for-byte and is accepted by the official iOS 123Tune+
// app, so they are considered authoritative.
static const char* SVC_UUID  = "da2b84f1-6279-48de-bdc0-afbea0226079";
static const char* INFO_UUID = "99564a02-dc01-4d3c-b04e-3bb1ef0571b2";  // read
static const char* BODY_UUID = "a87988b9-694c-479c-900e-95dfa6c00a24";  // read/write
static const char* RX_UUID   = "bf03260c-7205-4c25-af43-93b1c299d159";  // write commands
static const char* TX_UUID   = "18cda784-4bd3-4370-85bb-bfed91ec86af";  // notify data

// ── Handshake commands ────────────────────────────────────────────────────────
// Six commands sent in sequence after subscribing to TX (300 ms apart).
// Live RPM/Advance/Temp/Voltage/Pressure/Ampere notifications begin AFTER the
// final "v@\r" — see iafilius/123Tune-plus-Simulator ble.ino:
//   // All Meter values show up after command 6 !!!
//   // Service command 'v@' (Version Service Command)
static const uint8_t CMD_KEEPALIVE[]     = { 0x0D };
static const uint8_t CMD_ADV_CURVE_LO[]  = { 0x31, 0x30, 0x40, 0x0D }; // "10@\r" advance curve pts 1-7
static const uint8_t CMD_ADV_CURVE_HI[]  = { 0x31, 0x31, 0x40, 0x0D }; // "11@\r" pts 8-10 + PIN + limits
static const uint8_t CMD_MAP_CURVE_LO[]  = { 0x31, 0x32, 0x40, 0x0D }; // "12@\r" vacuum/MAP pts 1-7
static const uint8_t CMD_MAP_CURVE_HI[]  = { 0x31, 0x33, 0x40, 0x0D }; // "13@\r" vacuum/MAP pts 8-10
static const uint8_t CMD_VERSION[]       = { 0x76, 0x40, 0x0D };       // "v@\r"  triggers live data stream

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
static uint32_t                 _keepaliveMs  = 0;
static uint32_t                 _serialMs     = 0;
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

static uint32_t _notifyCount = 0;

static void notifyCB(NimBLERemoteCharacteristic* /*pChar*/,
                     uint8_t* pData, size_t length, bool /*isNotify*/) {
    _notifyCount++;
    // Log first 20 notifications in full, 20 bytes per log line
    if (_notifyCount <= 20) {
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
        bool matchSvc  = dev->haveServiceUUID() &&
                         dev->isAdvertisingService(NimBLEUUID(SVC_UUID));
        bool matchName = dev->haveName() &&
                         strstr(dev->getName().c_str(), "123") != nullptr;

        wlog("[ign] scan: '%s'  %s  RSSI=%d  svc=%s name=%s",
             dev->haveName() ? dev->getName().c_str() : "(no name)",
             dev->getAddress().toString().c_str(),
             dev->getRSSI(),
             matchSvc  ? "MATCH" : (dev->haveServiceUUID() ? "no" : "-"),
             matchName ? "MATCH" : "no");

        if (matchSvc || matchName) {
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

    // Mimic the iOS app: read Info and Body characteristics once after subscribing.
    // Some firmware revisions gate the streaming on these reads.
    if (auto* infoSvc = _client->getService(SVC_UUID)) {
        if (auto* info = infoSvc->getCharacteristic(INFO_UUID)) {
            if (info->canRead()) {
                std::string v = info->readValue();
                wlog("[ign] info read len=%u", (unsigned)v.size());
            }
        }
        if (auto* body = infoSvc->getCharacteristic(BODY_UUID)) {
            if (body->canRead()) {
                std::string v = body->readValue();
                wlog("[ign] body read len=%u", (unsigned)v.size());
            }
        }
    }

    // Full 6-step handshake after subscribe.  All writes are write-command (no response).
    // Live data only begins after "v@\r" (step 6) per the published reverse-engineering.
    vTaskDelay(pdMS_TO_TICKS(500));

    _rxChar->writeValue(CMD_KEEPALIVE,    sizeof(CMD_KEEPALIVE),    false);
    wlog("[ign] hs 1/6 keepalive");
    vTaskDelay(pdMS_TO_TICKS(HS_GAP_MS));

    _rxChar->writeValue(CMD_ADV_CURVE_LO, sizeof(CMD_ADV_CURVE_LO), false);
    wlog("[ign] hs 2/6 adv-curve-lo (10@)");
    vTaskDelay(pdMS_TO_TICKS(HS_GAP_MS));

    _rxChar->writeValue(CMD_ADV_CURVE_HI, sizeof(CMD_ADV_CURVE_HI), false);
    wlog("[ign] hs 3/6 adv-curve-hi (11@)");
    vTaskDelay(pdMS_TO_TICKS(HS_GAP_MS));

    _rxChar->writeValue(CMD_MAP_CURVE_LO, sizeof(CMD_MAP_CURVE_LO), false);
    wlog("[ign] hs 4/6 map-curve-lo (12@)");
    vTaskDelay(pdMS_TO_TICKS(HS_GAP_MS));

    _rxChar->writeValue(CMD_MAP_CURVE_HI, sizeof(CMD_MAP_CURVE_HI), false);
    wlog("[ign] hs 5/6 map-curve-hi (13@)");
    vTaskDelay(pdMS_TO_TICKS(HS_GAP_MS));

    _rxChar->writeValue(CMD_VERSION,      sizeof(CMD_VERSION),      false);
    wlog("[ign] hs 6/6 version (v@) — live data should now stream");

    _notifyCount  = 0;
    _keepaliveMs  = millis();
    vdata.ign_connected = true;
    _state = IgnBtState::ACTIVE;
    wlog("[ign] ACTIVE — streaming data");
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
            // Keepalive CR every 10 s + log current values
            if (millis() - _keepaliveMs >= 10000) {
                _keepaliveMs = millis();
                wlog("[ign] RPM=%.0f  ADV=%.1f  TEMP=%.0f  V=%.2f  A=%.2f  MAP=%.0f  N=%lu",
                     vdata.rpm, vdata.ign_advance_deg, vdata.ign_temp_c,
                     vdata.ign_voltage_v, vdata.ign_ampere, vdata.ign_pressure_kpa,
                     _notifyCount);
                if (_rxChar)
                    _rxChar->writeValue(CMD_KEEPALIVE, sizeof(CMD_KEEPALIVE), false);
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
