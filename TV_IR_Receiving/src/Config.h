#pragma once

// =============================================================================
// Config.h — TV IR RX hard-coded settings
// =============================================================================

#include <Arduino.h>

#define DEVICE_NAME     "TV-IR-RX"
#define FW_VERSION      "1.7.1"   // system version — keep equal to TX 01_Main.h

// ── WiFi ──────────────────────────────────────────────────────────────────────
// SSID, password, channel lock, static IP / DHCP and host name are in
// /WifiSettings.json (WifiSetup.h — setup portal when the file is missing).

// Reboot if WiFi stays disconnected this long (ESP-NOW keeps working meanwhile)
#define WIFI_LOST_REBOOT_MS   (10UL * 60UL * 1000UL)

// ── Pins — IR_FORWARD_V1 PCB (full map in Board.h) ───────────────────────────
// Blue LED (slow heartbeat), GREEN LED (per IR frame sent), buzzer, INPUT and
// the TMOV monitor are driven by Board.cpp.
#include "Board.h"
#define PIN_IR_LED      BRD_PIN_IR_OUT   // 2K2 → 2N2222 → IR LED(s) → 4 × 130R → 5 V

// ── IR output ─────────────────────────────────────────────────────────────────
#define IR_CARRIER_HZ       38000
#define IR_CARRIER_DUTY     33      // %
#define IR_QUEUE_DEPTH      8       // frames waiting for the IR LED
#define IR_XMP_GAP_MS       70      // min. silence after an XMP frame
#define IR_RAW_GAP_MS       40      // min. silence after a raw frame
