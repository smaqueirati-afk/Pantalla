#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_check.h"
/**
 * @file palette_extractor.c
 * @brief Median Cut palette extraction — 2 iterations, 4 output colours.
 *
 * Steps:
 *   1. Downsample: sample every 4th pixel to reduce work (~2500 samples from 400x400).
 *   2. Median Cut x2: split colour space along the widest channel axis.
 *   3. Average each bucket → 4 representative colours.
 *   4. Assign roles: primary (most pixels), accent (highest saturation),
 *      secondary (second most), text_on (computed contrast).
 */

#include "ui/palette_extractor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

static const char *TAG = "palette";

/* ── Colour structs ─────────────────────────────────────────────────────────*/
typedef struct { uint8_t r, g, b; } rgb_t;

typedef struct {
    rgb_t   *pixels;
    uint32_t count;
} bucket_t;

/* ── Helpers ────────────────────────────────────────────────────────────────*/

/* Relative luminance (sRGB approximation) */
static float luminance(uint8_t r, uint8_t g, uint8_t b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/* HSL saturation (0–1) */
static float saturation(uint8_t r, uint8_t g, uint8_t b)
{
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float mx = rf > gf ? (rf > bf ? rf : bf) : (gf > bf ? gf : bf);
    float mn = rf < gf ? (rf < bf ? rf : bf) : (gf < bf ? gf : bf);
    float d  = mx - mn;
    if (d < 0.001f) return 0.0f;
    float l = (mx + mn) / 2.0f;
    return d / (1.0f - fabsf(2.0f * l - 1.0f));
}

static uint32_t argb(uint8_t r, uint8_t g, uint8_t b)
{
    return (0xFF000000u) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* ── Channel range across a bucket ─────────────────────────────────────────*/
typedef enum { CHAN_R, CHAN_G, CHAN_B } chan_t;

static void bucket_range(const bucket_t *bkt,
                         uint8_t *rmin, uint8_t *rmax,
                         uint8_t *gmin, uint8_t *gmax,
                         uint8_t *bmin, uint8_t *bmax)
{
    *rmin = *gmin = *bmin = 255;
    *rmax = *gmax = *bmax = 0;
    for (uint32_t i = 0; i < bkt->count; i++) {
        if (bkt->pixels[i].r < *rmin) *rmin = bkt->pixels[i].r;
        if (bkt->pixels[i].r > *rmax) *rmax = bkt->pixels[i].r;
        if (bkt->pixels[i].g < *gmin) *gmin = bkt->pixels[i].g;
        if (bkt->pixels[i].g > *gmax) *gmax = bkt->pixels[i].g;
        if (bkt->pixels[i].b < *bmin) *bmin = bkt->pixels[i].b;
        if (bkt->pixels[i].b > *bmax) *bmax = bkt->pixels[i].b;
    }
}

/* ── Sort comparators ───────────────────────────────────────────────────────*/
static int cmp_r(const void *a, const void *b)
{ return (int)((rgb_t *)a)->r - (int)((rgb_t *)b)->r; }
static int cmp_g(const void *a, const void *b)
{ return (int)((rgb_t *)a)->g - (int)((rgb_t *)b)->g; }
static int cmp_b(const void *a, const void *b)
{ return (int)((rgb_t *)a)->b - (int)((rgb_t *)b)->b; }

/* ── Split one bucket into two at the median of its widest channel ──────────*/
static void split_bucket(bucket_t *src, bucket_t *lo, bucket_t *hi)
{
    uint8_t rmin, rmax, gmin, gmax, bmin, bmax;
    bucket_range(src, &rmin, &rmax, &gmin, &gmax, &bmin, &bmax);

    uint8_t rrange = rmax - rmin;
    uint8_t grange = gmax - gmin;
    uint8_t brange = bmax - bmin;

    if (rrange >= grange && rrange >= brange)
        qsort(src->pixels, src->count, sizeof(rgb_t), cmp_r);
    else if (grange >= rrange && grange >= brange)
        qsort(src->pixels, src->count, sizeof(rgb_t), cmp_g);
    else
        qsort(src->pixels, src->count, sizeof(rgb_t), cmp_b);

    uint32_t mid = src->count / 2;
    lo->pixels = src->pixels;
    lo->count  = mid;
    hi->pixels = src->pixels + mid;
    hi->count  = src->count - mid;
}

/* ── Average bucket colour ──────────────────────────────────────────────────*/
static rgb_t bucket_avg(const bucket_t *bkt)
{
    if (bkt->count == 0) return (rgb_t){0, 0, 0};
    uint32_t rs = 0, gs = 0, bs = 0;
    for (uint32_t i = 0; i < bkt->count; i++) {
        rs += bkt->pixels[i].r;
        gs += bkt->pixels[i].g;
        bs += bkt->pixels[i].b;
    }
    return (rgb_t){
        (uint8_t)(rs / bkt->count),
        (uint8_t)(gs / bkt->count),
        (uint8_t)(bs / bkt->count)
    };
}

/* ── Public API ─────────────────────────────────────────────────────────────*/

bool palette_extract(const uint8_t *rgb888, uint32_t width, uint32_t height,
                     color_palette_t *out)
{
    if (!rgb888 || !out || width == 0 || height == 0) return false;

    int64_t t0 = esp_timer_get_time();

    /* 1. Downsample — every 4th pixel in each direction (stride=4) */
    uint32_t stride    = 4;
    uint32_t max_samp  = (width / stride) * (height / stride);
    rgb_t *samples = (rgb_t *)malloc(max_samp * sizeof(rgb_t));
    if (!samples) {
        ESP_LOGE(TAG, "malloc %u bytes failed", (unsigned)(max_samp * sizeof(rgb_t)));
        return false;
    }

    uint32_t ns = 0;
    for (uint32_t y = 0; y < height; y += stride) {
        for (uint32_t x = 0; x < width; x += stride) {
            uint32_t idx = (y * width + x) * 3;
            samples[ns].r = rgb888[idx];
            samples[ns].g = rgb888[idx + 1];
            samples[ns].b = rgb888[idx + 2];
            ns++;
        }
    }

    /* 2. Median Cut — 2 splits → 4 buckets */
    bucket_t b0 = { samples, ns };
    bucket_t b1a, b1b;
    split_bucket(&b0, &b1a, &b1b);

    bucket_t b2a, b2b, b2c, b2d;
    split_bucket(&b1a, &b2a, &b2b);
    split_bucket(&b1b, &b2c, &b2d);

    /* 3. Average each bucket → 4 representative colours */
    rgb_t cols[4] = {
        bucket_avg(&b2a),
        bucket_avg(&b2b),
        bucket_avg(&b2c),
        bucket_avg(&b2d),
    };
    uint32_t counts[4] = {
        b2a.count, b2b.count, b2c.count, b2d.count
    };

    free(samples);

    /* 4. Assign roles */
    /* Primary = largest bucket */
    uint32_t primary_idx = 0;
    for (int i = 1; i < 4; i++)
        if (counts[i] > counts[primary_idx]) primary_idx = i;

    /* Secondary = second largest */
    uint32_t secondary_idx = (primary_idx == 0) ? 1 : 0;
    for (int i = 0; i < 4; i++) {
        if (i == (int)primary_idx) continue;
        if (counts[i] > counts[secondary_idx]) secondary_idx = i;
    }

    /* Accent = highest saturation (excluding primary if same) */
    uint32_t accent_idx = (primary_idx == 0) ? 1 : 0;
    float best_sat = -1.0f;
    for (int i = 0; i < 4; i++) {
        float s = saturation(cols[i].r, cols[i].g, cols[i].b);
        if (s > best_sat) { best_sat = s; accent_idx = i; }
    }

    /* Darken primary slightly for bg use */
    rgb_t p = cols[primary_idx];
    p.r = (uint8_t)(p.r * 0.5f);
    p.g = (uint8_t)(p.g * 0.5f);
    p.b = (uint8_t)(p.b * 0.5f);

    out->primary   = argb(p.r, p.g, p.b);
    out->secondary = argb(cols[secondary_idx].r, cols[secondary_idx].g, cols[secondary_idx].b);
    out->accent    = argb(cols[accent_idx].r,    cols[accent_idx].g,    cols[accent_idx].b);

    /* text_on: white if primary is dark, black if bright */
    float lum = luminance(p.r, p.g, p.b);
    out->text_on = (lum < 128.0f) ? 0xFFFFFFFFu : 0xFF000000u;

    int64_t elapsed_us = esp_timer_get_time() - t0;
    ESP_LOGI(TAG, "Palette extracted in %lld ms — P=#%06lX A=#%06lX",
             elapsed_us / 1000,
             (unsigned long)(out->primary & 0xFFFFFF),
             (unsigned long)(out->accent  & 0xFFFFFF));

    return true;
}
