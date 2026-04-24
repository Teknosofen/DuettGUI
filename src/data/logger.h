#pragma once
#include <stdint.h>

// How often to write a CSV row (ms).
#define LOG_INTERVAL_MS 1000

void        logger_init();        // call after sd_init(); starts logging automatically
void        logger_update();      // call every loop()
void        logger_enable(bool on);
bool        logger_enabled();
uint16_t    logger_sequence();    // current file sequence number
uint32_t    logger_records();     // rows written this session
const char* logger_filename();    // e.g. "/LOG_0042.CSV"
uint64_t    logger_file_bytes();  // current file size in bytes
