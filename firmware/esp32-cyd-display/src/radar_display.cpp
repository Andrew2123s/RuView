#include "radar_display.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <Arduino.h>

static TFT_eSPI tft;

// Sweep line position (x coordinate within the bar area)
static int16_t s_sweep_x = BAR_MARGIN_X;

// Last drawn amplitudes + peak for consistent sweep erase
static float s_last_amps[NUM_SUBCARRIERS];
static float s_last_peak = AMP_MAX;
static int   s_last_n = 0;

// ── Color helpers ────────────────────────────────────────────────────────

// Map normalized amplitude [0..1] to RGB565 colour (green→yellow→red)
static uint16_t amp_color(float norm) {
    if (norm < 0.5f) {
        // green → yellow
        uint8_t r = (uint8_t)(norm * 2.0f * 31);
        return tft.color565(r * 8, 63 * 4, 0);
    } else {
        // yellow → red
        uint8_t g = (uint8_t)((1.0f - norm) * 2.0f * 63);
        return tft.color565(31 * 8, g * 4, 0);
    }
}

// ── Public API ───────────────────────────────────────────────────────────

void display_init() {
    tft.init();
    tft.setRotation(1);          // landscape: 320×240
    tft.fillScreen(C_BLACK);
    // Backlight driven by TFT_BL=GPIO21 via TFT_eSPI (TFT_BACKLIGHT_ON=HIGH in build_flags)
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    // Draw zone separators
    tft.drawFastHLine(0, STATUS_H,    DISP_W, C_GRAY);
    tft.drawFastHLine(0, FOOTER_Y - 1, DISP_W, C_GRAY);

    // Header background
    tft.fillRect(0, 0, DISP_W, STATUS_H, C_HEADER_BG);

    // Footer background
    tft.fillRect(0, FOOTER_Y, DISP_W, FOOTER_H, C_FOOTER_BG);

    // Initialise sweep state
    memset(s_last_amps, 0, sizeof(s_last_amps));
}

void display_connecting(const char *msg) {
    tft.fillScreen(C_BLACK);
    tft.setTextColor(C_PRESENT, C_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 90);
    tft.print("RuView CSI");
    tft.setTextColor(C_WHITE, C_BLACK);
    tft.setTextSize(1);
    tft.setCursor(20, 115);
    tft.print(msg);
}

void display_error(const char *msg) {
    tft.fillRect(0, BARS_Y + 40, DISP_W, BARS_H - 80, C_BLACK);
    tft.setTextColor(C_WARN, C_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, BARS_Y + 70);
    tft.print(msg);
}

// ── Status bar (top zone) ────────────────────────────────────────────────

static void draw_status(const CsiFrame &f) {
    tft.fillRect(0, 0, DISP_W, STATUS_H, C_HEADER_BG);

    // Presence circle (left side)
    uint16_t pcolor = f.presence ? C_PRESENT : C_ABSENT;
    tft.fillCircle(20, STATUS_H / 2, 10, pcolor);
    tft.setTextColor(f.presence ? C_PRESENT : C_GRAY, C_HEADER_BG);
    tft.setTextSize(1);
    tft.setCursor(36, 4);
    tft.print(f.presence ? "PRESENT" : "absent ");

    // Node / channel
    tft.setTextColor(C_WHITE, C_HEADER_BG);
    tft.setCursor(36, 16);
    char buf[32];
    snprintf(buf, sizeof(buf), "N:%d  CH:%d", f.node_id, f.channel);
    tft.print(buf);

    // RSSI (right-aligned)
    snprintf(buf, sizeof(buf), "RSSI %d", f.rssi);
    tft.setCursor(DISP_W - 72, 4);
    uint16_t rssi_col = (f.rssi > -65) ? C_PRESENT : (f.rssi > -80 ? C_WARN : C_BAR_HIGH);
    tft.setTextColor(rssi_col, C_HEADER_BG);
    tft.print(buf);

    tft.drawFastHLine(0, STATUS_H, DISP_W, C_GRAY);
}

// ── Amplitude bar chart (main zone) ──────────────────────────────────────

