/**
 * ESP32-CYD Observatory Display for RuView WiFi DensePose
 *
 * Renders a stick-figure skeleton with vitals panels, mirroring the
 * observatory.html UI from the sensing server.
 *
 * Layout (320x240, landscape):
 *   [76px vitals] [168px figure] [76px signal]    <- 184px tall
 *   Header 28px top, footer 28px bottom.
 */

#include "radar_display.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <Arduino.h>
#include <math.h>

static TFT_eSPI tft;

// Breathing animation state
static float s_breath_phase  = 0.0f;
static float s_breath_expand = 0.0f;   // -1..+1 sin value
static float s_current_br    = 14.0f;  // breaths/min, updated from frame

// Skeleton joint offsets from (FIG_CX, FIG_CY) — positive Y is down
#define J_HEAD_Y   -82
#define J_HEAD_R    10
#define J_NECK_Y   -70
#define J_SHLDR_Y  -60
#define J_SHLDR_X   26   // half shoulder width, expands with breath
#define J_ELBOW_Y  -25
#define J_ELBOW_X   40
#define J_HAND_Y   +14
#define J_HAND_X    42
#define J_HIP_Y    +10
#define J_HIP_X     20
#define J_KNEE_Y   +50
#define J_KNEE_X    20
#define J_FOOT_Y   +82
#define J_FOOT_X    20

// ── Helpers ──────────────────────────────────────────────────────────────

static uint16_t rssi_color(float rssi) {
    if (rssi > -65.0f) return C_RSSI_GOOD;
    if (rssi > -80.0f) return C_RSSI_MED;
    return C_RSSI_BAD;
}

// Draw a filled progress bar (px,py) w wide, h tall, pct in 0..1
static void draw_bar(int16_t px, int16_t py, int16_t w, int16_t h,
                     float pct, uint16_t fill_col) {
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 1.0f) pct = 1.0f;
    int16_t fw = (int16_t)(pct * (float)w);
    tft.fillRect(px, py, fw, h, fill_col);
    if (fw < w) tft.fillRect(px + fw, py, w - fw, h, C_PANEL_BG);
}

// Thick line (draws twice, offset by 1 in X) for visible skeleton
static void skel_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t col) {
    tft.drawLine(x1, y1, x2, y2, col);
    tft.drawLine(x1 + 1, y1, x2 + 1, y2, col);
}

// ── Header (top 28px) ────────────────────────────────────────────────────

static void draw_header(const CsiFrame &f) {
    tft.fillRect(0, 0, DISP_W, STATUS_H, C_HEADER_BG);

    // Brand
    tft.setTextColor(C_FIG, C_HEADER_BG);
    tft.setTextSize(1);
    tft.setCursor(6, 4);
    tft.print("RuView");
    tft.setTextColor(C_GRAY, C_HEADER_BG);
    tft.setCursor(6, 14);
    tft.print("Observatory");

    // Presence badge
    uint16_t pc = f.presence ? C_PRESENT : C_ABSENT;
    tft.fillCircle(106, STATUS_H / 2, 5, pc);
    tft.setTextColor(pc, C_HEADER_BG);
    tft.setCursor(116, 4);
    tft.print(f.presence ? "PRESENT" : "absent ");
    tft.setTextColor(C_GRAY, C_HEADER_BG);
    tft.setCursor(116, 14);
    char buf[12];
    snprintf(buf, sizeof(buf), "P:%d", f.persons_count);
    tft.print(buf);

    // RSSI (right-aligned)
    tft.setTextColor(rssi_color(f.rssi), C_HEADER_BG);
    tft.setCursor(238, 4);
    snprintf(buf, sizeof(buf), "%.0fdBm", f.rssi);
    tft.print(buf);
    tft.setTextColor(C_GRAY, C_HEADER_BG);
    tft.setCursor(238, 14);
    tft.print("WiFi");

    tft.drawFastHLine(0, STATUS_H - 1, DISP_W, C_DIVIDER);
}

// ── Vitals panel (left 76px) ─────────────────────────────────────────────

