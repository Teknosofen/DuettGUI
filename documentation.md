# DuettGUI

A LovyanGFX-based GUI framework targeting the **Elecrow CrowPanel ESP32-S3 HMI 5.0"** (DIS07050H) — an 800×480 RGB parallel display with GT911 capacitive touch.

---

## Hardware

| Item | Detail |
|---|---|
| MCU | ESP32-S3-WROOM-1-N4R8 (4 MB flash, 8 MB OPI PSRAM) |
| Display | ILI6122 + ILI5960, 800×480, 16-bit RGB parallel |
| Touch | GT911, I2C (SDA = GPIO19, SCL = GPIO20, addr 0x5D) |
| Board | Elecrow CrowPanel ESP32 HMI 5.0" (DIS07050H) |

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

**PlatformIO** (`platformio.ini`):

```ini
[env:esp32-s3-devkitc-1]
platform     = espressif32@^6.5.0
board        = esp32-s3-devkitc-1
framework    = arduino
monitor_speed = 115200
upload_speed  = 921600

; N4R8 = 4 MB flash + 8 MB OPI PSRAM
board_build.arduino.memory_type = qio_opi
board_build.flash_mode          = qio
board_upload.flash_size         = 4MB       ; must match actual module (N4R8 = 4 MB)
board_upload.maximum_size       = 4194304
board_build.partitions          = no_ota.csv
board_build.filesystem          = littlefs

build_flags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1

lib_deps =
    lovyan03/LovyanGFX @ ^1.1.16
```

### PlatformIO Commands

| Command | Description |
|---|---|
| `pio run` | Compile only |
| `pio run -t upload` | Compile and flash firmware |
| `pio run -t uploadfs` | Upload `data/` folder to LittleFS filesystem partition |
| `pio run -t erase` | Erase entire flash (use before first upload or to recover a bricked board) |
| `pio device monitor` | Open serial monitor at 115200 baud |
| `pio run -t upload && pio device monitor` | Flash then monitor in one step |

### First Flash / Recovery Procedure

The ESP32-S3 uses native USB CDC — it only enumerates as a COM port when firmware is running or in download mode.

1. Hold **BOOT**, press and release **RESET**, release **BOOT** → board enters download mode
2. `pio run -t erase` — wipe flash cleanly (recommended on first flash or after partition table changes)
3. Hold **BOOT** + **RESET** again
4. `pio run -t upload` — flash firmware
5. Press **RESET** to boot normally

---

## Source Structure

```
src/
├── main.cpp              — setup() and loop()
├── display/
│   ├── display.h         — extern LGFX display + display_init()
│   └── display.cpp       — global LGFX instance, init
├── ui/
│   ├── screen_cube.h
│   └── screen_cube.cpp   — rotating cube demo screen
└── fs/
    ├── storage.h         — LittleFS helpers
    └── storage.cpp
include/
└── lgfx_user.h           — LovyanGFX display + touch configuration
data/                     — LittleFS root (uploaded with pio run -t uploadfs)
```

Adding a new screen: create `src/ui/screen_foo.h/.cpp`, call `screen_foo_init()` from `setup()` and `screen_foo_update()` from `loop()`.

---

## Files

| File | Purpose |
|---|---|
| `include/lgfx_user.h` | LovyanGFX display + GT911 touch configuration |
| `src/main.cpp` | Application entry point |
| `src/display/display.cpp` | Global LGFX display instance |
| `src/ui/screen_cube.cpp` | Rotating cube demo |
| `src/fs/storage.cpp` | LittleFS read/write/exists helpers |
| `platformio.ini` | PlatformIO build configuration |
| `data/` | LittleFS filesystem root — place assets here |

---

## Display Driver (`include/lgfx_user.h`)

Defines class `LGFX : public lgfx::LGFX_Device` with:

- `lgfx::Panel_RGB` + `lgfx::Bus_RGB` — RGB parallel bus (must be included manually; not in LovyanGFX's default include chain for ESP32-S3).
  - H timing: front porch 8, pulse 4, back porch 16
  - V timing: front porch 4, pulse 4, back porch 16
- `lgfx::Touch_GT911` — I2C touch, 400 kHz, address `0x5D`, range 0–799 × 0–479

---

## Demo: Rotating Cube (`src/ui/screen_cube.cpp`)

A real-time wireframe 3D cube demo with touch-controlled position.

### Features

- **Software 3D rendering** — perspective projection with independent X and Y rotation axes.
- **Depth shading** — each edge is brightness-mapped by its Z midpoint (front = bright white, back = dim grey).
- **Double buffering** — full-screen 16-bit `LGFX_Sprite` (800×480 × 2 bytes ≈ 768 KB in PSRAM) pushed atomically each frame — zero flicker.
- **Touch to move** — touch repositions the cube center. Drag freely in X and Y.
- **~60 fps** — 16 ms frame budget.

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
scale    = FOV / (z + ZDIST)   ; FOV=260, ZDIST=3.5
screen_x = vertex_x * scale + pos_x
screen_y = vertex_y * scale + pos_y
```

**Depth shading:**
```
brightness = (1 - midZ) * 87.5 + 80   ; maps [-1,1] → [255,80]
```