static void draw_bars(const float *amps, int n) {
    if (n <= 0) return;

    // Find max for relative scaling (use max(AMP_MAX, observed) to anchor scale)
    float peak = AMP_MAX;
    for (int i = 0; i < n; i++) {
        if (amps[i] > peak) peak = amps[i];
    }

    for (int i = 0; i < n && i < NUM_SUBCARRIERS; i++) {
        int16_t bx = BAR_MARGIN_X + i * BAR_W;
        float norm = amps[i] / peak;
        if (norm > 1.0f) norm = 1.0f;

        int16_t bar_h = (int16_t)(norm * BAR_MAX_H);
        int16_t bar_y = BARS_Y + BARS_H - 2 - bar_h;

        // Erase old bar above new bar top (use s_last_peak to match how it was rendered)
        int16_t old_h = (int16_t)((s_last_amps[i] / s_last_peak) * BAR_MAX_H);
        int16_t old_top = BARS_Y + BARS_H - 2 - old_h;
        if (old_top < bar_y) {
            tft.fillRect(bx, old_top, BAR_W - 1, bar_y - old_top, C_BLACK);
        }

        // Draw bar
        uint16_t col = amp_color(norm);
        tft.fillRect(bx, bar_y, BAR_W - 1, bar_h, col);

        s_last_amps[i] = amps[i];
    }
    s_last_peak = peak;
    s_last_n = n;
}

// ── Footer (vitals + stats) ───────────────────────────────────────────────

static void draw_footer(const CsiFrame &f, uint32_t fps) {
    tft.fillRect(0, FOOTER_Y, DISP_W, FOOTER_H, C_FOOTER_BG);
    tft.setTextColor(C_WHITE, C_FOOTER_BG);
    tft.setTextSize(1);

    char buf[64];
    if (f.has_vitals) {
        snprintf(buf, sizeof(buf), "HR: %.0fbpm  BR: %.0f/min",
                 f.heart_rate, f.breathing_rate);
    } else {
        snprintf(buf, sizeof(buf), "HR: --  BR: --");
    }
    tft.setCursor(8, FOOTER_Y + 6);
    tft.print(buf);

    snprintf(buf, sizeof(buf), "FPS:%u  F:%lu", (unsigned)fps, (unsigned long)f.frame_count);
    tft.setCursor(8, FOOTER_Y + 22);
    tft.setTextColor(C_GRAY, C_FOOTER_BG);
    tft.print(buf);
}

// ── Sweep line animation ──────────────────────────────────────────────────

void display_tick_sweep() {
    // Erase previous sweep line (redraw bar at that x)
    int16_t old_x = s_sweep_x;

    // Advance — wrap at last bar's right edge, not display edge
    s_sweep_x += SWEEP_STEP_PX;
    int16_t bar_area_end = BAR_MARGIN_X + NUM_SUBCARRIERS * BAR_W;
    if (s_sweep_x >= bar_area_end) {
        s_sweep_x = BAR_MARGIN_X;
    }

    // Erase old sweep column by redrawing affected bars
    for (int i = 0; i < s_last_n && i < NUM_SUBCARRIERS; i++) {
        int16_t bx = BAR_MARGIN_X + i * BAR_W;
        if (bx <= old_x && old_x < bx + BAR_W) {
            float norm = s_last_amps[i] / s_last_peak;
            if (norm > 1.0f) norm = 1.0f;
            int16_t bar_h = (int16_t)(norm * BAR_MAX_H);
            int16_t bar_y = BARS_Y + BARS_H - 2 - bar_h;
            tft.fillRect(bx, BARS_Y, BAR_W - 1, bar_y - BARS_Y, C_BLACK);
            tft.fillRect(bx, bar_y, BAR_W - 1, bar_h, amp_color(norm));
        }
    }

    // Draw new sweep line
    tft.drawFastVLine(s_sweep_x, BARS_Y + 2, BARS_H - 4, C_SWEEP);
}

// ── Main update ───────────────────────────────────────────────────────────

void display_frame(const CsiFrame &f, uint32_t fps) {
    draw_status(f);
    draw_bars(f.amplitudes, f.num_subcarriers);
    draw_footer(f, fps);
}