static void draw_vitals_panel(const CsiFrame &f) {
    tft.fillRect(0, FIG_Y, PANEL_W, FIG_H, C_PANEL_BG);
    tft.drawFastVLine(PANEL_W - 1, FIG_Y, FIG_H, C_DIVIDER);

    // Panel header strip
    tft.fillRect(0, FIG_Y, PANEL_W - 1, 12, C_PANEL_HDR);
    tft.setTextColor(C_WHITE, C_PANEL_HDR);
    tft.setTextSize(1);
    tft.setCursor(3, FIG_Y + 2);
    tft.print("Vital Signs");

    int16_t y = FIG_Y + 16;
    char buf[16];
    const int16_t bw = PANEL_W - 10;  // bar width

    // Heart Rate
    tft.setTextColor(C_HR, C_PANEL_BG);
    tft.setCursor(3, y);
    tft.print("HR");
    y += 9;
    if (f.has_vitals && f.heart_rate > 0.0f) {
        snprintf(buf, sizeof(buf), "%.0f BPM", f.heart_rate);
        tft.setTextColor(C_WHITE, C_PANEL_BG);
    } else {
        strcpy(buf, "-- BPM");
        tft.setTextColor(C_GRAY, C_PANEL_BG);
    }
    tft.setCursor(3, y);
    tft.print(buf);
    y += 9;
    float hr_pct = (f.has_vitals && f.heart_rate > 0.0f)
                   ? f.heart_rate / 200.0f : 0.0f;
    draw_bar(3, y, bw, 4, hr_pct, C_HR);
    y += 12;

    // Breathing Rate
    tft.setTextColor(C_BR, C_PANEL_BG);
    tft.setCursor(3, y);
    tft.print("BR");
    y += 9;
    if (f.has_vitals && f.breathing_rate > 0.0f) {
        snprintf(buf, sizeof(buf), "%.0f /min", f.breathing_rate);
        tft.setTextColor(C_WHITE, C_PANEL_BG);
    } else {
        strcpy(buf, "-- /min");
        tft.setTextColor(C_GRAY, C_PANEL_BG);
    }
    tft.setCursor(3, y);
    tft.print(buf);
    y += 9;
    float br_pct = (f.has_vitals && f.breathing_rate > 0.0f)
                   ? f.breathing_rate / 40.0f : 0.0f;
    draw_bar(3, y, bw, 4, br_pct, C_BR);
    y += 12;

    // Confidence
    tft.setTextColor(C_CONF, C_PANEL_BG);
    tft.setCursor(3, y);
    tft.print("Conf");
    y += 9;
    float conf = (f.confidence >= 0.0f) ? f.confidence : 0.0f;
    snprintf(buf, sizeof(buf), "%.0f%%", conf * 100.0f);
    tft.setTextColor(C_WHITE, C_PANEL_BG);
    tft.setCursor(3, y);
    tft.print(buf);
    y += 9;
    draw_bar(3, y, bw, 4, conf, C_CONF);
    y += 14;

    // Motion bar
    tft.setTextColor(C_GRAY, C_PANEL_BG);
    tft.setCursor(3, y);
    tft.print("Motion");
    y += 9;
    draw_bar(3, y, bw, 4, f.motion_score, C_WARN);
}

// ── Signal panel (right 76px) ─────────────────────────────────────────────

