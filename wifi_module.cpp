// ============================================================
// wifi_module.cpp
// WiFi cloud-backup for ESP32 Groovebox.
// See wifi_module.h for the public API and usage notes.
// ============================================================

#include "wifi_module.h"

#if ENABLE_WIFI

#include <WiFi.h>
#include <WebServer.h>

#include "drum_module.h"
#include "synth_module.h"
#include "effects_module.h"
#include "controls_module.h"

// ── Internal server instance ──────────────────────────────────────────────────
static WebServer server(WIFI_SERVER_PORT);

// ── JSON helpers ─────────────────────────────────────────────────────────────

// Build a JSON representation of the entire groovebox state.
// Maximum payload size estimate:
//   fixed scalars    ~150 chars
//   drumPattern      4 rows × 16 steps × 2 chars + brackets/commas ≈ 145 chars
//   effectEnabled    28 values × 2 chars + brackets/commas ≈ 60 chars
//   Total            ≈ 355 chars; 512 gives comfortable headroom.
static String buildStateJson() {
    String json;
    json.reserve(512);
    json = "{";

    json += "\"bpm\":";           json += bpm;           json += ",";
    json += "\"drumBank\":";      json += currentDrumBank; json += ",";
    json += "\"shape\":";         json += cachedShape;   json += ",";
    json += "\"envIndex\":";      json += cachedEnvIndex; json += ",";
    json += "\"drumDivision\":";  json += drumDivisionIndex; json += ",";
    json += "\"fxAmount\":";      json += fxAmount;      json += ",";

    // Drum pattern [DRUM_ROWS][DRUM_MAX_STEPS]
    json += "\"drumPattern\":[";
    for (int r = 0; r < DRUM_ROWS; r++) {
        json += "[";
        for (int s = 0; s < DRUM_MAX_STEPS; s++) {
            json += drumPattern[r][s] ? "1" : "0";
            if (s < DRUM_MAX_STEPS - 1) json += ",";
        }
        json += "]";
        if (r < DRUM_ROWS - 1) json += ",";
    }
    json += "],";

    // Active effects bitmask [EFFECT_COUNT]
    json += "\"effectEnabled\":[";
    for (int i = 0; i < EFFECT_COUNT; i++) {
        json += effectEnabled[i] ? "1" : "0";
        if (i < EFFECT_COUNT - 1) json += ",";
    }
    json += "]";

    json += "}";
    return json;
}

// Extract an integer value for `key` from a minimal JSON string.
// Returns defaultVal when the key is not found.
static int parseJsonInt(const String& json, const char* key, int defaultVal) {
    String searchKey = "\"";
    searchKey += key;
    searchKey += "\":";
    int pos = json.indexOf(searchKey);
    if (pos < 0) return defaultVal;
    pos += searchKey.length();
    int end = pos;
    while (end < (int)json.length() && (isdigit(json[end]) || json[end] == '-')) end++;
    if (end == pos) return defaultVal;
    return json.substring(pos, end).toInt();
}

// ── HTTP handlers ─────────────────────────────────────────────────────────────

