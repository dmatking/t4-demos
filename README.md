# t4-demos

Animated graphics demos for the LilyGo T4-S3 2.41" AMOLED display.

## Hardware

- **Board:** [LilyGo T4-S3 2.41" AMOLED](https://www.lilygo.cc/products/t4-s3) (ESP32-S3, 16MB flash, 8MB OPI PSRAM)
- **Display:** 2.41" AMOLED, 450x600, ~310 DPI, RM690B0 controller
- **Interface:** QSPI over SPI3 at 36 MHz
- **Color:** RGB565 (16-bit, byte-swapped on wire)

## Demos

Four animated demos cycle every 30 seconds:

1. **Mandelbrot zoom** — dives into three fractal locations, resets at float32 precision limit
2. **Plasma** — classic sine-wave interference with shifting palette (integer LUT-based)
3. **Rainbow** — rotating hue sweep with diagonal wave ripple
4. **Starfield** — 600 stars flying toward camera over nebula clouds with motion trails

All rendering uses a full-frame PSRAM framebuffer (450x600x2 = ~527 KB) pushed in a single `display_push_colors` call. Animations use precomputed sin and hue lookup tables for speed.

## Building

Requires [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/get-started/).

```bash
idf.py set-target esp32s3   # first time only
idf.py build flash monitor
```

## Project structure

```
├── main/main.c                  # Demo code
├── components/amoled/           # Vendor QSPI display driver (MIT, Lewis He)
│   ├── amoled_driver.c/h        # SPI bus, pixel push, window management
│   ├── initSequence.c/h         # RM690B0 register init sequence
│   └── product_pins.h           # GPIO definitions
├── sdkconfig.defaults           # PSRAM, 16MB flash, 240MHz, USB JTAG
└── CMakeLists.txt
```

## Display notes

The vendor defines `AMOLED_WIDTH=600` (physical height) and `AMOLED_HEIGHT=450` (physical width). Use `W = amoled_height()` for columns and `H = amoled_width()` for rows. The column offset (+16) is handled automatically by the driver via `CONFIG_LILYGO_T4_S3_241`.
