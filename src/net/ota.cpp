#include "ota.h"
#include "wifi_log.h"
#include <ArduinoOTA.h>

void ota_init()
{
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        const char* what = (ArduinoOTA.getCommand() == U_FLASH)
                           ? "firmware" : "filesystem";
        wlog("OTA start: %s", what);
    });

    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        // Log at every 10 % step to avoid flooding the ring buffer.
        static uint8_t lastStep = 0xFF;
        uint8_t step = (uint8_t)((done * 10) / total);  // 0-10
        if (step != lastStep) {
            lastStep = step;
            wlog("OTA %u%%", step * 10);
        }
    });

    ArduinoOTA.onEnd([]() {
        wlog("OTA complete — rebooting");
    });

    ArduinoOTA.onError([](ota_error_t err) {
        const char* msg;
        switch (err) {
            case OTA_AUTH_ERROR:    msg = "auth failed";    break;
            case OTA_BEGIN_ERROR:   msg = "begin failed";   break;
            case OTA_CONNECT_ERROR: msg = "connect failed"; break;
            case OTA_RECEIVE_ERROR: msg = "receive failed"; break;
            case OTA_END_ERROR:     msg = "end failed";     break;
            default:                msg = "unknown error";  break;
        }
        wlog("OTA error: %s", msg);
    });

    ArduinoOTA.begin();
    wlog("OTA ready  host=%s  port=3232", OTA_HOSTNAME);
}

void ota_update()
{
    ArduinoOTA.handle();
}
