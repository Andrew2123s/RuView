#pragma once

// ILI9341 landscape (rotation=1): 320 wide x 240 tall
#define DISP_W          320
#define DISP_H          240

// ── Layout zones ────────────────────────────────────────────────────────
#define STATUS_H        28
#define FOOTER_H        28
#define FOOTER_Y        (DISP_H - FOOTER_H)      // 212
#define PANEL_W         76
#define FIG_X           PANEL_W                   // 76
#define FIG_W           (DISP_W - 2 * PANEL_W)   // 168
#define FIG_Y           STATUS_H                  // 28
#define FIG_H           (FOOTER_Y - FIG_Y)        // 184
#define FIG_CX          (FIG_X + FIG_W / 2)       // 160
#define FIG_CY          (FIG_Y + FIG_H / 2)       // 120

// ── CSI ─────────────────────────────────────────────────────────────────
#define NUM_SUBCARRIERS 56
#define AMP_MAX         180.0f

// ── Colors (RGB565) ─────────────────────────────────────────────────────
#define C_BLACK         0x0000
#define C_WHITE         0xFFFF
#define C_GRAY          0x4208
#define C_HEADER_BG     0x000F   // dark blue
#define C_FOOTER_BG     0x0841   // very dark grey
#define C_PANEL_BG      0x0821   // panel background (near black)
#define C_PANEL_HDR     0x0228   // dark teal panel header strip
#define C_DIVIDER       0x2945   // separator line
#define C_FIG           0x07FF   // cyan — figure wireframe
#define C_FIG_DIM       0x0210   // very dim cyan — absent ghost
#define C_FIG_JOINT     0x5FFF   // light cyan — joints
#define C_PRESENT       0x07E0   // bright green
#define C_ABSENT        0x4208   // dark gray
#define C_WARN          0xFE60   // orange
#define C_HR            0xF9A6   // salmon — heart rate
#define C_BR            0x47DF   // teal — breathing
#define C_CONF          0xFFE0   // yellow — confidence
#define C_RSSI_GOOD     0x07E0   // green
#define C_RSSI_MED      0xFFE0   // yellow
#define C_RSSI_BAD      0xF800   // red

// ── Timing ───────────────────────────────────────────────────────────────
#define DISPLAY_REFRESH_MS   80     // ~12 fps
#define WS_RECONNECT_MS      2000