static void handleRoot() {
    String html;
    html.reserve(300);  // static content is ~220 chars
    html  = "<!DOCTYPE html><html><head><title>Groovebox</title></head><body>";
    html += "<h1>ESP32 Groovebox</h1>";
    html += "<p><b>GET</b> <a href='/api/state'>/api/state</a> &ndash; export current state as JSON</p>";
    html += "<p><b>POST</b> /api/state &ndash; import state from a JSON body</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

static void handleGetState() {
    server.send(200, "application/json", buildStateJson());
}

static void handlePostState() {
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Missing JSON body");
        return;
    }
    const String& body = server.arg("plain");

    // BPM
    int newBpm = parseJsonInt(body, "bpm", (int)bpm);
    if (newBpm >= BPM_MIN && newBpm <= BPM_MAX) {
        setBpmExternal((uint16_t)newBpm);
    }

    // Drum bank
    int newBank = parseJsonInt(body, "drumBank", currentDrumBank);
    if (newBank >= 0 && newBank < DRUM_BANK_COUNT) {
        currentDrumBank = newBank;
    }

    // Synth shape
    int newShape = parseJsonInt(body, "shape", cachedShape);
    if (newShape >= 0 && newShape < SHAPE_COUNT) {
        cachedShape = newShape;
    }

    // Envelope preset
    int newEnv = parseJsonInt(body, "envIndex", cachedEnvIndex);
    if (newEnv >= 0 && newEnv < ENV_PRESET_COUNT) {
        cachedEnvIndex = newEnv;
    }

    // Drum division
    int newDiv = parseJsonInt(body, "drumDivision", (int)drumDivisionIndex);
    if (newDiv >= 0 && newDiv <= 2) {
        drumDivisionIndex = (uint8_t)newDiv;
    }

    // FX amount
    int newFxAmt = parseJsonInt(body, "fxAmount", (int)fxAmount);
    if (newFxAmt >= 0 && newFxAmt <= 255) {
        fxAmount = (uint8_t)newFxAmt;
    }

    // Drum pattern  "drumPattern":[[...],[...],[...],[...]]
    {
        String patKey = "\"drumPattern\":";
        int patPos = body.indexOf(patKey);
        if (patPos >= 0) {
            int rowStart = patPos + patKey.length();
            for (int r = 0; r < DRUM_ROWS; r++) {
                int open = body.indexOf('[', rowStart);
                if (open < 0) break;
                int close = body.indexOf(']', open);
                if (close < 0) break;
                String rowStr = body.substring(open + 1, close);
                int p = 0;
                for (int s = 0; s < DRUM_MAX_STEPS; s++) {
                    while (p < (int)rowStr.length() && !isdigit(rowStr[p])) p++;
                    if (p >= (int)rowStr.length()) break;
                    drumPattern[r][s] = (rowStr[p] == '1');
                    p++;
                }
                rowStart = close + 1;
            }
        }
    }

    // Effect enable flags  "effectEnabled":[...]
    {
        String fxKey = "\"effectEnabled\":";
        int fxPos = body.indexOf(fxKey);
        if (fxPos >= 0) {
            int open = body.indexOf('[', fxPos + fxKey.length());
            if (open >= 0) {
                int close = body.indexOf(']', open);
                if (close >= 0) {
                    String fxStr = body.substring(open + 1, close);
                    int p = 0;
                    for (int i = 0; i < EFFECT_COUNT; i++) {
                        while (p < (int)fxStr.length() && !isdigit(fxStr[p])) p++;
                        if (p >= (int)fxStr.length()) break;
                        effectEnabled[i] = (fxStr[p] == '1');
                        p++;
                    }
                }
            }
        }
    }

    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleNotFound() {
    server.send(404, "text/plain", "Not found");
}

// ── FreeRTOS task ─────────────────────────────────────────────────────────────

void wifiServerTask(void* parameter) {
    (void)parameter;

    Serial.println("[WIFI] Task started on core 1");

#if WIFI_AP_MODE
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[WIFI] Access-point \"");
    Serial.print(WIFI_SSID);
    Serial.print("\" started. Connect and browse http://");
    Serial.println(WiFi.softAPIP());
#else
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[WIFI] Connecting to \"");
    Serial.print(WIFI_SSID);
    Serial.print("\"");
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000UL) {
        vTaskDelay(pdMS_TO_TICKS(500));
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("\n[WIFI] Connected. IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n[WIFI] Connection failed; cloud-backup unavailable.");
        vTaskDelete(nullptr);
        return;
    }
#endif

    server.on("/",           HTTP_GET,  handleRoot);
    server.on("/api/state",  HTTP_GET,  handleGetState);
    server.on("/api/state",  HTTP_POST, handlePostState);
    server.onNotFound(handleNotFound);
    server.begin();

    Serial.print("[WIFI] HTTP server listening on port ");
    Serial.println(WIFI_SERVER_PORT);

    for (;;) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ── Public init ───────────────────────────────────────────────────────────────

void initWifi() {
    xTaskCreatePinnedToCore(
        wifiServerTask,
        "wifiTask",
        // Stack breakdown: WebServer internal buffers ~4 KB, String JSON ~0.5 KB,
        // FreeRTOS overhead ~0.5 KB, safety margin ~3 KB → 8 KB is adequate.
        8192,       // stack size (bytes)
        nullptr,
        1,          // priority
        nullptr,
        1           // pin to core 1 (core 0 = Mozzi audio)
    );
}

#endif // ENABLE_WIFI
