#pragma once

// =============================================================================
// Board.h — IR_FORWARD_V1 PCB: pin map + shared board functions
// =============================================================================
// IDENTICAL copy in TV_IR_Sending/src and TV_IR_Receiving/src — keep in sync.
// One PCB for TX and RX; the loaded project decides the role.
//
// Pin map (from PCB/IR_FORWARD_V1.sch netlist, WEMOS LOLIN S2 Mini):
//   GPIO 1   TSOP31238 out (IR_IN header pin 1; pin 2 = 3.3 V, pin 3 = GND)
//   GPIO 2   IR OUT — 2K2 → 2N2222 → IR LED(s) → 4 × 130R (∥) → 5 V
//   GPIO 3   BUZZER — 4K7 → 2N2222, buzzer to 5 V (active buzzer, HIGH = on)
//   GPIO 4   RED   LED — 330R, RED_LED_GREEN header pin 1 (pin 2 = GND) — RX: IR send error
//   GPIO 5   GREEN LED — 330R, RED_LED_GREEN header pin 3 — RX: IR code sent out
//   GPIO 6   INPUT header pin 2 (pin 1 = GND) — function to be determined
//   GPIO 15  on-board blue LED — heartbeat (RX slow, TX fast)
//   GPIO 36  4N35 collector (emitter GND) — Littelfuse TMOV monitor
//
// MOV monitor: AC_L → TMOV thermal link → MON → 5 × 10K → M7 → FUSE_LINK →
// LED1 → 4N35 LED → AC_N. While the TMOV is intact the 4N35 conducts on every
// positive half cycle, so GPIO 36 (internal pull-up) pulses LOW at 50 Hz.
// No pulses = TMOV thermal link blown (or FUSE_LINK jumper removed for a test)
// → buzzer beeps until muted. The mute is kept in NVS (survives reboots) and
// clears itself once pulses are back, i.e. after the TMOV has been replaced.
// Note: on USB power only (no 220 V) there are no pulses → reads as BLOWN.
// =============================================================================

#include <Arduino.h>

// ── Pins ─────────────────────────────────────────────────────────────────────
#define BRD_PIN_IR_IN        1
#define BRD_PIN_IR_OUT       2
#define BRD_PIN_BUZZER       3
#define BRD_PIN_LED_RED      4
#define BRD_PIN_LED_GREEN    5
#define BRD_PIN_INPUT        6
#define BRD_PIN_BLUE_LED    15
#define BRD_PIN_MOV_SENSE   36

// ── Blue LED heartbeat period (full on+off cycle) ────────────────────────────
#define BRD_HEARTBEAT_RX_MS  1500   // TV_IR_Receiving loaded — slow
#define BRD_HEARTBEAT_TX_MS   300   // TV_IR_Sending loaded   — fast

// ── Timing ───────────────────────────────────────────────────────────────────
#define BRD_LED_FLASH_MS      100   // red/green on-time per IR frame
#define BRD_MOV_BLOWN_MS     5000   // no 50 Hz pulse this long → BLOWN (rides out brief dropouts)
#define BRD_MOV_OK_MS        3000   // pulses this long → OK (TMOV replaced)
#define BRD_BUZZ_ON_MS        250   // alarm beep on-time ...
#define BRD_BUZZ_PERIOD_MS   2000   // ... repeated every 2 s
#define BRD_INPUT_DEBOUNCE_MS  50

enum BoardMovState : uint8_t { MOV_UNKNOWN = 0, MOV_OK, MOV_BLOWN };

// Configures all PCB pins and starts the board task (10 ms). Call first in setup().
void BoardSetup(uint32_t heartbeatPeriodMs);

void BoardFlashRed();     // any task — red LED on for BRD_LED_FLASH_MS
void BoardFlashGreen();   // any task — green LED on for BRD_LED_FLASH_MS

BoardMovState BoardMovGetState();
bool          BoardMovMuted();              // buzzer muted by the web page
void          BoardMovSetMuted(bool muted); // web page checkbox (stored in NVS)
bool          BoardBuzzerOn();              // alarm currently sounding (not muted)

bool          BoardInputActive();           // INPUT header shorted (debounced)

const char   *BoardMovText();               // "OK" / "BLOWN" / "BLOWN - buzzer muted" / "checking..."
