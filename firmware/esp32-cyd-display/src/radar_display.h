#pragma once
#include <stdint.h>
#include <stddef.h>

struct CsiFrame {
    int     node_id;
    int     rssi;
    int     channel;
    bool    presence;
    float   amplitudes[64];   // up to 64 subcarriers; num_subcarriers gives actual count
    int     num_subcarriers;
    float   heart_rate;
    float   breathing_rate;
    bool    has_vitals;
    uint32_t frame_count;
};

// Must be called once before any draw function.
void display_init();

// Full-screen "connecting…" splash while WiFi / WS connects.
void display_connecting(const char *msg);

// Draw error message centered on screen.
void display_error(const char *msg);

// Update all three display zones from a received frame.
// Call after each WebSocket message parse.
void display_frame(const CsiFrame &f, uint32_t fps);

// Advance the sweep line by SWEEP_STEP_PX and redraw it.
// Call from the main loop independently of frame arrival.
void display_tick_sweep();
