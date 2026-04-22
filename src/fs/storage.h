#pragma once
#include <Arduino.h>

bool     storage_init();
bool     storage_exists(const char* path);
String   storage_read(const char* path);
bool     storage_write(const char* path, const String& data);
bool     storage_remove(const char* path);
void     storage_info();
