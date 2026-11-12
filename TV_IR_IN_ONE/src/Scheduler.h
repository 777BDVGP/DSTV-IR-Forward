#pragma once

// =============================================================================
// Scheduler.h — runs recorded sequences at set times on set weekdays
// =============================================================================
// From TV_IR_Receiving, on this board's Clock (NTP, SAST). Checked once a
// second; an entry runs when HH:MM and the weekday match and it has not run in
// that minute yet. Stored in /schedule.json (SeqStore.cpp).
//   * Missed runs (board off / no time at that minute) are NOT caught up.
//   * While a sequence recording is open, due entries are skipped (logged).
//   * Runs also when "Forward DSTV codes" is off — that setting is the remote only.
// The editor is the Schedule card on the /sequences page (WebSequences.cpp).
// =============================================================================

#include <Arduino.h>
#include <vector>
#include "SeqStore.h"

void   SetupScheduler();          // after SetupSeqStore()
void   SchedulerLoop();           // from loop()

std::vector<SchedEntry> SchedGet();
String SchedSet(const std::vector<SchedEntry> &entries);   // "" = ok, else error
bool   SchedRunNow(size_t index);
void   SchedReload();             // after /schedule.json was changed on the Files page
void   SchedForgetSequence(uint16_t id);   // a sequence was deleted
