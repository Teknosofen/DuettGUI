#include "screen_storage.h"
#include "widgets.h"
#include "../data/logger.h"
#include "../fs/storage.h"
#include <SD.h>
#include <Arduino.h>
#include <stdio.h>

// ── helper ────────────────────────────────────────────────────────────────────

static void formatSize(char* buf, size_t len, uint64_t bytes)
{
    if (bytes >= (uint64_t)1024 * 1024 * 1024) {
        snprintf(buf, len, "%.1f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= (uint64_t)1024 * 1024) {
        snprintf(buf, len, "%.0f MB", (double)bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buf, len, "%u KB", (unsigned)(bytes / 1024));
    }
}

// ── ScreenStorage ─────────────────────────────────────────────────────────────

void ScreenStorage::init(uint16_t contentW, uint16_t contentH)
{
    _contentW = contentW;
}

void ScreenStorage::drawButton(lgfx::LovyanGFX& gfx, bool enabled)
{
    uint32_t bg = enabled
        ? gfx.color888(0, 0x80, 0)
        : gfx.color888(0x40, 0x40, 0x40);
    uint32_t border = gfx.color888(0x20, 0x20, 0x20);

    gfx.fillRect(BTN_X, BTN_Y, BTN_W, BTN_H, bg);
    gfx.drawRect(BTN_X, BTN_Y, BTN_W, BTN_H, border);

    gfx.setFont(&lgfx::fonts::DejaVu18);
    gfx.setTextSize(1);
    gfx.setTextColor(TFT_WHITE, bg);

    const char* lbl = enabled ? "\xE2\x97\x8F  LOGGING ON" : "\xE2\x97\x8B  LOGGING OFF";
    int tw = (int)gfx.textWidth(lbl);
    int th = (int)gfx.fontHeight();
    gfx.setCursor(BTN_X + (BTN_W - tw) / 2, BTN_Y + (BTN_H - th) / 2);
    gfx.print(lbl);
}

void ScreenStorage::drawDynamic(lgfx::LovyanGFX& gfx)
{
    bool sdOk = sd_available();

    // ── SD status dot + text (y=45) ──────────────────────────────────────────
    {
        gfx.fillRect(20, 38, 300, 28, TFT_BLACK);
        uint32_t dotCol = sdOk
            ? gfx.color888(0, 0xC0, 0)
            : gfx.color888(0xC0, 0, 0);
        gfx.fillCircle(30, 52, 6, dotCol);

        gfx.setFont(&lgfx::fonts::DejaVu18);
        gfx.setTextSize(1);
        gfx.setTextColor(gfx.color888(0xD0, 0xD0, 0xD0), TFT_BLACK);
        gfx.setCursor(44, 43);
        gfx.print(sdOk ? "Card OK" : "No card");
    }

    // ── SD size line (y=72) ───────────────────────────────────────────────────
    {
        gfx.fillRect(20, 65, 500, 28, TFT_BLACK);
        if (sdOk) {
            char totBuf[16], freeBuf[16];
            uint64_t total = sd_total_bytes();
            uint64_t used  = sd_used_bytes();
            uint64_t free_ = (total > used) ? (total - used) : 0;
            formatSize(totBuf, sizeof(totBuf), total);
            formatSize(freeBuf, sizeof(freeBuf), free_);

            char line[64];
            snprintf(line, sizeof(line), "Total: %s   Free: %s", totBuf, freeBuf);

            gfx.setFont(&lgfx::fonts::DejaVu18);
            gfx.setTextSize(1);
            gfx.setTextColor(gfx.color888(0xA0, 0xA0, 0xA0), TFT_BLACK);
            gfx.setCursor(20, 70);
            gfx.print(line);
        }
    }

    // ── Toggle button (if state changed) ─────────────────────────────────────
    bool en = logger_enabled();
    if (en != _prevEnabled) {
        drawButton(gfx, en);
        _prevEnabled = en;
    }

    // ── Log filename + size (y=218) ───────────────────────────────────────────
    {
        gfx.fillRect(20, 211, 500, 24, TFT_BLACK);
        gfx.setFont(&lgfx::fonts::DejaVu18);
        gfx.setTextSize(1);
        gfx.setTextColor(gfx.color888(0xD0, 0xD0, 0xD0), TFT_BLACK);
        gfx.setCursor(20, 215);

        const char* fn = logger_filename();
        if (fn && fn[0] != '\0') {
            char szBuf[16];
            formatSize(szBuf, sizeof(szBuf), logger_file_bytes());
            char line[48];
            snprintf(line, sizeof(line), "%s  (%s)", fn, szBuf);
            gfx.print(line);
        } else {
            gfx.print("No log file");
        }
    }

    // ── Record count (y=248) ──────────────────────────────────────────────────
    {
        gfx.fillRect(20, 241, 400, 24, TFT_BLACK);
        gfx.setFont(&lgfx::fonts::DejaVu18);
        gfx.setTextSize(1);
        gfx.setTextColor(gfx.color888(0xA0, 0xA0, 0xA0), TFT_BLACK);
        gfx.setCursor(20, 245);

        char line[32];
        snprintf(line, sizeof(line), "Records: %lu", (unsigned long)logger_records());
        gfx.print(line);
    }

    _prevSdOk   = sdOk;
    _prevRecords = logger_records();
}

void ScreenStorage::update(lgfx::LovyanGFX& gfx,
                            uint16_t contentW, uint16_t contentH)
{
    if (_needsRedraw) {
        gfx.fillRect(0, 0, contentW, contentH, TFT_BLACK);

        // Static labels
        Widget::sectionLabel(gfx, 20, 15, "SD Card");
        Widget::hRule(gfx, 100, contentW);
        Widget::sectionLabel(gfx, 20, 118, "Data Logging");
        Widget::hRule(gfx, 280, contentW);

        // Initial button draw
        drawButton(gfx, logger_enabled());
        _prevEnabled = logger_enabled();

        // Dynamic fields
        drawDynamic(gfx);

        _staticDrawn = true;
        _needsRedraw = false;
        return;
    }

    // Partial update when something changed
    bool en     = logger_enabled();
    uint32_t rec = logger_records();
    bool sdOk   = sd_available();

    if (en != _prevEnabled || rec != _prevRecords || sdOk != _prevSdOk) {
        drawDynamic(gfx);
    }
}

void ScreenStorage::onTouch(uint16_t x, uint16_t y)
{
    if (x >= BTN_X && x < BTN_X + BTN_W &&
        y >= BTN_Y && y < BTN_Y + BTN_H)
    {
        if (millis() - _lastToggleMs >= 400) {
            logger_enable(!logger_enabled());
            _lastToggleMs = millis();
            _prevEnabled  = !_prevEnabled;   // force button redraw
            _prevRecords  = 0xFFFFFFFF;      // force stats redraw
        }
    }
}
