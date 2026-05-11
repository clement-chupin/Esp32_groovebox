#pragma once

// ============================================================
// wifi_module.h
// Optional WiFi cloud-backup feature for ESP32 Groovebox.
//
// When ENABLE_WIFI is 1 (config_module.h) the ESP32 either creates
// its own Wi-Fi access-point (WIFI_AP_MODE 1) or connects to an
// existing network (WIFI_AP_MODE 0), then starts a lightweight HTTP
// server that exposes a JSON REST API:
//
//   GET  /api/state  – export the current groovebox state as JSON
//   POST /api/state  – import a previously exported state (restores
//                      drum patterns, BPM, shape, effects, …)
//   GET  /           – simple HTML info page
//
// The server runs in its own FreeRTOS task pinned to core 1 so it
// does not compete with the Mozzi audio engine on core 0.
//
// NOTE: even with the task isolated to core 1 the ESP32 Wi-Fi stack
// shares some radio hardware with the CPU and may cause occasional
// audio dropouts.  Leave ENABLE_WIFI 0 for the best audio quality.
// ============================================================

#include <Arduino.h>
#include "config_module.h"

#if ENABLE_WIFI

// Initialise Wi-Fi and start the HTTP server task (call once in setup()).
void initWifi();

// FreeRTOS task function – do NOT call directly; used internally by initWifi().
void wifiServerTask(void* parameter);

#endif // ENABLE_WIFI
