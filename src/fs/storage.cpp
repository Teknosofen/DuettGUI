#include "storage.h"
#include <LittleFS.h>

bool storage_init()
{
    if (!LittleFS.begin(true)) {
        Serial.println("[fs] mount failed");
        return false;
    }
    storage_info();
    return true;
}

bool storage_exists(const char* path)
{
    return LittleFS.exists(path);
}

String storage_read(const char* path)
{
    File f = LittleFS.open(path, "r");
    if (!f) return String();
    String s = f.readString();
    f.close();
    return s;
}

bool storage_write(const char* path, const String& data)
{
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.print(data);
    f.close();
    return true;
}

bool storage_remove(const char* path)
{
    return LittleFS.remove(path);
}

void storage_info()
{
    Serial.printf("[fs] %u KB used / %u KB total\n",
        (unsigned)(LittleFS.usedBytes()  / 1024),
        (unsigned)(LittleFS.totalBytes() / 1024));
}
