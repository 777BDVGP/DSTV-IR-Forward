#pragma once

// =============================================================================
// RouterCheck.h — is the router (gateway) still reachable over WiFi?
// =============================================================================
// IDENTICAL copy in TV_IR_Sending/src and TV_IR_Receiving/src — keep in sync.
//
// WiFi can report "connected" while the AP/router has silently stopped
// delivering traffic to the board (web pages then time out and nothing
// recovers). Every RC_INTERVAL_MS one ICMP ping goes to the gateway, using the
// ESP-IDF ping component in its own task, so the caller never waits.
//   RC_FAILS_RECONNECT failed checks in a row → caller reconnects WiFi
//   RC_FAILS_REBOOT    failed checks in a row → caller logs and reboots
// The caller does the logging / action (TX: SaveGenReport, RX: StatusLog).
// =============================================================================

#include <Arduino.h>

#define RC_INTERVAL_MS       60000UL   // one ping per minute
#define RC_TIMEOUT_MS         1000     // no reply within 1 s = failed
#define RC_FAILS_RECONNECT       3     // 3 minutes unreachable → reconnect WiFi
#define RC_FAILS_REBOOT          6     // 3 more minutes → reboot

enum RouterAction : uint8_t { RC_NONE = 0, RC_RECONNECT, RC_REBOOT, RC_RECOVERED };

// Call from loop() while WiFi is connected. Returns what the caller must do.
RouterAction RouterCheckLoop();

String RouterCheckText();      // "yes (4 ms)" / "NO (2 failed)" / "checking..."
bool   RouterReachable();      // last check answered (true before the first check)
uint8_t RouterFailCount();
