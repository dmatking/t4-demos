// Copyright 2025 David M. King
// SPDX-License-Identifier: Apache-2.0

#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "amoled_driver.h"

#define TAG "APP"
#define DEMO_SECS 30
#define NUM_STARS 600

static int W, H;
static uint16_t *fb;

// --- Fast sin lookup table (1024 entries, fixed-point 8.8) ---
#define SIN_LUT_SIZE 1024
#define SIN_LUT_MASK (SIN_LUT_SIZE - 1)
static int16_t sin_lut[SIN_LUT_SIZE];  // values in [-256, 256] representing [-1.0, 1.0]

static void init_sin_lut(void)
{
    for (int i = 0; i < SIN_LUT_SIZE; i++) {
        sin_lut[i] = (int16_t)(sinf(i * 2.0f * M_PI / SIN_LUT_SIZE) * 256.0f);
    }
}

// Fast sin: input is fixed-point where 1024 = full period (2*pi)
static inline int fast_sin(int angle)
{
    return sin_lut[angle & SIN_LUT_MASK];
}

// --- HSV lookup table (360 entries, pre-byte-swapped RGB565) ---
static uint16_t hue_lut[360];

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return (c >> 8) | (c << 8);
}

static void init_hue_lut(void)
{
    for (int h = 0; h < 360; h++) {
        float hf = (float)h;
        float c = 1.0f;
        float x = c * (1.0f - fabsf(fmodf(hf / 60.0f, 2.0f) - 1.0f));
        float r, g, b;
        if      (hf < 60)  { r = c; g = x; b = 0; }
        else if (hf < 120) { r = x; g = c; b = 0; }
        else if (hf < 180) { r = 0; g = c; b = x; }
        else if (hf < 240) { r = 0; g = x; b = c; }
        else if (hf < 300) { r = x; g = 0; b = c; }
        else               { r = c; g = 0; b = x; }
        hue_lut[h] = rgb565((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
    }
}

static inline uint16_t fast_hue(int h)
{
    h = h % 360;
    if (h < 0) h += 360;
    return hue_lut[h];
}

static void push(void)
{
    display_push_colors(0, 0, W, H, fb);
}

// ==========================================================================
// Demo 1: Mandelbrot zoom
// ==========================================================================
// Zoom targets with rich detail at multiple scales
static const float zoom_targets[][2] = {
    { -0.74364388703f, 0.13182590421f },  // seahorse valley spiral
    { -0.16070135f,    1.03757843f    },   // spiral arm
    { -1.25066f,       0.02012f       },   // mini-brot tendril
};
#define NUM_TARGETS (sizeof(zoom_targets) / sizeof(zoom_targets[0]))

static void run_mandelbrot(void)
{
    ESP_LOGI(TAG, "Mandelbrot zoom");
    int target_idx = 0;
    float scale = 0.008f;
    int64_t start = esp_timer_get_time();

    while ((esp_timer_get_time() - start) < (int64_t)DEMO_SECS * 1000000) {
        float cx = zoom_targets[target_idx][0];
        float cy = zoom_targets[target_idx][1];

        for (int py = 0; py < H; py++) {
            float y0 = cy + (py - H / 2) * scale;
            for (int px = 0; px < W; px++) {
                float x0 = cx + (px - W / 2) * scale;
                float x = 0, y = 0;
                int iter = 0;
                while (x * x + y * y <= 4.0f && iter < 48) {
                    float xt = x * x - y * y + x0;
                    y = 2.0f * x * y + y0;
                    x = xt;
                    iter++;
                }
                if (iter == 48) {
                    fb[py * W + px] = 0;
                } else {
                    fb[py * W + px] = fast_hue((iter * 8) % 360);
                }
            }
        }
        push();
        scale *= 0.50f;

        // Reset before float32 loses precision, pick next target
        if (scale < 1e-6f) {
            target_idx = (target_idx + 1) % NUM_TARGETS;
            scale = 0.008f;
        }
    }
}

// ==========================================================================
// Demo 2: Animated plasma (integer LUT-based)
// ==========================================================================
static void run_plasma(void)
{
    ESP_LOGI(TAG, "Animated plasma");
    int t = 0;
    int64_t start = esp_timer_get_time();

    // Precompute scale factors: pixel coord -> sin LUT index
    // We want ~1 full sine period per 45 pixels -> 1024/45 ≈ 23
    const int XSCALE = 23;
    const int YSCALE = 23;

    while ((esp_timer_get_time() - start) < (int64_t)DEMO_SECS * 1000000) {
        for (int py = 0; py < H; py++) {
            int ya = py * YSCALE + t * 18;
            int ysin = fast_sin(ya);
            for (int px = 0; px < W; px++) {
                int xa = px * XSCALE + t * 25;
                // 4 sine components combined
                int v = fast_sin(xa)
                      + ysin
                      + fast_sin(xa + ya)
                      + fast_sin((px * px + py * py) / 50 + t * 33);
                // v is in [-1024, 1024], map to hue [0, 360)
                int hue = (v + 1024) * 360 / 2048 + t * 2;
                fb[py * W + px] = fast_hue(hue);
            }
        }
        push();
        t++;
    }
}

// ==========================================================================
// Demo 3: Rotating rainbow with wave
// ==========================================================================
static void run_rainbow(void)
{
    ESP_LOGI(TAG, "Rotating rainbow");
    int t = 0;
    int64_t start = esp_timer_get_time();

    while ((esp_timer_get_time() - start) < (int64_t)DEMO_SECS * 1000000) {
        for (int py = 0; py < H; py++) {
            int base_hue = py * 360 / H + t * 3;
            for (int px = 0; px < W; px++) {
                // Diagonal wave offset
                int wave = fast_sin((px + py) * 5 + t * 8) / 8;  // [-32, 32]
                fb[py * W + px] = fast_hue(base_hue + wave);
            }
        }
        push();
        t++;
    }
}

// ==========================================================================
// Demo 4: Flying starfield
// ==========================================================================

typedef struct {
    float x, y, z;
    uint8_t bright;
    uint8_t tint;
} star_t;

static star_t stars[NUM_STARS];

static void init_star(star_t *s, bool far)
{
    s->x = (rand() % 2000 - 1000) / 100.0f;
    s->y = (rand() % 2000 - 1000) / 100.0f;
    s->z = far ? (rand() % 900 + 100) / 100.0f : (rand() % 1000 + 1) / 100.0f;
    s->bright = 150 + rand() % 106;
    s->tint = rand() % 3;
}

static void run_starfield(void)
{
    ESP_LOGI(TAG, "Flying starfield");
    srand(42);
    for (int i = 0; i < NUM_STARS; i++) init_star(&stars[i], false);

    // Precompute nebula
    uint16_t *nebula = heap_caps_malloc(W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (nebula) {
        for (int py = 0; py < H; py++) {
            for (int px = 0; px < W; px++) {
                // Two soft glow centers
                int dx1 = px - W * 35 / 100, dy1 = py - H * 40 / 100;
                int dx2 = px - W * 70 / 100, dy2 = py - H * 65 / 100;
                // Use integer squared distance, skip sqrtf
                int d1 = (dx1 * dx1 + dy1 * dy1) / 100 + 1;
                int d2 = (dx2 * dx2 + dy2 * dy2) / 100 + 1;
                int r = 6000 / d1 + 9000 / d2;
                int g = 1500 / d1 + 3000 / d2;
                int b = 12000 / d1 + 4500 / d2;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
                nebula[py * W + px] = rgb565(r, g, b);
            }
        }
    }

    int64_t start = esp_timer_get_time();
    float speed = 0.08f;

    while ((esp_timer_get_time() - start) < (int64_t)DEMO_SECS * 1000000) {
        if (nebula) {
            memcpy(fb, nebula, W * H * sizeof(uint16_t));
        } else {
            memset(fb, 0, W * H * sizeof(uint16_t));
        }

        for (int i = 0; i < NUM_STARS; i++) {
            star_t *s = &stars[i];
            s->z -= speed;

            if (s->z <= 0.1f) { init_star(s, true); continue; }

            int sx = W / 2 + (int)(s->x / s->z * 60.0f);
            int sy = H / 2 + (int)(s->y / s->z * 60.0f);

            if (sx < 1 || sx >= W - 1 || sy < 1 || sy >= H - 1) {
                init_star(s, true);
                continue;
            }

            float depth = 1.0f / (s->z * 0.3f);
            if (depth > 1.0f) depth = 1.0f;
            uint8_t br = (uint8_t)(s->bright * depth);

            uint8_t r = br, g = br, b = br;
            if (s->tint == 0) { r = 255; g = br * 4 / 5; b = br * 3 / 5; }
            else if (s->tint == 1) { r = br * 7 / 10; g = br * 4 / 5; b = 255; }
            uint16_t color = rgb565(r, g, b);

            fb[sy * W + sx] = color;

            if (s->z < 3.0f) {
                fb[sy * W + sx + 1] = color;
                fb[(sy + 1) * W + sx] = color;
            }
            if (s->z < 1.5f) {
                fb[(sy - 1) * W + sx] = color;
                fb[sy * W + sx - 1] = color;
                fb[(sy + 1) * W + sx + 1] = color;
                fb[(sy - 1) * W + sx + 1] = color;
            }

            // Motion trail
            if (s->z < 2.5f) {
                int tlen = (int)(4.0f / s->z);
                if (tlen > 6) tlen = 6;
                for (int j = 1; j <= tlen; j++) {
                    int ty = sy - j;
                    if (ty >= 0) {
                        uint8_t fade = br / (j + 1);
                        fb[ty * W + sx] = rgb565(fade, fade, fade);
                    }
                }
            }
        }

        push();
        speed += 0.0005f;
    }

    if (nebula) heap_caps_free(nebula);
}

// ==========================================================================

void app_main(void)
{
    ESP_LOGI(TAG, "display_init...");
    display_init();
    vTaskDelay(pdMS_TO_TICKS(500));

    W = amoled_height();  // 450
    H = amoled_width();   // 600

    fb = heap_caps_malloc(W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb) {
        ESP_LOGE(TAG, "framebuffer alloc failed");
        return;
    }

    init_sin_lut();
    init_hue_lut();

    while (1) {
        run_mandelbrot();
        run_plasma();
        run_rainbow();
        run_starfield();
    }
}
