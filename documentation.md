# DuettGUI

A LovyanGFX-based GUI framework for the **1964 Volvo Duett** — measuring and displaying engine, fuel, and GPS data on an **Elecrow CrowPanel ESP32-S3 HMI 5.0"** (DIS07050H), an 800×480 RGB parallel display with GT911 capacitive touch.

---

## Hardware

| Item | Detail |
|---|---|
| MCU | ESP32-S3-WROOM-1-N4R8 (4 MB flash, 8 MB OPI PSRAM) |
| Display | ILI6122 + ILI5960, 800×480, 16-bit RGB parallel |
| Touch | GT911, I2C (SDA = GPIO19, SCL = GPIO20) |
| Backlight | PWM on GPIO 2 |
| SD card | SPI on GPIO 10 (CS), 11 (MOSI), 12 (SCK), 13 (MISO) |
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
| Backlight | 2 |

Pixel clock: **15 MHz**.

### Serial / USB

The board uses a **USB-UART bridge** (CH340) connected to UART0 (GPIO43 TX / GPIO44 RX) — this is the only serial path. GPIO19/20, which are the ESP32-S3 native USB D−/D+ pins, are wired to the GT911 touch controller instead. Do **not** set `ARDUINO_USB_CDC_ON_BOOT` or `ARDUINO_USB_MODE` in build flags.

---

## Project Setup

**`platformio.ini`:**

```ini
[env:esp32-s3-devkitc-1]
; pioarduino: arduino-esp32 3.x / IDF 5.x — required for OPI PSRAM + RGB LCD
platform     = https://github.com/pioarduino/platform-espressif32/releases/download/51.03.07/platform-espressif32.zip
board        = esp32-s3-devkitc-1
framework    = arduino
monitor_speed = 115200
upload_speed  = 921600

board_build.arduino.memory_type = qio_opi
board_build.flash_mode          = qio
board_upload.flash_size         = 4MB
board_upload.maximum_size       = 4194304
board_build.partitions          = no_ota.csv
board_build.filesystem          = littlefs

lib_deps =
    lovyan03/LovyanGFX @ ^1.1.16
```

> **Why pioarduino?** The standard `espressif32` platform uses IDF 4.4 which hangs during OPI PSRAM init. pioarduino 51.03.07 uses IDF 5.x which initialises PSRAM correctly.

### PlatformIO Commands

| Command | Description |
|---|---|
| `pio run` | Compile only |
| `pio run -t upload` | Compile and flash firmware |
| `pio run -t uploadfs` | Upload `data/` to LittleFS partition |
| `pio run -t erase` | Erase entire flash |
| `pio device monitor` | Open serial monitor at 115200 baud |
| `pio run -t upload; pio device monitor` | Flash then monitor (use `;` not `&&` in PowerShell) |

### First Flash / Recovery

1. Hold **BOOT**, press and release **RESET**, release **BOOT** → download mode
2. `pio run -t erase`
3. BOOT + RESET again
4. `pio run -t upload`
5. Press **RESET** to boot normally

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                   ScreenManager                     │
│  touch polling · page switching · nav bar           │
├──────────┬──────────────┬───────────┬───────────────┤
│ Screen   │ Screen       │ Screen    │ Screen  · · · │
│ Cube     │ Vehicle      │ GPS       │ (future)      │
├──────────┴──────────────┴───────────┴───────────────┤
│         Widget  (dataRow · hRule · sectionLabel)    │
├─────────────────────────────────────────────────────┤
│                   VehicleData                       │
│   shared struct updated by sensor readers           │
├──────────────────────────────────────────────────────┤
│        display.h / LovyanGFX / RGB LCD panel        │
└─────────────────────────────────────────────────────┘
```

### Screen layout

```
┌──────────────────────── 800 px ─────────────────────────┐
│                                                          │
│              Content area  800 × 432 px                 │
│              Active page renders here                   │
│                                                          │
├──────────────────────────────────────────────────────────┤  y = 432
│  [◀]        Page Name   (n / N)        [▶]             │  48 px
└──────────────────────────────────────────────────────────┘  y = 480
   ←120→      ←────── centre ──────→        ←120→
