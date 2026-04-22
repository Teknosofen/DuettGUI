# DuettGUI

A LovyanGFX-based GUI framework targeting the **Elecrow CrowPanel ESP32-S3 HMI 5.0"** (DIS07050H) — an 800×480 RGB parallel display with GT911 capacitive touch.

---

## Hardware

| Item | Detail |
|---|---|
| MCU | ESP32-S3 (N4R8 — 4 MB flash, 8 MB OPI PSRAM) |
| Display | ILI6122 + ILI5960, 800×480, 16-bit RGB parallel |
| Touch | GT911, I2C (SDA = GPIO19, SCL = GPIO20, addr 0x5D) |
| Board | Elecrow CrowPanel ESP32 HMI 5.0" |

### RGB Bus Pin Map

| Signal | GPIO |
|---|---|
| B0–B4 | 8, 3, 46, 9, 1 |
| G0–G5 | 5, 6, 7, 15, 16, 4 |
| R0–R4 | 45, 48, 47, 21, 14 |
| DE (HENABLE) | 40 |
| VSYNC | 41 |
| HSYNC | 39 |
| PCLK | 0 |

Pixel clock: **12 MHz** (can be raised to 14–16 MHz if stable).

---

## Project Setup

**PlatformIO** (see `platformio.ini`):

```ini
[env:esp32-s3-devkitc-1]
platform     = espressif32
board        = esp32-s3-devkitc-1
framework    = arduino
monitor_speed = 115200
upload_speed  = 921600

board_build.arduino.memory_type = qio_opi   ; enables OPI PSRAM
board_build.flash_mode          = qio
board_build.partitions          = huge_app.csv

build_flags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1

lib_deps =
    lovyan03/LovyanGFX @ ^1.1.16
```

**Build & flash:**
```bash
pio run -t upload
pio device monitor
```

---

## Files

| File | Purpose |
|---|---|
| `include/lgfx_user.h` | LovyanGFX display + GT911 touch configuration |
| `src/main.cpp` | Application entry point |
| `platformio.ini` | PlatformIO build configuration |

---

## Display Driver (`include/lgfx_user.h`)

Defines class `LGFX : public lgfx::LGFX_Device` with:

- `lgfx::Panel_RGB` + `lgfx::Bus_RGB` — full RGB parallel bus configuration with horizontal/vertical timing:
  - H: front porch 8, pulse 4, back porch 16
  - V: front porch 4, pulse 4, back porch 16
- `lgfx::Touch_GT911` — I2C touch, 400 kHz, address `0x5D`, range 0–799 × 0–479

---

## Demo: Rotating Cube (`src/main.cpp`)

A real-time wireframe 3D cube demo with touch-controlled position.

### Features

- **Software 3D rendering** — perspective projection with independent X and Y rotation axes.
- **Depth shading** — each edge is brightness-mapped by its Z midpoint (front = bright white, back = dim grey), giving a convincing 3D look without filled faces.
- **Double buffering** — a full-screen 16-bit `LGFX_Sprite` (800×480 × 2 bytes = ~768 KB, allocated in PSRAM) is rendered off-screen and pushed atomically each frame — zero flicker.
- **Touch to move** — touching the screen repositions the cube's screen-space center to the touch point. Drag freely in X and Y.
- **~60 fps** — 16 ms frame budget with `delay()` for the remainder.

### How it works

```
Each frame:
  1. Read touch → update pos_x, pos_y
  2. Increment ang_x, ang_y (different speeds → non-periodic rotation)
  3. For each of 8 vertices: rotate (X then Y), perspective-project to screen
  4. For each of 12 edges: compute Z midpoint → brightness → draw line on sprite
  5. Push sprite to display
```

**Perspective projection:**
```
scale  = FOV / (z + ZDIST)   ; FOV=260, ZDIST=3.5
screen_x = vertex_x * scale + pos_x
screen_y = vertex_y * scale + pos_y
```

**Depth shading:**
```
brightness = (1 - midZ) * 87.5 + 80   ; maps [-1,1] → [255,80]
```
