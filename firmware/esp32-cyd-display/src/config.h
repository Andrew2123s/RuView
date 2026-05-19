#pragma once
// ── Display geometry ────────────────────────────────────────────────────
// ILI9341 in landscape (rotation=1): 320 wide × 240 tall
#define DISP_W         320
#define DISP_H         240

// Layout zones (Y coordinates, top to bottom)
#define STATUS_H        42    // presence indicator + node/RSSI row
#define BARS_Y          STATUS_H
#define BARS_H          158   // subcarrier bar chart area
#define FOOTER_Y        (STATUS_H + BARS_H)
#define FOOTER_H        (DISP_H - FOOTER_Y)  // ~40px

// Bar chart
#define NUM_SUBCARRIERS 56
#define BAR_MARGIN_X    8
#define BAR_TOTAL_W     (DISP_W - 2 * BAR_MARGIN_X)  // 304px
#define BAR_W           (BAR_TOTAL_W / NUM_SUBCARRIERS) // 5px (remainder cropped)
#define BAR_MAX_H       (BARS_H - 4)

// CSI amplitude normalization: max sqrt(127²+127²) ≈ 180
#define AMP_MAX         180.0f

// ── Colors (RGB565) ─────────────────────────────────────────────────────
#define C_BLACK         0x0000
#define C_WHITE         0xFFFF
#define C_GRAY          0x4208
#define C_DARK          0x18C3
#define C_PRESENT       0x07E0   // bright green
#define C_ABSENT        0x4208   // dark gray
#define C_WARN          0xFE60   // orange
#define C_BAR_LOW       0x07E0   // green
#define C_BAR_MID       0xFFE0   // yellow
#define C_BAR_HIGH      0xF800   // red
#define C_SWEEP         0x001F   // blue sweep line
#define C_HEADER_BG     0x000F   // dark blue header
#define C_FOOTER_BG     0x0841   // very dark grey footer

// ── Timing ──────────────────────────────────────────────────────────────
#define DISPLAY_REFRESH_MS   80    // ~12 fps max
#define SWEEP_STEP_PX         4    // sweep line moves N px per frame
