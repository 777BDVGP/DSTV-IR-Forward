#pragma once

// =============================================================================
// Clock.h — NTP time for the page header and the event log (SAST, UTC+2)
// =============================================================================
// (The RX Scheduler held this; IN_ONE has no schedule, only the clock.)
// =============================================================================

#include <Arduino.h>

#define CLOCK_TZ        "SAST-2"
#define CLOCK_NTP1      "za.pool.ntp.org"
#define CLOCK_NTP2      "pool.ntp.org"

void   ClockSetTimeZone();   // first thing in setup(): log lines in SAST after a soft reboot
void   SetupClock();         // after WiFi is connected (starts NTP)
void   ClockLoop();          // loop(): logs the first sync

bool   ClockSynced();
String ClockText();          // "30 Sep 2026 14:05:12 Wed" or "OFFLINE  NO  TIME"