```

Nav bar touch zones: x < 160 → previous page, x ≥ 640 → next page.

---

## Source Structure

```
src/
├── main.cpp                    — setup() · loop() · page registration
│
├── data/
│   ├── vehicle_data.h          — VehicleData struct (all sensor values)
│   └── vehicle_data.cpp        — global vdata instance
│
├── display/
│   ├── display.h               — extern LGFX* display
│   └── display.cpp             — display_init()
│
├── ui/
│   ├── screen.h                — abstract Screen base class
│   ├── screen_manager.h/.cpp   — page registry · touch routing · nav bar
│   ├── screen_cube.h/.cpp      — rotating wireframe cube demo
│   ├── screen_vehicle.h/.cpp   — engine / fuel sensor readout
│   ├── screen_gps.h/.cpp       — GPS speed · heading · position
│   └── widgets.h/.cpp          — shared stateless drawing helpers
│
└── fs/
    ├── storage.h/.cpp          — LittleFS helpers (init · read · write · remove)
    └── storage.h/.cpp          — SD card helpers  (init · read · write · remove)

include/
└── lgfx_user.h                 — LovyanGFX Panel_RGB + Light_PWM + Touch_GT911

data/                           — LittleFS root (pio run -t uploadfs)
```

---

## Key Classes

### `VehicleData`  (`src/data/vehicle_data.h`)

Single global struct `vdata` shared by all pages. Sensor reader classes update it; pages read it.

| Field | Type | Description |
|---|---|---|
| `rpm` | `float` | Engine speed (rev/min) |
| `throttle_pct` | `float` | Throttle position (0–100 %) |
| `map_kpa` | `float` | Manifold absolute pressure |
| `ambient_kpa` | `float` | Barometric pressure |
| `fuel_flow_lph` | `float` | Fuel flow (L/h) |
| `fuel_per_100km` | `float` | Calculated consumption |
| `fuel_used_l` | `float` | Trip accumulator |
| `speed_kmh` | `float` | GPS speed |
| `heading_deg` | `float` | GPS heading (0–360°) |
| `lat` / `lon` | `double` | GPS position |
| `altitude_m` | `float` | GPS altitude |
| `gps_valid` | `bool` | GPS fix acquired |

---

### `Screen`  (`src/ui/screen.h`)

Abstract base for all pages:

```cpp
class Screen {
public:
    virtual void        init(uint16_t contentW, uint16_t contentH) {}
    virtual void        update(lgfx::LovyanGFX& gfx,
                               uint16_t contentW, uint16_t contentH) = 0;
    virtual void        onTouch(uint16_t x, uint16_t y) {}
    virtual const char* name() const = 0;
};
```

- `init()` — called once on first activation; allocate sprites here.
- `update()` — called every frame; `gfx` is either a manager-level sprite or the display directly.
- `onTouch()` — coordinates are relative to the content area top-left.

### Adding a new page

1. Create `src/ui/screen_foo.h/.cpp` inheriting `Screen`.
2. Implement `update()` using `vdata` fields and `Widget::` helpers.
3. In `main.cpp`: add `static ScreenFoo screenFoo;` and `mgr.addPage(&screenFoo);`.

---

### `ScreenManager`  (`src/ui/screen_manager.h`)

```cpp
bool addPage(Screen* s);   // register before begin()
void begin();              // call after display_init()
void update();             // call every loop() iteration
```

On `update()`: polls touch → routes to page or nav bar → calls `page->update()` → draws nav bar on top. Pages are lazy-initialised (init() on first visit).

Attempts to allocate a full content-area sprite (800×432, ~675 KB) for flicker-free rendering. Falls back to direct draw if the allocation fails.

---

### `Widget` namespace  (`src/ui/widgets.h`)

Stateless helpers; all functions take `lgfx::LovyanGFX& gfx` so they work on any render target.

| Function | Description |
|---|---|
| `Widget::dataRow(gfx, y, label, value, unit, color)` | Label left / value right-aligned / unit; standard row height 80 px |
| `Widget::hRule(gfx, y, w)` | Thin horizontal separator line |
| `Widget::sectionLabel(gfx, x, y, text)` | Small gray heading |

---

## Pages

### ScreenCube  —  `"Cube Demo"`

Rotating wireframe 3D cube, touch to move. Uses a 300×300 sprite (~175 KB internal heap) centred in the content area. Falls back to direct draw if allocation fails.

Projection: `scale = 180 / (z + 5)` → vertices project ≈ ±95 px from centre.

### ScreenVehicle  —  `"Vehicle"`

Five `Widget::dataRow` rows centred vertically:

| Row | Source field | Colour |
|---|---|---|
| RPM | `vdata.rpm` | Green |
| Throttle | `vdata.throttle_pct` | Cyan |
| Inlet pressure | `vdata.map_kpa` | Orange |
| Ambient pressure | `vdata.ambient_kpa` | Orange |
| Fuel flow | `vdata.fuel_flow_lph` | Red |

No sprite — draws directly on `gfx` each frame.

### ScreenGPS  —  `"GPS"`

```
┌──────────────────────┬──────────────────────────┐
│  Speed               │  Heading                 │
│  (size-7, green)     │  degrees + compass point │
│  km/h                │  (cyan / amber)           │
├──────────────────────┴──────────────────────────┤
│  Latitude   DD° MM.MMMM' N/S                    │
│  Longitude  DD° MM.MMMM' E/W                    │
│  Altitude                                       │
├─────────────────────────────────────────────────┤
│  L/100km (moving) or L/h (stationary)  ● fix   │
└─────────────────────────────────────────────────┘
```

No sprite — draws directly on `gfx` each frame.

---

## Storage

### LittleFS  (internal flash, ~1.9 MB)

```cpp
storage_init();
storage_write("/settings.json", jsonString);
String s = storage_read("/settings.json");
```

Use for persistent settings (calibration, preferences).

### SD card  (SPI, GPIO 10–13)

```cpp
sd_init();               // optional — system boots without a card
if (sd_available()) {
    sd_write("/log.csv", line);
    String s = sd_read("/config.json");
}
```

Use for data logging and large assets. `sd_init()` prints card type and capacity to serial.

---

## Display Driver (`include/lgfx_user.h`)

| Component | Detail |
|---|---|
| Panel | `lgfx::Panel_RGB` + `lgfx::Bus_RGB` (headers included manually) |
| Backlight | `lgfx::Light_PWM` on GPIO 2, PWM ch 7, 44.1 kHz |
| Touch | `lgfx::Touch_GT911`, I2C addr `0x5D`, 400 kHz |

### Timing (from official Elecrow configuration)

| Parameter | Value |
|---|---|
| Pixel clock | 15 MHz |
| HSYNC front / pulse / back porch | 8 / 4 / 43 |
| VSYNC front / pulse / back porch | 8 / 4 / 12 |
| `pclk_active_neg` | 1 (latch on falling edge) |

---

## Memory Layout

| Region | Size | Usage |
|---|---|---|
| Internal SRAM | ~348 KB free at boot | Application heap, sprites |
| PSRAM (8 MB) | ~750 KB used by IDF LCD driver | RGB panel framebuffer |
| PSRAM remainder | ~7.2 MB | Not accessible via Arduino heap API; available via IDF `heap_caps_malloc(MALLOC_CAP_SPIRAM)` if needed |

The RGB panel framebuffer is allocated by `esp_lcd_rgb_panel_create()` at `display->init()` — outside the Arduino heap — which is why `ESP.getPsramSize()` reports 0 while the display works normally.

The `ScreenCube` sprite (300×300, 175 KB) is allocated from internal SRAM. `ScreenVehicle` and `ScreenGPS` draw directly with no sprite.
