/**
 * ESP32-CYD Radar Display for RuView WiFi CSI Sensing
 *
 * Connects to the wifi-densepose sensing-server WebSocket (ws://HOST:3001)
 * and renders CSI amplitude bars, presence detection, and vitals on the
 * 2.8" ILI9341 TFT display of the ESP32-CYD (2432S028R).
 *
 * Build with PlatformIO: pio run --target upload
 * Configure WIFI_SSID, WIFI_PASS, SERVER_HOST in platformio.ini build_flags.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "radar_display.h"

// ── State ────────────────────────────────────────────────────────────────
static WebSocketsClient ws;
static CsiFrame g_frame = {};
static volatile bool g_frame_ready = false;
static uint32_t g_frame_count = 0;
static uint32_t g_fps_last_ms = 0;
static uint32_t g_fps_frames_this_sec = 0;
static uint32_t g_fps = 0;
static uint32_t g_last_display_ms = 0;
static uint32_t g_last_animate_ms = 0;
static bool g_ws_connected = false;

// ── JSON parse ───────────────────────────────────────────────────────────

static void parse_frame(const char *payload, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) return;

    const char *type = doc["type"] | "";
    if (strcmp(type, "sensing_update") != 0) return;

    CsiFrame f = {};

    // Node 0 — RSSI and amplitudes
    JsonObjectConst node0 = doc["nodes"][0];
    f.node_id = node0["node_id"] | 0;
    f.rssi    = node0["rssi_dbm"] | -100.0f;
    f.channel = 6;

    JsonArrayConst amps = node0["amplitude"].as<JsonArrayConst>();
    f.num_subcarriers = 0;
    for (JsonVariantConst v : amps) {
        if (f.num_subcarriers >= NUM_SUBCARRIERS) break;
        f.amplitudes[f.num_subcarriers++] = v.as<float>();
    }

    // Classification
    f.presence   = doc["classification"]["presence"] | false;
    f.confidence = doc["classification"]["confidence"] | 0.0f;

    // Vital signs (optional field)
    JsonObjectConst vs = doc["vital_signs"];
    if (!vs.isNull()) {
        float hr = vs["heart_rate_bpm"] | -1.0f;
        float br = vs["breathing_rate_bpm"] | -1.0f;
        if (hr > 0.0f && br > 0.0f) {
            f.heart_rate     = hr;
            f.breathing_rate = br;
            f.has_vitals     = true;
        }
    }

    // Multi-person count
    f.persons_count = (uint8_t)(doc["estimated_persons"] | 0);

    // Motion score from feature variance, clamped to [0, 1]
    float var = doc["features"]["variance"] | 0.0f;
    f.motion_score = var < 1.0f ? var : 1.0f;

    f.frame_count = ++g_frame_count;
    g_fps_frames_this_sec++;

    uint32_t now = millis();
    if (now - g_fps_last_ms >= 1000) {
        g_fps = g_fps_frames_this_sec;
        g_fps_frames_this_sec = 0;
        g_fps_last_ms = now;
    }

    memcpy(&g_frame, &f, sizeof(f));
    g_frame_ready = true;
}

// ── WebSocket callback ───────────────────────────────────────────────────

static void on_ws_event(WStype_t type, uint8_t *payload, size_t len) {
    switch (type) {
        case WStype_DISCONNECTED:
            g_ws_connected = false;
            display_connecting("WebSocket disconnected — reconnecting...");
            break;

        case WStype_CONNECTED:
            g_ws_connected = true;
            break;

        case WStype_TEXT:
            parse_frame((const char *)payload, len);
            break;

        case WStype_ERROR:
            display_error("WS error");
            break;

        default:
            break;
    }
}

// ── WiFi connection ──────────────────────────────────────────────────────

static void wifi_connect() {
    display_connecting("Connecting to WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        if (millis() - t0 > 20000) {
            display_error("WiFi timeout — check credentials");
            delay(5000);
            ESP.restart();
        }
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "WiFi OK  %s", WiFi.localIP().toString().c_str());
    display_connecting(msg);
    delay(800);
}

// ── Setup ────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    display_init();
    display_connecting("Initialising...");

    wifi_connect();

    display_connecting("Connecting to sensing server...");

    ws.begin(SERVER_HOST, SERVER_WS_PORT, SERVER_WS_PATH);
    ws.onEvent(on_ws_event);
    ws.setReconnectInterval(WS_RECONNECT_MS);
    ws.enableHeartbeat(15000, 3000, 2);  // ping every 15s
}

// ── Loop ─────────────────────────────────────────────────────────────────

void loop() {
    ws.loop();

    uint32_t now = millis();

    // Refresh display with latest frame
    if (g_frame_ready && (now - g_last_display_ms >= DISPLAY_REFRESH_MS)) {
        display_frame(g_frame, g_fps);
        g_frame_ready = false;
        g_last_display_ms = now;
    }

    // Advance breathing animation regardless of new frames
    if (g_ws_connected && (now - g_last_animate_ms >= DISPLAY_REFRESH_MS)) {
        display_tick_animate();
        g_last_animate_ms = now;
    }

    // Show reconnecting message if WS not up
    if (!g_ws_connected && (now % 3000 < 50)) {
        char msg[80];
        snprintf(msg, sizeof(msg), "Waiting for %s:%d%s",
                 SERVER_HOST, SERVER_WS_PORT, SERVER_WS_PATH);
        display_connecting(msg);
    }
}
