#pragma once

// =============================================================================
// Scheduler.h — runs recorded sequences at set times on set weekdays
// =============================================================================
// Time from NTP (za.pool.ntp.org, SAST UTC+2). Checked once a second; an entry
// runs when HH:MM and the weekday match and it has not run in that minute yet.
// Missed runs (board off / no time sync at that minute) are NOT caught up.
// =============================================================================

#include <Arduino.h>
#include <vector>
#include "SeqStore.h"

#define SCHED_TZ        "SAST-2"
#define SCHED_NTP1      "za.pool.ntp.org"
#define SCHED_NTP2      "pool.ntp.org"

void   SetupScheduler();          // after WiFi is connected
void   SchedulerLoop();           // from loop()

std::vector<SchedEntry> SchedGet();
String SchedSet(const std::vector<SchedEntry> &entries);   // "" = ok, else error
bool   SchedRunNow(size_t index);
void   SchedReload();             // after /schedule.json was changed on the Files page

bool   SchedTimeSynced();
String SchedTimeText();           // "30 Sep 2026 14:05:12 Wed" or "OFFLINE  NO  TIME"