static void draw_signal_panel(const CsiFrame &f) {
    const int16_t px = DISP_W - PANEL_W;  // 244
    tft.fillRect(px, FIG_Y, PANEL_W, FIG_H, C_PANEL_BG);
    tft.drawFastVLine(px, FIG_Y, FIG_H, C_DIVIDER);

    // Panel header strip
    tft.fillRect(px, FIG_Y, PANEL_W, 12, C_PANEL_HDR);
    tft.setTextColor(C_WHITE, C_PANEL_HDR);
    tft.setTextSize(1);
    tft.setCursor(px + 3, FIG_Y + 2);
    tft.print("WiFi Signal");

    int16_t y = FIG_Y + 16;
    char buf[16];
    const int16_t bw = PANEL_W - 10;

    // RSSI
    tft.setTextColor(C_GRAY, C_PANEL_BG);
    tft.setCursor(px + 3, y);
    tft.print("RSSI");
    y += 9;
    snprintf(buf, sizeof(buf), "%.0f dBm", f.rssi);
    tft.setTextColor(rssi_color(f.rssi), C_PANEL_BG);
    tft.setCursor(px + 3, y);
    tft.print(buf);
    y += 9;
    float rssi_pct = (f.rssi + 100.0f) / 70.0f;  // -100=-0%, -30=100%
    if (rssi_pct < 0.0f) rssi_pct = 0.0f;
    if (rssi_pct > 1.0f) rssi_pct = 1.0f;
    draw_bar(px + 3, y, bw, 4, rssi_pct, rssi_color(f.rssi));
    y += 14;

    // Presence badge
    tft.setTextColor(C_GRAY, C_PANEL_BG);
    tft.setCursor(px + 3, y);
    tft.print("Presence");
    y += 9;
    uint16_t badge_bg = f.presence ? 0x0280 : 0x0821;
    uint16_t badge_fg = f.presence ? C_PRESENT : C_ABSENT;
    tft.fillRect(px + 3, y, bw, 11, badge_bg);
    tft.setTextColor(badge_fg, badge_bg);
    tft.setCursor(px + 5, y + 2);
    tft.print(f.presence ? "PRESENT" : "absent");
    y += 15;

    // Persons dots
    tft.setTextColor(C_GRAY, C_PANEL_BG);
    tft.setCursor(px + 3, y);
    snprintf(buf, sizeof(buf), "Ppl: %d", f.persons_count);
    tft.print(buf);
    y += 9;
    tft.fillRect(px + 3, y, bw, 10, C_PANEL_BG);
    uint8_t dots = f.persons_count < 4 ? f.persons_count : 4;
    for (uint8_t i = 0; i < dots; i++) {
        tft.fillCircle(px + 8 + i * 14, y + 4, 4, C_FIG);
    }
    y += 16;

    // Node/channel at bottom
    tft.setTextColor(C_GRAY, C_PANEL_BG);
    tft.setCursor(px + 3, FOOTER_Y - 16);
    snprintf(buf, sizeof(buf), "N%d CH%d", f.node_id, f.channel);
    tft.print(buf);
}

// ── Stick figure (center 168px) ───────────────────────────────────────────

static void draw_skeleton(uint16_t line_col, uint16_t joint_col, int16_t shldr_dx) {
    const int16_t cx = FIG_CX;
    const int16_t cy = FIG_CY;

    // Head
    tft.drawCircle(cx, cy + J_HEAD_Y, J_HEAD_R, joint_col);
    tft.drawCircle(cx, cy + J_HEAD_Y, J_HEAD_R - 1, joint_col);

    // Neck
    tft.drawFastVLine(cx, cy + J_HEAD_Y + J_HEAD_R,
                      (cy + J_SHLDR_Y) - (cy + J_HEAD_Y + J_HEAD_R), line_col);

    // Shoulders
    skel_line(cx - shldr_dx, cy + J_SHLDR_Y,
              cx + shldr_dx, cy + J_SHLDR_Y, line_col);
    tft.fillCircle(cx - shldr_dx, cy + J_SHLDR_Y, 2, joint_col);
    tft.fillCircle(cx + shldr_dx, cy + J_SHLDR_Y, 2, joint_col);

    // Left arm
    skel_line(cx - shldr_dx, cy + J_SHLDR_Y,
              cx - J_ELBOW_X, cy + J_ELBOW_Y, line_col);
    tft.fillCircle(cx - J_ELBOW_X, cy + J_ELBOW_Y, 2, joint_col);
    skel_line(cx - J_ELBOW_X, cy + J_ELBOW_Y,
              cx - J_HAND_X, cy + J_HAND_Y, line_col);

    // Right arm
    skel_line(cx + shldr_dx, cy + J_SHLDR_Y,
              cx + J_ELBOW_X, cy + J_ELBOW_Y, line_col);
    tft.fillCircle(cx + J_ELBOW_X, cy + J_ELBOW_Y, 2, joint_col);
    skel_line(cx + J_ELBOW_X, cy + J_ELBOW_Y,
              cx + J_HAND_X, cy + J_HAND_Y, line_col);

    // Spine
    skel_line(cx, cy + J_SHLDR_Y, cx, cy + J_HIP_Y, line_col);

    // Hips
    skel_line(cx - J_HIP_X, cy + J_HIP_Y,
              cx + J_HIP_X, cy + J_HIP_Y, line_col);
    tft.fillCircle(cx - J_HIP_X, cy + J_HIP_Y, 2, joint_col);
    tft.fillCircle(cx + J_HIP_X, cy + J_HIP_Y, 2, joint_col);

    // Left leg
    skel_line(cx - J_HIP_X, cy + J_HIP_Y,
              cx - J_KNEE_X, cy + J_KNEE_Y, line_col);
    tft.fillCircle(cx - J_KNEE_X, cy + J_KNEE_Y, 2, joint_col);
    skel_line(cx - J_KNEE_X, cy + J_KNEE_Y,
              cx - J_FOOT_X, cy + J_FOOT_Y, line_col);

    // Right leg
    skel_line(cx + J_HIP_X, cy + J_HIP_Y,
              cx + J_KNEE_X, cy + J_KNEE_Y, line_col);
    tft.fillCircle(cx + J_KNEE_X, cy + J_KNEE_Y, 2, joint_col);
    skel_line(cx + J_KNEE_X, cy + J_KNEE_Y,
              cx + J_FOOT_X, cy + J_FOOT_Y, line_col);
}

