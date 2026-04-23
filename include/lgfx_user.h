#pragma once

// ============================================================
//  LovyanGFX display + touch configuration
//  Board  : Elecrow CrowPanel ESP32 HMI 5.0" (DIS07050H)
//  Display: ILI6122 + ILI5960  –  800 x 480  RGB-16bit parallel
//  Touch  : GT911  –  I2C (SDA=GPIO19, SCL=GPIO20)
//  Source : Official Elecrow Arduino Tutorial (elecrow.com wiki)
// ============================================================

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_RGB   _panel_instance;
    lgfx::Bus_RGB     _bus_instance;
    lgfx::Light_PWM   _light_instance;
    lgfx::Touch_GT911 _touch_instance;

public:
    LGFX(void)
    {
        // ---- RGB parallel bus ------------------------------------------
        {
            auto cfg = _bus_instance.config();

            cfg.panel = &_panel_instance;

            // Blue channel (B0-B4)
            cfg.pin_d0  = GPIO_NUM_8;   // B0
            cfg.pin_d1  = GPIO_NUM_3;   // B1
            cfg.pin_d2  = GPIO_NUM_46;  // B2
            cfg.pin_d3  = GPIO_NUM_9;   // B3
            cfg.pin_d4  = GPIO_NUM_1;   // B4

            // Green channel (G0-G5)
            cfg.pin_d5  = GPIO_NUM_5;   // G0
            cfg.pin_d6  = GPIO_NUM_6;   // G1
            cfg.pin_d7  = GPIO_NUM_7;   // G2
            cfg.pin_d8  = GPIO_NUM_15;  // G3
            cfg.pin_d9  = GPIO_NUM_16;  // G4
            cfg.pin_d10 = GPIO_NUM_4;   // G5

            // Red channel (R0-R4)
            cfg.pin_d11 = GPIO_NUM_45;  // R0
            cfg.pin_d12 = GPIO_NUM_48;  // R1
            cfg.pin_d13 = GPIO_NUM_47;  // R2
            cfg.pin_d14 = GPIO_NUM_21;  // R3
            cfg.pin_d15 = GPIO_NUM_14;  // R4

            // Sync / control signals
            cfg.pin_henable = GPIO_NUM_40;  // DE  (Data Enable)
            cfg.pin_vsync   = GPIO_NUM_41;
            cfg.pin_hsync   = GPIO_NUM_39;
            cfg.pin_pclk    = GPIO_NUM_0;

            cfg.freq_write = 15000000;  // 15 MHz (official Elecrow setting)

            // Horizontal timing (pixels) — from official Elecrow config
            cfg.hsync_polarity    = 0;
            cfg.hsync_front_porch = 8;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch  = 43;

            // Vertical timing (lines) — from official Elecrow config
            cfg.vsync_polarity    = 0;
            cfg.vsync_front_porch = 8;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch  = 12;

            cfg.pclk_active_neg = 1;  // pixel latched on falling edge
            cfg.de_idle_high    = 0;
            cfg.pclk_idle_high  = 0;

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // ---- Panel ---------------------------------------------------------
        {
            auto cfg = _panel_instance.config();

            cfg.memory_width  = 800;
            cfg.memory_height = 480;
            cfg.panel_width   = 800;
            cfg.panel_height  = 480;
            cfg.offset_x      = 0;
            cfg.offset_y      = 0;

            _panel_instance.config(cfg);
        }

        // ---- Backlight (GPIO 2, PWM) ----------------------------------------
        {
            auto cfg = _light_instance.config();

            cfg.pin_bl      = GPIO_NUM_2;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;

            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        // ---- GT911 touch ---------------------------------------------------
        {
            auto cfg = _touch_instance.config();

            cfg.x_min        = 0;
            cfg.x_max        = 799;
            cfg.y_min        = 0;
            cfg.y_max        = 479;
            cfg.pin_int      = GPIO_NUM_NC;
            cfg.bus_shared   = false;
            cfg.offset_rotation = 0;

            cfg.i2c_port = 0;
            cfg.i2c_addr = 0x5D;
            cfg.pin_sda  = GPIO_NUM_19;
            cfg.pin_scl  = GPIO_NUM_20;
            cfg.freq     = 400000;

            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }
};
