#pragma once
#include <stdint.h>
#include <stdbool.h>

#define NUM_SUBCARRIERS 56

struct CsiFrame {
    uint8_t  node_id;
    float    rssi;           // dBm (float from server)
    uint8_t  channel;
    bool     presence;
    float    amplitudes[NUM_SUBCARRIERS];
    uint8_t  num_subcarriers;
    float    heart_rate;     // BPM, -1 if unknown
    float    breathing_rate; // BPM, -1 if unknown
    bool     has_vitals;
    float    confidence;     // 0..1 from classification
    uint8_t  persons_count;  // estimated_persons or derived from presence
    float    motion_score;   // 0..1 normalised
    uint32_t frame_count;
};

void display_init();
void display_connecting(const char *msg);
void display_error(const char *msg);
void display_frame(const CsiFrame &f, uint32_t fps);
void display_tick_animate();