static void draw_figure(const CsiFrame &f) {
    // Clear figure zone
    tft.fillRect(FIG_X, FIG_Y, FIG_W, FIG_H, C_BLACK);

    // "WiFi DensePose" watermark at bottom of figure area
    tft.setTextColor(0x0841, C_BLACK);
    tft.setTextSize(1);
    tft.setCursor(FIG_X + 18, FOOTER_Y - 14);
    tft.print("WiFi DensePose");

    // Shoulder width expands with breathing
    int16_t shldr_dx = J_SHLDR_X + (int16_t)(s_breath_expand * 3.0f);

    if (f.presence) {
        draw_skeleton(C_FIG, C_FIG_JOINT, shldr_dx);
        // Aura at breath peak
        if (s_breath_expand > 0.6f) {
            tft.drawCircle(FIG_CX, FIG_CY + J_HEAD_Y, J_HEAD_R + 4, 0x0210);
        }
    } else {
        // Ghost skeleton when no-one is detected
        draw_skeleton(C_FIG_DIM, C_FIG_DIM, J_SHLDR_X);
    }
}

// ── Footer (bottom 28px) ──────────────────────────────────────────────────

static void draw_footer(const CsiFrame &f, uint32_t fps) {
    tft.fillRect(0, FOOTER_Y, DISP_W, FOOTER_H, C_FOOTER_BG);
    tft.drawFastHLine(0, FOOTER_Y, DISP_W, C_DIVIDER);
    tft.setTextSize(1);

    char buf[48];
    snprintf(buf, sizeof(buf), "FPS:%u  F:%lu",
             (unsigned)fps, (unsigned long)f.frame_count);
    tft.setTextColor(C_GRAY, C_FOOTER_BG);
    tft.setCursor(6, FOOTER_Y + 6);
    tft.print(buf);

    tft.setTextColor(C_FIG, C_FOOTER_BG);
    tft.setCursor(6, FOOTER_Y + 16);
    tft.print("RuView Observatory");
}

// ── Public API ────────────────────────────────────────────────────────────

void display_init() {
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(C_BLACK);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
}

void display_connecting(const char *msg) {
    tft.fillScreen(C_BLACK);
    tft.setTextColor(C_FIG, C_BLACK);
    tft.setTextSize(2);
    tft.setCursor(24, 70);
    tft.print("RuView");
    tft.setTextColor(C_GRAY, C_BLACK);
    tft.setTextSize(1);
    tft.setCursor(24, 94);
    tft.print("WiFi DensePose Observatory");
    tft.setTextColor(C_WHITE, C_BLACK);
    tft.setCursor(24, 118);
    tft.print(msg);
}

void display_error(const char *msg) {
    tft.fillRect(FIG_X, FIG_Y + 60, FIG_W, 30, C_BLACK);
    tft.setTextColor(C_WARN, C_BLACK);
    tft.setTextSize(1);
    tft.setCursor(FIG_X + 10, FIG_Y + 70);
    tft.print(msg);
}

void display_frame(const CsiFrame &f, uint32_t fps) {
    // Update breathing animation rate from live vitals
    if (f.has_vitals && f.breathing_rate > 0.0f) {
        s_current_br = f.breathing_rate;
    }
    draw_header(f);
    draw_vitals_panel(f);
    draw_figure(f);
    draw_signal_panel(f);
    draw_footer(f, fps);
}

void display_tick_animate() {
    // Advance breathing phase — s_current_br breaths/min, 80ms per tick
    float delta = (s_current_br / 60.0f) * 2.0f * (float)M_PI
                  * (DISPLAY_REFRESH_MS / 1000.0f);
    s_breath_phase += delta;
    if (s_breath_phase >= 2.0f * (float)M_PI) {
        s_breath_phase -= 2.0f * (float)M_PI;
    }
    s_breath_expand = sinf(s_breath_phase);
}
