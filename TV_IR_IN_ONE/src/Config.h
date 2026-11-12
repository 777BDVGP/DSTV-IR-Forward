#pragma once

// =============================================================================
// Config.h — TV IR IN ONE hard-coded settings
// =============================================================================

#include <Arduino.h>

#define DEVICE_NAME     "TV-IR-IN-ONE"   // default System Name (Settings page changes it)
#define FW_VERSION      "1.2.8"          // TV_IR_IN_ONE has its own version line

// ── WiFi + passwords ──────────────────────────────────────────────────────────
// Networks, passwords, static IPs and host name are in /WifiSettings.json on
// the board (WifiSetup.h), entered in the setup WiFi on first boot.
// No passwords in the code: an optional src/Private.h (never committed, see
// Private.example.h) can pre-load networks (WIFI_DEFAULT_NETS) and the /update
// password (OWNER_PASSWORD_DEFAULT, OwnerPass.h) for your own boards.
#if __has_include("Private.h")
#include "Private.h"
#endif
#define WIFI_DEFAULT_HOST   "tv-ir-in-one"

// Reboot if WiFi stays disconnected this long (IR forwarding keeps working meanwhile)
#define WIFI_LOST_REBOOT_MS   (10UL * 60UL * 1000UL)

// ── Pins — IR_FORWARD_V1 PCB (full map in Board.h) ───────────────────────────
// TSOP31238 on IR IN, IR LED(s) on IR OUT (wired through to the decoder next
// door). RED / GREEN LEDs work as on TV_IR_Sending: GREEN = DSTV received,
// RED = other remote. Blue heartbeat, buzzer, INPUT and the TMOV monitor are
// driven by Board.cpp.
#include "Board.h"
#define PIN_IR_LED      BRD_PIN_IR_OUT   // 2K2 → 2N2222 → IR LED(s) → 4 × 130R → 5 V

// ── IR output ─────────────────────────────────────────────────────────────────
#define IR_CARRIER_HZ       38000
#define IR_CARRIER_DUTY     33      // %
#define IR_QUEUE_DEPTH      8       // frames waiting for the IR LED
#define IR_XMP_GAP_MS       70      // min. silence after an XMP frame
#define IR_RAW_GAP_MS       40      // min. silence after a raw frame
