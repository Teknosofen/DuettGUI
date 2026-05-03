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

#### UART0 shared between GPS and programming

The GPS module is also wired to UART0 (GPIO43/44) via the UART0/UART1 HY2.0-4P connector on the board edge. This means **UART0 cannot be used for `Serial` debug output** while the GPS is connected — it is initialised at 9600 baud exclusively for the GPS (`gps_init()` calls `Serial.begin(9600)`).

Debug output is replaced by the WiFi web console (see [WiFi Log Console](#wifi-log-console) below).

**Flashing firmware while GPS is connected is not safe** — the CH340 drives GPIO44 (RX) during upload, which collides with the GPS TX line. Disconnect the GPS RX wire (board RX → GPS TX) before flashing, or power off the GPS module.

---

## Project Setup

**`platformio.ini`:**

```ini
[platformio]
default_envs = usb

[base]
; pioarduino: arduino-esp32 3.x / IDF 5.x — required for OPI PSRAM + RGB LCD
platform     = https://github.com/pioarduino/platform-espressif32/releases/download/51.03.07/platform-espressif32.zip
board        = esp32-s3-devkitc-1
framework    = arduino
monitor_speed = 115200
board_build.arduino.memory_type = qio_opi
board_build.flash_mode          = qio
board_upload.flash_size         = 4MB
board_build.partitions          = partitions_ota.csv
board_build.filesystem          = littlefs
lib_deps =
    lovyan03/LovyanGFX @ ^1.1.16
    adafruit/Adafruit GPS Library @ ^1.5.4
    h2zero/NimBLE-Arduino @ ^1.4.3

[env:usb]
extends      = base
upload_speed = 921600

[env:ota]
extends         = base
upload_protocol = espota
upload_port     = 192.168.4.1
upload_flags    = --auth=duett1964
```

> **Why pioarduino?** The standard `espressif32` platform uses IDF 4.4 which hangs during OPI PSRAM init. pioarduino 51.03.07 uses IDF 5.x which initialises PSRAM correctly.

> **NimBLE-Arduino version:** `h2zero/NimBLE-Arduino @ ^2.0.0` is required for IDF 5.x. Version 1.4.x compiles but crashes at runtime on this platform.

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
┌─────────────────────────────────────────────────────────────────────────────┐
│                             ScreenManager                                    │
│          touch polling · page switching · nav bar                            │
├────────┬──────────┬─────────┬─────────┬─────────┬──────────┬───────────────┤
│ Screen │ Screen   │ Screen  │ Screen  │ Screen  │ Screen   │ Screen        │
│ Dash   │ Ignition │ Vehicle │ GPS     │ Storage │ Settings │ Cube          │
├────────┴──────────┴─────────┴─────────┴─────────┴──────────┴───────────────┤
│              Widget  (dataRow · hRule · vRule · sectionLabel)                │
├─────────────────────────────────────────────────────────────────────────────┤
│                              VehicleData                                     │
│    updated by: IgnitionBT (RPM/advance/temp/volt) · GPS reader · sim        │
├─────────────────────────────────────────────────────────────────────────────┤
│     IgnitionBT (NimBLE central)        Logger (CSV/SD · seq/LittleFS)        │
├─────────────────────────────────────────────────────────────────────────────┤
│                    display.h / LovyanGFX / RGB LCD panel                     │
└─────────────────────────────────────────────────────────────────────────────┘
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
│   ├── vehicle_data.h          — VehicleData struct (engine · GPS · ignition fields)
│   ├── vehicle_data.cpp        — global vdata instance
│   ├── sim.h                   — SIM_ENABLE compile flag + sim_set()/sim_running() API
│   ├── sim.cpp                 — synthetic drive-cycle; runtime-togglable via sim_set()
│   ├── logger.h                — logger_init/update/enable/query API
│   └── logger.cpp              — CSV logger: LittleFS sequence counter + SD file I/O
│
├── display/
│   ├── display.h               — extern LGFX* display
│   └── display.cpp             — display_init()
│
├── sensors/
│   ├── gps_reader.h            — gps_init() · gps_update()
│   ├── gps_reader.cpp          — Adafruit_GPS on UART0, populates vdata + timestamp
│   ├── ignition_bt.h           — ignition_bt_init/update/state/send_* API
│   └── ignition_bt.cpp         — NimBLE central client for 123-ignition TUNE+
│
├── net/
│   ├── wifi_log.h              — wlog() declaration + WIFI_LOG_SSID / PASS
│   ├── wifi_log.cpp            — WiFi AP + ring-buffer HTTP log server
│   ├── ota.h                   — OTA_HOSTNAME / OTA_PASSWORD
│   └── ota.cpp                 — ArduinoOTA with wlog() progress callbacks
│
├── ui/
│   ├── screen.h                — abstract Screen base class
│   ├── screen_manager.h/.cpp   — page registry · touch routing · nav bar
│   ├── screen_dash.h/.cpp      — Dashboard: speed + RPM dials with extras
│   ├── screen_ignition.h/.cpp  — 123-ignition data display + advance/tune controls
│   ├── screen_vehicle.h/.cpp   — two-column engine / fuel readout
│   ├── screen_gps.h/.cpp       — GPS speed · heading · position
│   ├── screen_storage.h/.cpp   — SD status · logging toggle · file info
│   ├── screen_settings.h/.cpp  — runtime settings (simulation toggle)
│   ├── screen_cube.h/.cpp      — rotating wireframe cube demo
│   └── widgets.h/.cpp          — shared stateless drawing helpers
│
└── fs/
    ├── storage.h/.cpp          — LittleFS helpers (init · read · write · remove)
    └── sd_storage.h/.cpp       — SD card helpers  (init · read · write · remove)

include/
└── lgfx_user.h                 — LovyanGFX Panel_RGB + Light_PWM + Touch_GT911

data/                           — LittleFS root (pio run -t uploadfs)
partitions_ota.csv              — custom 4 MB OTA partition layout
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
| `timestamp[24]` | `char[]` | ISO-8601 string when GPS fix valid (`"2025-06-01T12:34:56"`); `"T+Xs"` from `millis()/1000` otherwise |
| `ign_advance_deg` | `float` | Ignition advance (° BTDC) from 123-ignition BLE |
| `ign_temp_c` | `float` | Ignition module temperature (°C) |
| `ign_voltage_v` | `float` | Supply voltage seen by ignition module (V) |
| `ign_pressure_kpa` | `float` | MAP/vacuum from ignition sensor (kPa absolute) |
| `ign_ampere` | `float` | Coil current (A) |
| `ign_connected` | `bool` | BLE link to ignition module is active |
| `ign_tune_mode` | `bool` | Tuning mode active on the ignition module |

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
    void setNeedsRedraw()        { _needsRedraw = true; }
    bool needsFullRedraw() const { return _needsRedraw; }
protected:
    bool _needsRedraw = true;
};
```

- `init()` — called once on first activation; allocate sprites here.
- `update()` — called at the render rate (`RENDER_INTERVAL_MS`); `gfx` is either a manager-level sprite or the display directly.
- `onTouch()` — called every `loop()` iteration while a finger is in the content area; coordinates are relative to the content area top-left.

#### `_needsRedraw` lifecycle

`ScreenManager::goTo()` calls `setNeedsRedraw()` on the incoming page. The page's `update()` checks the flag to decide between a **full render** (clear background, draw all static labels, draw current values) or a **partial update** (erase and redraw only cells whose value changed since last frame).

`ScreenManager` reads `needsFullRedraw()` *before* calling `update()` so it can redraw the nav bar after any full-screen clear. A page clears `_needsRedraw` inside `update()` once the full render is done.

> **Critical rule for continuous-render pages:** A page such as `ScreenCube` that redraws its entire region every frame must set `_needsRedraw = false` at the **very start** of `update()`. If it does not, `needsFullRedraw()` returns `true` every frame and the `ScreenManager` redraws the nav bar every frame, causing visible flickering.

#### `fillRect` vs `fillScreen`

All pages clear their area with `gfx.fillRect(0, 0, contentW, contentH, TFT_BLACK)` — **never** `fillScreen`. In direct-draw mode `gfx` is the physical display; `fillScreen` would overwrite the nav bar area below the content region.

### Adding a new page

1. Create `src/ui/screen_foo.h/.cpp` inheriting `Screen`.
2. Override `name()` and `update()`. Use `vdata` fields and `Widget::` helpers.
3. In `update()`:
   - If `_needsRedraw` is true: clear with `gfx.fillRect(0, 0, contentW, contentH, TFT_BLACK)`, draw all static labels, draw current values, set `_needsRedraw = false`.
   - Otherwise: erase and redraw only cells whose value changed (partial update pattern).
   - **Exception — continuous-render pages** (pages that redraw everything every frame, like `ScreenCube`): set `_needsRedraw = false` at the very top of `update()` before any drawing so the manager does not misinterpret every frame as a full-screen clear.
4. In `main.cpp`: add `static ScreenFoo screenFoo;` and `mgr.addPage(&screenFoo);`.

---

### `ScreenManager`  (`src/ui/screen_manager.h`)

```cpp
bool addPage(Screen* s);   // register before begin()
void begin();              // call after display_init()
void update();             // call every loop() iteration
```

Each `update()` call is split into two phases with independent rates:

| Phase | Rate | Purpose |
|---|---|---|
| Touch polling | Every `loop()` call (full speed) | Instant tap/swipe response |
| Render | `RENDER_INTERVAL_MS` (default 100 ms = 10 Hz) | Page drawing, sprite push, nav bar |

Pages are lazy-initialised (`init()` on first visit). Attempts to allocate a full content-area sprite (800×432, ~675 KB) for flicker-free rendering; falls back to direct draw if allocation fails.

#### Render rate

`RENDER_INTERVAL_MS` is defined in `screen_manager.h`. At 100 ms (10 Hz):
- Dashboard dials and numbers update smoothly (≤ ~2 km/h step per frame with the simulator)
- WiFi, OTA, and GPS processing get more CPU time because `loop()` returns quickly on non-render iterations
- `ScreenCube` animates at 10 fps — visible steps, but it is a demo page

Decrease toward 40 ms (25 Hz) if animation needs to be smoother; increase toward 200 ms (5 Hz) to further reduce draw work.

#### Touch debounce

Raw GT911 touch events are asserted for many consecutive frames. Two guards prevent accidental double-navigation:

1. **Rising-edge detection** — `_touchActive` tracks whether a finger was already down last call. Navigation fires only on the first call of a new touch (`risingEdge = touching && !_touchActive`).
2. **Time cooldown** — `NAV_COOLDOWN_MS` (defined in `screen_manager.h`) is the minimum milliseconds between page switches. Increase it if double-tap still occurs; decrease it for snappier response.

Content-area touches (`ty < contentH`) are forwarded to the active page on every `loop()` call so drags remain smooth regardless of render throttling.

#### Nav bar render budget (`_navDirty`)

`drawNavBar()` is called only when `_navDirty` is true **or** the page just did a full-screen clear (detected via `needsFullRedraw()` polled before `update()`). In steady state — no page switch, no full redraw — the nav bar is never touched, which eliminates its flicker during animation.

The page name and page counter are rendered in **DejaVu18**, vertically centred in the 48 px nav bar using `fontHeight()` for precise placement.

---

### Typography

All text is rendered with LovyanGFX's built-in **DejaVu anti-aliased fonts**. These are stored as pre-rendered glyph bitmaps with proper anti-aliasing — a large improvement over the default 6×8 bitmap font scaled with `setTextSize()`.

| Font constant | Approx. height | Used for |
|---|---|---|
| `lgfx::fonts::DejaVu9` | 9 px | Small hints (e.g. "x1000 rpm", cube label) |
| `lgfx::fonts::DejaVu12` | 12 px | Extra-cell labels (Throttle [%], MAP [kPa] …) |
| `lgfx::fonts::DejaVu18` | 18 px | Page headers, labels, units, nav bar, section headings |
| `lgfx::fonts::DejaVu24` | 24 px | GPS coordinate lines, GPS unit labels |
| `lgfx::fonts::DejaVu40` | 40 px | Dashboard dial numbers, vehicle data values |
| `lgfx::fonts::DejaVu56` | 56 px | GPS large speed display |

Always call `gfx.setTextSize(1)` after `gfx.setFont(...)` — the size multiplier still applies and must be 1 when using named fonts.

Every drawing function that uses `setTextSize()` sets its own font first, so there is no implicit dependency on font state left by a previous page or call. Proportional fonts (all DejaVu fonts) have variable character widths; text-area clears must use the **maximum possible** string width, not the current one, to avoid ghost digits when a longer number is replaced by a shorter one. `drawNumber()` achieves this by pre-measuring the formatted `maxVal` string.

---

### `Widget` namespace  (`src/ui/widgets.h`)

Stateless helpers; all functions take `lgfx::LovyanGFX& gfx` so they work on any render target. Each function sets its own font so it is safe to call from any page regardless of prior font state.

The two-column layout is supported via the `col` parameter (`0` = left, `1` = right). Column x-offset is `col × COL_W` where `COL_W = 400 px`.

| Function | Font | Description |
|---|---|---|
| `Widget::dataRow(gfx, y, label, value, unit, color, col)` | Label/unit: DejaVu18 · Value: DejaVu40 | Full data row; row height `ROW_H = 80 px`. Label at `dx+20`, value right-aligned to `dx+360`, unit at `dx+368`. |
| `Widget::updateRowValue(gfx, rowY, stored, sz, newVal, color, col)` | DejaVu40 | Partial update: erases value area (`dx+190`, width 175) and redraws only if `newVal` differs. |
| `Widget::hRule(gfx, y, w)` | — | Thin horizontal separator spanning `w − 40` px. |
| `Widget::vRule(gfx, x, h)` | — | Thin vertical separator at `x`, from y=10 to y=h−10. Used as the column divider. |
| `Widget::sectionLabel(gfx, x, y, text)` | DejaVu18 | Small gray section heading. |

---

## Pages

### Page order

| # | Screen | Name |
|---|---|---|
| 1 | `ScreenDash` | Dashboard |
| 2 | `ScreenIgnition` | Ignition |
| 3 | `ScreenVehicle` | Vehicle |
| 4 | `ScreenGPS` | GPS |
| 5 | `ScreenStorage` | Storage |
| 6 | `ScreenSettings` | Settings |
| 7 | `ScreenCube` | Cube Demo |

---

### ScreenDash  —  `"Dashboard"` (page 1)

Two analogue dials side-by-side with a centre column of scalar extras.

#### Layout (800 × 432 content area)

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Speed                         │                        Engine RPM      │
│                                │                                        │
│         ┌─────────┐       Throttle [%]        ┌─────────┐              │
│         │  dial   │        Fuel [L/100km]      │  dial   │              │
│         │  140    │        MAP [kPa]           │  6000   │              │
│         └─────────┘                            └─────────┘              │
│        cx=175, cy≈216         cx=400          cx=625, cy≈216            │
└─────────────────────────────────────────────────────────────────────────┘
```

| Element | Value |
|---|---|
| Dial radius | 140 px |
| Speed dial centre | (175, 226) |
| RPM dial centre | (625, 226) |
| Centre column x | 400 |
| Extras top y | cy − 75 = 151 |
| Extra row spacing | 55 px |
| Gauge arc sweep | 270° (7:30-o'clock → 4:30-o'clock, CW) |

#### Angle conventions

LovyanGFX `fillArc` and the custom `ptAt()` helper use **different** zero-angle origins:

| Convention | Zero direction | Used for |
|---|---|---|
| `ptAt` (dial) | 12-o'clock, CW | Tick line endpoints, label positions |
| `fillArc` (GFX) | 3-o'clock, CW | Coloured arc and background track |

Offset between them is exactly **90°** (subtracting 90° from the dial angle gives the GFX angle).

| Constant | Value | Meaning |
|---|---|---|
| `DIAL_START` | 225° | Gauge zero (7:30-o'clock) in ptAt convention |
| `ARC_START` | 135° | Same position in fillArc convention (225 − 90) |
| `DIAL_SWEEP` | 270° | Full-scale arc span |

#### Speed dial

| Property | Value |
|---|---|
| Range | 0 – 140 km/h |
| Major ticks / labels | every 20 km/h |
| Minor ticks | every 10 km/h |
| Colour (normal) | Cyan `0x00BFFF` |
| Warning zone | ≥ 72% of scale ≈ 101 km/h → amber |
| Red zone | ≥ 86% of scale ≈ 120 km/h → red |
| Centre number | Current speed, size-5 font, km/h unit |

#### RPM dial

| Property | Value |
|---|---|
| Range | 0 – 6000 rpm |
| Major ticks / labels | every 1000 rpm (shown as `x1000 rpm`) |
| Minor ticks | every 500 rpm |
| Colour (normal) | Green `0x00FF88` |
| Warning zone | ≥ 75% of scale ≈ 4500 rpm → amber |
| Red zone | ≥ 92% of scale ≈ 5500 rpm → red |
| Centre number | Current RPM, size-5 font, rpm unit |

#### Centre column extras

Three `updateExtra()` cells stacked vertically, redrawn only when label or value string changes:

| Cell | Label | Source | Colour |
|---|---|---|---|
| Top | `Throttle [%]` | `vdata.throttle_pct` | Cyan |
| Middle | `Fuel [L/100km]` or `Fuel [L/h]` | `vdata.fuel_per_100km` / `vdata.fuel_flow_lph` | Orange |
| Bottom | `MAP [kPa]` | `vdata.map_kpa` | Orange |

Fuel label switches automatically: `L/100km` when GPS fix is valid and speed > 2 km/h, otherwise `L/h`.

#### Arc rendering — delta-sector update

The arc is redrawn using an incremental strategy to eliminate flicker. The old approach (erase the entire 270° gray track, paint the colored arc on top) caused a visible gray flash on every update. The new approach paints only the sector that changed:

| Condition | Action |
|---|---|
| First draw after page entry (`prevVal < 0`) | Full redraw: gray track + colored arc + all ticks |
| Color zone changed (crossed warn/red threshold) | Full redraw (rare event) |
| Value increased | `fillArc(prevAngle → newAngle, color)` + redraw ticks in that sliver only |
| Value decreased | `fillArc(newAngle → prevAngle, gray)` + redraw ticks in that sliver only |

`drawTicksInRange()` filters the tick loop to only the affected angular sector, so a typical frame draws 0–2 tick lines instead of all 27.

The arc is also gated on a minimum angle change before any redraw is attempted:

| Constant | Value | Meaning |
|---|---|---|
| `ARC_THRESHOLD` | 1.0° | Minimum arc angle change before redraw |
| `ARC_MIN_MS` | 40 ms | Floor on arc redraw interval (superseded in practice by `RENDER_INTERVAL_MS`) |

The centre number (`drawNumber`) uses **DejaVu40** for the value and **DejaVu18** for the unit, stacked vertically and centred on the dial centre using measured `fontHeight()`. The erase rectangle width is derived from the **maximum possible value** (`maxVal` passed by the caller) rather than the current value, so that a shorter number (e.g. "850") always fully erases a previously drawn wider number (e.g. "3500").

---

### ScreenStorage  —  `"Storage"` (page 5)

Shows SD card health, storage capacity, and logging controls on a single screen.

```
┌─────────────────────────────────────────────────────────┐
│  SD Card                                                │  ← sectionLabel y=15
│  ●  SD OK  512 MB                       (or No SD)      │  y=45
│     Used: 12.4 MB                                       │  y=72
├─────────────────────────────────────────────────────────┤  y=100
│  Data Logging                                           │  y=118
│  ┌──────────────────────────────────┐                   │
│  │  ● LOGGING ON  (or ○ LOGGING OFF)│  toggle button    │  y=155
│  └──────────────────────────────────┘                   │
│  LOG_0003.CSV  (4.2 KB)                                 │  y=218
│  1247 records                                           │  y=248
└─────────────────────────────────────────────────────────┘
```

| Element | Detail |
|---|---|
| SD status dot | Green = card present and mounted; red = no card |
| SD size string | Total capacity in KB / MB / GB from `sd_total_bytes()` |
| Used string | `sd_used_bytes()` formatted as KB / MB / GB |
| Toggle button | `BTN_X=280, BTN_Y=155, BTN_W=240, BTN_H=50` — green (ON) or gray (OFF) |
| Log filename | `logger_filename()` — e.g. `LOG_0003.CSV` |
| File size | `logger_file_bytes()` formatted to KB / MB |
| Record count | `logger_records()` — rows written since last `logger_init()` |
| Debounce | 400 ms between button presses |

No sprite — draws directly on `gfx`.

**Render strategy:** full render on `_needsRedraw`; partial update for button state (`_prevEnabled`), record count (`_prevRecords`), and SD presence (`_prevSdOk`) — only changed elements are erased and redrawn.

**Touch:** `onTouch()` checks if the touch point falls inside the button rectangle and toggles logging via `logger_enable(bool)`.

---

### ScreenSettings  —  `"Settings"` (page 6)

Runtime configuration. Currently contains one setting: the simulation toggle.

```
┌─────────────────────────────────────────────────────────┐
│  Settings                                               │
├─────────────────────────────────────────────────────────┤
│  Sensor Simulation                                      │
│         ┌──────────────────────────────┐               │
│         │  · SIM ON  (or  · SIM OFF)  │  toggle button │
│         └──────────────────────────────┘               │
│  [description of active/inactive behaviour]             │
├─────────────────────────────────────────────────────────┤
│  RPM source                                             │
│  Sim ON  » synthetic drive cycle                        │
│  Sim OFF + BLE connected  » RPM from 123-ignition       │
│  Sim OFF + no BLE  » RPM = 0                            │
└─────────────────────────────────────────────────────────┘
```

| Element | Detail |
|---|---|
| Toggle button | `BTN_X=260, BTN_Y=115, BTN_W=280, BTN_H=60` — green (SIM ON) or gray (SIM OFF) |
| Description | Explains effect of current state; updates on toggle |
| RPM source table | Static text showing all three RPM source scenarios |
| Debounce | 400 ms |

**Touch:** `onTouch()` calls `sim_set(!sim_running())` when tap lands inside the button rectangle.

No sprite — draws directly on `gfx`.

---

### ScreenCube  —  `"Cube Demo"` (page 7)

Rotating wireframe 3D cube, touch to move. Uses a 300×300 sprite (~175 KB internal heap) centred in the content area. Falls back to direct draw if allocation fails.

Projection: `scale = 180 / (z + 5)` → vertices project ≈ ±95 px from centre.

**Render strategy:** redraws everything every frame (clear → draw edges → push sprite). Sets `_needsRedraw = false` at the very start of `update()` so `ScreenManager` never sees a persistent "full redraw" signal and does not redraw the nav bar every frame.

### ScreenIgnition  —  `"Ignition"` (page 2)

Dedicated page for the 123-ignition TUNE+ module: live data display and in-car tuning controls.

```
┌─────────────────────────────────────────────────────────────────────────┐
│  ● Connected          (or ○ Scanning... / Connecting...)  123-ignition  │  y=0–40
├─────────────────────────────────────────────────────────────────────────┤  y=40
│  RPM                            │  Advance                              │
│  [DejaVu56, green]              │  [DejaVu40, cyan]                     │  y=68
│  rpm                            │  ° BTDC                               │
├─────────────────────────────────┼───────────────────────────────────────┤  y=165
│  Temperature                    │  Voltage                              │
│  [DejaVu40, orange]             │  [DejaVu40, yellow]                   │  y=194
│  °C                             │  V                                    │
├─────────────────────────────────┴───────────────────────────────────────┤  y=278
│  MAP  [DejaVu24, white]  kPa                                            │
├─────────────────────────────────────────────────────────────────────────┤  y=316
│  [− ADV]         [TUNE MODE]            [ADV +]                         │  y=316–432
└─────────────────────────────────────────────────────────────────────────┘
```

| Element | Detail |
|---|---|
| Status dot | Green = connected; amber = scanning/connecting/handshaking; gray = idle |
| RPM | `vdata.rpm` — written by ignition BLE when sim is off |
| Advance | `vdata.ign_advance_deg` (° BTDC) |
| Temperature | `vdata.ign_temp_c` (°C) |
| Voltage | `vdata.ign_voltage_v` (V) |
| MAP | `vdata.ign_pressure_kpa` (kPa) |
| `− ADV` button | Sends advance − command; repeats at 350 ms while held |
| `TUNE MODE` button | Toggles tune mode on/off; 600 ms debounce; turns amber when active |
| `ADV +` button | Sends advance + command; repeats at 350 ms while held |

No sprite — draws directly on `gfx`.

**Render strategy:** full render on `_needsRedraw`; partial update per value field (each erased and redrawn only when its formatted string changes); status dot and text redrawn only when connection state changes; TUNE MODE button redrawn only when `vdata.ign_tune_mode` changes.

---

### ScreenVehicle  —  `"Vehicle"` (page 3)

Seven `Widget::dataRow` rows split across two columns, centred vertically in the 432 px content area.

```
┌──────────────────────────────┬──────────────────────────────┐
│  Left column (col=0)         │  Right column (col=1)        │
├──────────────────────────────┼──────────────────────────────┤
│  RPM          [rpm]  green   │  Fuel flow    [L/h]  red     │
│  Throttle     [%]    cyan    │  Economy   [L/100km] orange  │
│  MAP          [kPa]  orange  │  Fuel used    [L]    orange  │
│  Ambient      [kPa]  orange  │                              │
└──────────────────────────────┴──────────────────────────────┘
           x=400: vRule divider
```

| Parameter | Value |
|---|---|
| Column width (`COL_W`) | 400 px |
| Row height (`ROW_H`) | 80 px |
| Rows per column | 4 left / 3 right |
| Vertical start (`_startY`) | `(contentH − 4 × ROW_H) / 2` |
| Column divider | `Widget::vRule` at x = 400 |

No sprite — draws directly on `gfx`.

**Render strategy:** on `_needsRedraw` performs a full render (background + vRule divider + all labels, units, values); on subsequent frames calls `Widget::updateRowValue()` for each row, which erases and redraws the value cell only when the formatted string changes.

### ScreenGPS  —  `"GPS"` (page 4)

```
┌──────────────────────┬──────────────────────────┐
│  Speed               │  Heading                 │
│  (DejaVu56, green)   │  degrees + compass point │
│  km/h                │  (DejaVu40, cyan / amber) │
├──────────────────────┴──────────────────────────┤
│  Latitude   DD° MM.MMMM' N/S   (DejaVu24)       │
│  Longitude  DD° MM.MMMM' E/W   (DejaVu24)       │
│  Altitude   NNN m              (DejaVu18)        │
├─────────────────────────────────────────────────┤
│  L/100km (moving) or L/h (stationary)  ● fix   │
└─────────────────────────────────────────────────┘
```

No sprite — draws directly on `gfx`.

**Render strategy:** `drawStatic()` draws section labels, unit strings, dividers and separators once (called only when `_needsRedraw`). Dynamic fields (speed, heading, compass point, lat, lon, altitude, fuel, GPS dot) are updated each frame via `updateField()` / `updateFieldRight()` helpers that erase and redraw a field's bounding rectangle only when its formatted string value changes. The erase height uses `max(hardcodedH, fontHeight())` so it always covers the full glyph even if the nominal height constant predates a font change.

---

## Sensor Simulation

A synthetic drive cycle animates `vdata` so the dashboard can be tested without a running engine or GPS fix.

### Enabling / disabling

Two independent controls:

**Compile-time flag** (`src/data/sim.h`):

```c
#define SIM_ENABLE 1   // 1 = compile sim code,  0 = compile away entirely
```

When `SIM_ENABLE` is 0 the entire `sim.cpp` translation unit is skipped and all sim functions become empty inlines — zero code size, zero runtime cost.

**Runtime toggle** (Settings page or programmatic):

```cpp
sim_set(false);   // pause simulation; real/BLE data takes effect immediately
sim_set(true);    // resume simulation
bool active = sim_running();
```

The Settings page (page 6) exposes this toggle as a touch button. The default runtime state is `true` (simulation runs on boot when `SIM_ENABLE 1`).

`sim_update()` is called in `loop()` **after** `ignition_bt_update()`. When `sim_running()` is true it overwrites all `vdata` fields (including `rpm`). When false it returns immediately, leaving sensor and BLE data untouched.

**RPM source priority:**

| Condition | `vdata.rpm` source |
|---|---|
| `sim_running() == true` | Synthetic drive cycle (overwrites everything) |
| `sim_running() == false` AND `ign_connected == true` | 123-ignition BLE (written in notify callback) |
| `sim_running() == false` AND no BLE | Remains at last BLE value or 0 |

### Drive cycle

```
 0 s ──────────────────── 5 s ── 5.5 s ──────────────── 10.5 s → repeat
      accelerate 0→100 km/h      hold        decelerate 100→0 km/h
```

| Constant | Value | Meaning |
|---|---|---|
| `ACCEL_MS` | 5000 ms | Acceleration phase |
| `HOLD_MS` | 500 ms | Constant-speed phase |
| `DECEL_MS` | 5000 ms | Deceleration phase |
| `CYCLE_MS` | 10 500 ms | Total period |
| `MAX_SPEED` | 100 km/h | Peak speed |
| `IDLE_RPM` | 850 rpm | Idle (speed = 0) |
| `MAX_RPM` | 3500 rpm | At 100 km/h |

Easing uses **smoothstep** (`t² × (3 − 2t)`) so the dials ease in and out rather than stepping linearly.

### Fields updated by the simulator

| Field | Behaviour |
|---|---|
| `speed_kmh` | Smoothstep 0 → 100 → 0 km/h |
| `rpm` | Linear with speed: 850 + (speed/100) × 2650 |
| `throttle_pct` | Builds during acceleration (20–85 %), 30 % at hold, 0 % on decel |
| `map_kpa` | 45 + throttle × 0.6 when throttle > 0, else 32 kPa |
| `fuel_flow_lph` | 1.5 + throttle × 0.12 when throttle > 0, else 0.8 L/h |
| `fuel_per_100km` | Calculated from flow/speed when speed > 2 km/h |
| `gps_valid` | Always `true` |
| `lat` / `lon` | Fixed at Stockholm (59.3293° N, 18.0686° E) |
| `altitude_m` | 28 m |
| `heading_deg` | 45° |
| `timestamp` | `"T+Xs"` from `millis()/1000` (no GPS hardware in sim) |

---

## 123-ignition BLE  (`src/sensors/ignition_bt.h/.cpp`)

Integrates the **123ignition TUNE+** electronic ignition module directly into the dashboard via BLE. The module streams RPM, ignition advance, temperature, voltage, MAP pressure, and coil current as continuous BLE notifications. The ESP32-S3 acts as the BLE **central** (client); the ignition module is the peripheral. BLE and the WiFi AP coexist using the ESP32's built-in radio coexistence mode.

### BLE identifiers

The 123ignition TUNE+ exposes a **proprietary 128-bit service** (not Nordic UART, despite a previous theory). UUIDs below are taken from the published, working ESP32 simulator [iafilius/123Tune-plus-Simulator](https://github.com/iafilius/123Tune-plus-Simulator), which is accepted byte-for-byte by the official iOS 123Tune+ app and is therefore authoritative.

| Item | UUID | Properties |
|---|---|---|
| Primary service | `da2b84f1-6279-48de-bdc0-afbea0226079` | — |
| Info characteristic | `99564a02-dc01-4d3c-b04e-3bb1ef0571b2` | read |
| Body characteristic | `a87988b9-694c-479c-900e-95dfa6c00a24` | read/write |
| **RX (commands → module)** | `bf03260c-7205-4c25-af43-93b1c299d159` | write |
| **TX (notifications → host)** | `18cda784-4bd3-4370-85bb-bfed91ec86af` | notify |
| Battery service | `0x180F` (standard) | — |
| Battery level | `0x2A19` (standard) | read |

The scan matches by service UUID first; also accepts any advertising device whose name contains `"123"` as a fallback.

### Connection state machine

```
IDLE ──scan start──▶ SCANNING ──device found──▶ CONNECTING
                                                      │ (FreeRTOS task, non-blocking)
                                                      ▼
                                               HANDSHAKING ──3 commands──▶ ACTIVE
                                                      │                        │
                                               disconnect/fail          disconnect
                                                      │                        │
                                                      └──────── IDLE ◀─────────┘
                                                           (retry after 8 s)
```

The connect attempt runs in a short-lived FreeRTOS task so `loop()` stays responsive during the ~1–3 s BLE connection window.

### Handshake sequence

> **Source:** the six-step handshake below is documented in `iafilius/123Tune-plus-Simulator` (`ESP32-Arduino/ESP32_123Tune_plus_server/ble.ino`). The simulator's own comment is the smoking gun: *"All Meter values show up after command 6 !!! — Service command 'v@' (Version Service Command) returns voltage, Temp, pressure and hardware/software version."* The previous 3-step handshake stopped at step 3 and is the reason no notifications were received.

After subscribing to TX notifications, six commands are written to the RX characteristic in order with ~300 ms gaps. Live data only begins after step 6.

| Step | Bytes | ASCII | Purpose |
|---|---|---|---|
| 1 | `0D` | `\r` | Keepalive / sync |
| 2 | `31 30 40 0D` | `10@\r` | Advance curve points 1–7 (RPM/degrees) |
| 3 | `31 31 40 0D` | `11@\r` | Advance curve points 8–10 + PIN + RPM limit |
| 4 | `31 32 40 0D` | `12@\r` | Vacuum/MAP curve points 1–7 |
| 5 | `31 33 40 0D` | `13@\r` | Vacuum/MAP curve points 8–10 |
| 6 | `76 40 0D` | `v@\r` | **Version + live data trigger** — starts streaming |

The Info and Body characteristics are read once before step 1, mimicking the iOS app (some firmware revisions gate the streaming on these reads).

A periodic keepalive (`0x0D`) is sent every 20 s while ACTIVE to prevent the device's idle timeout. Padding bytes `0x24` may appear on either side of any command and are stripped by the parser.

### Packet format

Every notification is a stream of 5-byte value packets, fragmented into 20-byte MTU chunks:

```
[0]  cmd     — identifies the value type
[1]  MSB     — single ASCII hex character ('0'–'F')
[2]  LSB     — single ASCII hex character ('0'–'F')
[3]  csum    — cmd + 0x10 + (MSB−'0') + (LSB−'0')
[4]  0x20 or 0x0D  — packet terminator
```

`0x24` padding bytes (keepalive filler) are silently discarded before parsing.

### Value decoding

| `cmd` | Value | Decode formula |
|---|---|---|
| `0x30` | RPM | `nibble(MSB) × 800 + nibble(LSB) × 50` |
| `0x31` | Advance (° BTDC) | `nibble(MSB) × 3.2 + nibble(LSB) × 0.2` |
| `0x32` | MAP/vacuum (kPa) | `hexByte(MSB, LSB)` |
| `0x33` | Temperature (°C) | `hexByte(MSB, LSB) − 30` |
| `0x34` | Tune mode flag | `LSB == '1'` |
| `0x35` | Current (A) | `hexByte(MSB, LSB) / (16 / 1.85)` |
| `0x41` | Voltage (V) | `hexByte(MSB, LSB) / (0x40 / 14.1)` |

`nibble(c)` converts a single ASCII hex char to 0–15. `hexByte(hi, lo)` combines two ASCII hex chars into a 0–255 byte value.

### Tune controls

Three commands can be written to the RX characteristic at any time during ACTIVE state:

| Function | Byte | Effect |
|---|---|---|
| `ignition_send_advance_plus()` | `0x61` (`'a'`) | +0.2° advance in tune mode |
| `ignition_send_advance_minus()` | `0x72` (`'r'`) | −0.2° advance in tune mode |
| `ignition_send_tune_toggle()` | `0x74` (`'t'`) | Enter / exit tune mode |

The ScreenIgnition touch buttons call these functions with debounce (350 ms repeat for ± advance, 600 ms one-shot for tune toggle).

### API

| Function | Description |
|---|---|
| `ignition_bt_init()` | Initialise NimBLE stack; call in `setup()` after `wifi_log_init()` |
| `ignition_bt_update()` | Drive state machine; call every `loop()` |
| `ignition_bt_state()` | Returns `IgnBtState` enum |
| `ignition_bt_state_str()` | Returns human-readable string (e.g. `"Connected"`, `"Scanning..."`) |
| `ignition_send_advance_plus/minus()` | Write advance control command |
| `ignition_send_tune_toggle()` | Write tune mode toggle command |

---

## Data Logging  (`src/data/logger.h/.cpp`)

The logger writes one CSV row per second to an SD card file. It is enabled by default at boot (if an SD card is present) and can be toggled at runtime from the Storage page.

### Sequence numbering

A 16-bit sequence counter is stored in `/logseq.txt` on LittleFS. At every `logger_init()` call the counter is read, incremented, and written back, then the SD file is opened as `/LOG_NNNN.CSV` (zero-padded to 4 digits). Files are never overwritten between reboots even after a crash; the next boot creates a new file with the next sequence number.

### Initialisation

`logger_init()` is called in `setup()` after `sd_init()`. It:

1. Reads `/logseq.txt` from LittleFS (creates it at 0 if absent).
2. Writes `seq + 1` back to persist the new sequence number.
3. Opens `/LOG_NNNN.CSV` on SD with `FILE_WRITE`.
4. Writes the CSV header row.

### API

| Function | Description |
|---|---|
| `logger_init()` | Open next numbered CSV file and write header |
| `logger_update()` | Write one row per `LOG_INTERVAL_MS`; call every `loop()` |
| `logger_enable(bool)` | Pause / resume logging at runtime |
| `logger_enabled()` | Returns current state |
| `logger_sequence()` | Current file sequence number |
| `logger_records()` | Rows written to current file |
| `logger_filename()` | Current filename, e.g. `"LOG_0003.CSV"` |
| `logger_file_bytes()` | Current file size in bytes |

`LOG_INTERVAL_MS` is defined in `logger.h` (default: 1000 ms = 1 row/sec). The file is flushed to SD every 10 records to reduce wear while keeping data loss risk low.

### CSV format

```
seq,timestamp,speed_kmh,rpm,throttle_pct,map_kpa,ambient_kpa,fuel_flow_lph,fuel_per_100km,fuel_used_l,gps_valid,lat,lon,altitude_m,heading_deg
```

| Column | Format | Source |
|---|---|---|
| `seq` | integer | Row number within the file |
| `timestamp` | string | `vdata.timestamp` — see [Clock Synchronisation](#clock-synchronisation) |
| `speed_kmh` | `%.1f` | `vdata.speed_kmh` |
| `rpm` | `%.0f` | `vdata.rpm` |
| `throttle_pct` | `%.1f` | `vdata.throttle_pct` |
| `map_kpa` | `%.1f` | `vdata.map_kpa` |
| `ambient_kpa` | `%.1f` | `vdata.ambient_kpa` |
| `fuel_flow_lph` | `%.2f` | `vdata.fuel_flow_lph` |
| `fuel_per_100km` | `%.1f` | `vdata.fuel_per_100km` |
| `fuel_used_l` | `%.3f` | `vdata.fuel_used_l` |
| `gps_valid` | `0` / `1` | `vdata.gps_valid` |
| `lat` | `%.6lf` | `vdata.lat` |
| `lon` | `%.6lf` | `vdata.lon` |
| `altitude_m` | `%.1f` | `vdata.altitude_m` |
| `heading_deg` | `%.1f` | `vdata.heading_deg` |

---

## Clock Synchronisation

Accurate timestamps in log files require a time source. The options below are listed from most to least accurate.

| Option | Accuracy | Status | Notes |
|---|---|---|---|
| **GPS time** (NMEA `$GPRMC`) | ±1 s | **Implemented** | Adafruit_GPS parses date/time from each NMEA sentence. Populated into `vdata.timestamp` as ISO-8601 once `GPS.fix && GPS.year > 0`. |
| **Phone via HTTP POST** | ±1–2 s | Possible | Phone connects to the `DuettGUI` AP and POSTs the current epoch to an endpoint on the ESP. No extra hardware. Requires a small web handler and manual trigger from the phone each drive. |
| **NTP** | ms-level | Not viable | The ESP32 is the AP — it is not a WiFi client, so it cannot reach an NTP server unless a phone acts as a bridge. |
| **DS3231 RTC module** | ±2 min/year | Possible | Battery-backed hardware clock on I2C. Not fitted on this board. Would require adding hardware. |
| **Time since boot** | relative | **Implemented (fallback)** | `vdata.timestamp` is set to `"T+Xs"` (`millis()/1000`) when GPS time is unavailable. Sufficient to correlate rows within one session. |

The current implementation uses GPS time as the primary source and boot-relative time as the fallback. No additional hardware or network setup is required.

---

## WiFi Log Console

Because UART0 is occupied by the GPS, debug output is provided over WiFi instead.

### Access point

The ESP32 starts as an access point on boot:

| Setting | Value |
|---|---|
| SSID | `DuettGUI` (set `WIFI_LOG_SSID` in `wifi_log.h`) |
| Password | none — open network (set `WIFI_LOG_PASS` to change) |
| IP | `192.168.4.1` |
| Port | 80 |

Connect any phone or laptop to the `DuettGUI` network and open `http://192.168.4.1` in a browser.

### API

| Function | Description |
|---|---|
| `wifi_log_init()` | Start AP and HTTP server — call in `setup()` |
| `wifi_log_update()` | Handle pending clients — call every `loop()` |
| `wlog(fmt, ...)` | `printf`-style log; prepends a `[NNN.Ns]` timestamp |

`wlog()` writes into a 100-line ring buffer. The browser polls `/log?since=N` every second and receives only new lines as JSON. No WebSocket library needed.

### Console features

- Dark terminal-style UI, auto-scrolls to latest line
- Timestamp prefix (`[NNN.Ns]`) highlighted in a dimmer colour
- **Pause** checkbox — freezes display without losing buffered lines
- **Clear display** — clears the screen view; history is still in the ring buffer
- Green/grey dot in the header shows live connection state

### Usage

```cpp
#include "net/wifi_log.h"

wlog("RPM = %.0f  throttle = %.1f%%", vdata.rpm, vdata.throttle_pct);
wlog("GPS fix lost");
```

---

## Storage

### LittleFS  (internal flash, ~448 KB partition)

```cpp
storage_init();
storage_write("/settings.json", jsonString);
String s = storage_read("/settings.json");
```

Use for small persistent data: settings, calibration, and the logger sequence counter (`/logseq.txt`). Do **not** use for bulk data — the partition is only 448 KB.

### SD card  (SPI, GPIO 10–13)

```cpp
sd_init();               // optional — system boots without a card
if (sd_available()) {
    sd_write("/log.csv", line);
    String s = sd_read("/config.json");
}
```

Use for data logging and large assets. `sd_init()` calls `wlog()` with card type and capacity. `sd_total_bytes()` and `sd_used_bytes()` expose capacity for the Storage page display.

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

The `ScreenCube` sprite (300×300, 175 KB) is allocated from internal SRAM. `ScreenVehicle`, `ScreenGPS`, `ScreenStorage`, `ScreenIgnition`, and `ScreenSettings` draw directly with no sprite.

The NimBLE stack is initialised by `ignition_bt_init()` and shares the same 2.4 GHz radio as WiFi via the ESP32 coexistence mode. No additional RAM allocation is required beyond the NimBLE stack itself (~30 KB).

---

## OTA (Over-the-Air) Updates

OTA is implemented and ready. No router is needed — the ESP32 runs as its own WiFi AP and accepts firmware uploads directly from a laptop connected to the `DuettGUI` network.

### Flash layout

| Partition | Size | Note |
|---|---|---|
| `app0` / `app1` | 1.75 MB each | Firmware fits comfortably (~1.0–1.3 MB) |
| LittleFS | 448 KB | Use SD card for data logging |

Defined in [partitions_ota.csv](partitions_ota.csv). `ArduinoOTA` is bundled with arduino-esp32 — no extra library entry needed.

### OTA password

Set in two places — they **must match**:

| File | Symbol | Default |
|---|---|---|
| [src/net/ota.h](src/net/ota.h) | `OTA_PASSWORD` | `duett1964` |
| [platformio.ini](platformio.ini) `[env:ota]` | `--auth=` | `duett1964` |

### Uploading from VS Code (PlatformIO)

`platformio.ini` defines two environments:

| Environment | How it uploads | When to use |
|---|---|---|
| `usb` *(default)* | USB / CH340 at 921600 baud | First flash, recovery, GPS disconnected |
| `ota` | `espota` over WiFi to `192.168.4.1` | Board running, laptop on `DuettGUI` network |

#### Method 1 — PlatformIO sidebar (recommended)

1. Open the **PlatformIO** panel in the VS Code activity bar (the ant icon).
2. Expand **Project Tasks**.
3. Expand the **`ota`** environment.
4. Click **Upload**.

PlatformIO compiles the firmware and pushes it wirelessly. Progress appears in the terminal and as `wlog()` entries in the web console.

#### Method 2 — Environment switcher in the status bar

1. Click the environment name in the VS Code **status bar** (bottom, e.g. `env:usb`).
2. Select **`ota`** from the dropdown.
3. Click the **→ Upload** arrow in the status bar (or press the PlatformIO upload shortcut).

#### Method 3 — Integrated terminal

```bash
pio run -e ota -t upload
```

### OTA workflow

1. Make code changes and save.
2. Ensure your laptop is connected to the **`DuettGUI`** WiFi network.
3. Open `http://192.168.4.1` in a browser — you will see log output confirming the board is alive.
4. Trigger an upload using any method above.
5. The web console shows:
   ```
   [NNN.Ns] OTA start: firmware
   [NNN.Ns] OTA 0%
   [NNN.Ns] OTA 10%
   …
   [NNN.Ns] OTA 100%
   [NNN.Ns] OTA complete — rebooting
   ```
6. The board reboots into the new firmware automatically. Refresh the browser after ~5 seconds.

### First-time flash (USB required)

OTA can only update a board that already has OTA-capable firmware. The very first flash, and any recovery from a bad OTA, must be done over USB:

1. Disconnect the GPS module (or at least its RX wire) — see [UART0 note](#uart0-shared-between-gps-and-programming).
2. Hold **BOOT**, press **RESET**, release **BOOT** → download mode.
3. In VS Code: use the `usb` environment → **Upload** (or `pio run -e usb -t upload`).
4. Press **RESET** to boot normally.
5. Reconnect the GPS.

---

## Appendix A — 123-ignition BLE Fault-finding Log

This appendix records the full diagnostic process carried out to integrate the 123ignition TUNE+ over BLE, including every dead end and what each log line revealed. Keep this as a reference if the BLE integration stops working after a firmware update on the ignition module or a NimBLE library upgrade.

---

### A.1  What was found vs. what the original notes said

The initial implementation was based on reverse-engineering notes that turned out to contain incorrect information:

| Item | Original notes | Reality (discovered via diagnostics) |
|---|---|---|
| Service UUID | `da2b84f1-6279-48de-bdc0-afbea0226079` | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` (Nordic UART Service) |
| RX characteristic | `BF03260C-7205-4C25-AF43-93B1C299D159` | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX characteristic | `18CDA784-4BD3-4370-85BB-BFED91EC86AF` | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| NimBLE version | `^1.4.3` | `^2.0.0` required (1.4.x crashes at runtime on IDF 5.x) |

The 123ignition TUNE+ uses the **standard Nordic UART Service (NUS)** — a common BLE serial-over-BLE transport used by many embedded devices. Service `6e400001`, RX `6e400002` (client writes), TX `6e400003` (device notifies).

---

### A.2  NimBLE version issue (display blank + WiFi shows nothing)

**Symptom:** After adding NimBLE to the project, the display went blank and the WiFi console loaded but showed no log lines — as though `setup()` was crashing before `mgr.begin()`.

**Root cause:** `h2zero/NimBLE-Arduino @ ^1.4.3` compiles against IDF 4.x headers but the pioarduino platform runs IDF 5.x. The NimBLE internal structs differ between IDF versions, causing a runtime panic during `NimBLEDevice::init()`.

**Fix:** Change `platformio.ini` to `h2zero/NimBLE-Arduino @ ^2.0.0`.

**API breaking changes between 1.4.x and 2.x:**

| 1.4.x | 2.x |
|---|---|
| `NimBLEAdvertisedDeviceCallbacks` | `NimBLEScanCallbacks` |
| `onResult(NimBLEAdvertisedDevice*)` | `onDiscovered(const NimBLEAdvertisedDevice*)` |
| `scan->setAdvertisedDeviceCallbacks(cb)` | `scan->setScanCallbacks(cb, false)` |
| `onDisconnect(NimBLEClient*)` | `onDisconnect(NimBLEClient*, int reason)` |

---

### A.3  WiFi console showing blank page

**Symptom:** Browser connected to `192.168.4.1` but the page was empty — HTTP 200 returned but body was zero bytes.

**Root cause:** `handleRoot()` used `_srv.send_P(200, "text/html", PAGE)` with a PROGMEM-declared string. In arduino-esp32 3.x on IDF 5.x, `send_P` can silently produce an empty body for large strings.

**Fix:** Remove `PROGMEM` from the `PAGE[]` declaration and use `_srv.send(200, "text/html", PAGE)`.

---

### A.4  Service UUID mismatch — all 6 services logged

After fixing the UUIDs (step A.1 was not yet done), connecting succeeded but service discovery failed. The device's actual services were logged:

```
[ign]   0x1800  rx=no tx=no      — Generic Access
[ign]   0x1801  rx=no tx=no      — Generic Attribute
[ign]   6e400001-b5a3-f393-e0a9-e50e24dcca9e  rx=no tx=no   ← NUS!
[ign]   0x1804  rx=no tx=no      — TX Power
[ign]   0x180a  rx=no tx=no      — Device Information
[ign]   0x180f  rx=no tx=no      — Battery
[ign] no service with RX+TX found
```

The `rx=no tx=no` on the NUS service was because the old characteristic UUIDs were still wrong. Once all three UUIDs were updated to the NUS values, `setupChars()` succeeded.

---

### A.5  Blocking GATT operations stalling WiFi

**Symptom:** After connecting and completing the handshake, the WiFi AP took ~60 s to appear and eventually stopped responding entirely.

**Root cause:** `setupChars()` — which calls `_client->getService()` and `_client->getServices()` — ran in the Arduino loop task. These are synchronous GATT discovery calls that block for 200–800 ms each. During that time `wifi_log_update()` (`_srv.handleClient()`) could not run, starving the WiFi/TCP stack.

**Fix:** Move **all** post-connect work (service discovery, characteristic lookup, handshake writes, notify subscribe) into the FreeRTOS `connectTask`. The Arduino loop only ever hits `break` during HANDSHAKING state. Stack size was increased from 4096 to 8192 bytes to accommodate NimBLE's internal GATT stack.

---

### A.6  Characteristics confirmed correct — still no notifications

After fixing all UUIDs and moving GATT ops to the task, the log confirmed a clean connection:

```
[ign] RX(02) canWrite=yes canWriteNoRsp=yes canNotify=no canRead=no
[ign] TX(03) canWrite=no  canNotify=yes     canIndicate=no canRead=no
[ign] secure: not required / failed
[ign] subscribe TX=ok  RX=no
[ign] ACTIVE — streaming data
```

But `notify #1` never appeared in any log session. The device connects, accepts the CCCD write (subscription), accepts all handshake writes, then sends **zero notifications** before disconnecting with reason 520 (supervision timeout) after ~45 s.

**What was ruled out:**

| Hypothesis | Test | Result |
|---|---|---|
| Wrong notification type (indicate vs notify) | Checked `canNotify` / `canIndicate`; tried both | `canNotify=yes`, subscribe TX=ok |
| Pairing/bonding required | Called `_client->secureConnection()` | `secure: not required / failed` — no pairing needed |
| RX/TX roles swapped | Tried subscribing to `6e400002` as well | `subscribe RX=no` — RX has no notify property |
| Display bug (data arrives but not shown) | Verified `notifyCB` logs first 8 raw hex notifications | Callback never called — confirmed BLE side issue |
| Sim data masking BLE | Turned sim OFF, checked data | Fields go to 0 — display is correct, no BLE data arrives |
| Wrong handshake command order | Tried: commands-first, subscribe-first, no commands, single CR | None triggered notifications |
| Wrong write type | Tried write-with-response (`true`) and write-command (`false`) | Both accepted; no difference in outcome |
| Connection supervision timeout | Added 20 s keepalive CR in ACTIVE loop | Connection lasts longer but still no data |

---

### A.7  Resolution — published reverse-engineering

**State before resolution:** The 123ignition TUNE+ connected reliably, characteristics were found, subscription succeeded and the link stayed up, but zero BLE notifications were received. Two earlier theories (Nordic UART UUIDs; only 3 handshake commands) were both wrong.

**Resolution:** A complete, working reverse-engineering exists at [github.com/iafilius/123Tune-plus-Simulator](https://github.com/iafilius/123Tune-plus-Simulator) (Arjan Filius, 2017–2019). It is an ESP32 simulator that successfully impersonates a real 123\TUNE+ to the official iOS client. Two facts from that codebase fixed our streaming problem:

1. The service uses **proprietary 128-bit UUIDs**, not Nordic UART. The actual UUIDs are documented in the BLE identifiers table at the top of the 123-ignition BLE section.
2. The device requires **six** handshake commands, not three. Live notifications begin only after the final `v@\r` (Version Service Command). The simulator code states this directly: *"All Meter values show up after command 6"*.

Both fixes are now applied in `ignition_bt.cpp`.

**Optional verification using a phone app**

If the device firmware revision differs from the one Filius reverse-engineered (his is `41-10-45`), the protocol grammar may have changed. To verify:

*Option 1 — nRF Connect:* Power off DuettGUI (the device only accepts one BLE link). Install nRF Connect, scan and connect to `123\TUNE+`, expand service `da2b84f1…`, subscribe to TX `18cda784…`, then write `0D`, `31 30 40 0D`, `31 31 40 0D`, `31 32 40 0D`, `31 33 40 0D`, `76 40 0D` to RX `bf03260c…` with ~300 ms gaps. Notifications should start flowing after the last write.

*Option 2 — Android HCI snoop log:* Settings → Developer options → enable Bluetooth HCI snoop log. Run the official 123Tune+ app for ~30 s with the engine cranking. Disable the log to flush, pull `/sdcard/btsnoop_hci.log`, open in Wireshark with filter `btatt`, look for Write operations to handle matching `bf03260c…`.

**Security note:** The simulator's author refuses to use the device on a real car because of an unpatched authentication weakness — the PIN is checked client-side only. The vendor was notified in 2018 with no firmware fix issued. Treat the BLE link as untrusted and never expose the ignition module's BLE radio outside the car.

**One BLE link only:** The device accepts a single connection. Always fully close the 123Tune+ phone app before DuettGUI boots, or DuettGUI will fail to connect (and vice versa).

---

### A.8  Diagnostic log lines reference

The following `wlog()` lines are present in `ignition_bt.cpp` and are useful for ongoing diagnosis:

| Log line | Meaning |
|---|---|
| `scan: 'NAME' ADDR RSSI=X svc=MATCH/no/- name=MATCH/no` | Every BLE device seen during scan; confirms module is advertising |
| `>> SELECTED addr=...` | Device matched by name — connect task will be spawned |
| `spawning connect task` | `xTaskCreate` called; task will attempt connection |
| `connecting to ... heap=X KB` | Task started; heap shown to detect memory pressure |
| `connect OK / FAILED` | BLE connection result |
| `BLE connected` | `onConnect` callback fired (connection established) |
| `chars OK` | Service and both characteristics found |
| `TX canNotify=yes canIndicate=no` | Characteristic properties; confirms correct UUID |
| `secure: ok / not required / failed` | Result of `secureConnection()` attempt |
| `subscribe TX=ok RX=no` | CCCD written for TX; RX has no notify property (expected) |
| `hs 1/3 keepalive` | Handshake CR sent |
| `hs 2/3 adv-curve` | `"10@\r"` sent |
| `hs 3/3 config` | `"11@\r"` sent |
| `ACTIVE — streaming data` | State machine reached ACTIVE; `vdata.ign_connected = true` |
| `notify #N len=X XX XX XX ...` | First 8 notifications logged with raw hex (callback fired) |
| `BLE disconnected (reason 520)` | Supervision timeout — device stopped responding to link-layer events |
| `BLE disconnected (reason 534)` | Local disconnect — our code called `_client->disconnect()` |
| `scan ended found=N reason=N` | Scan cycle ended (after `stop()` or duration expiry) |
