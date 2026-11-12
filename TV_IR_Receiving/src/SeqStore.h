#pragma once

// =============================================================================
// SeqStore.h — recorded IR sequences + schedule, stored in LittleFS
// =============================================================================
//   /seq/<id>.json   {"name":"Reset","steps":[{"label":"Soundbar ON","delay":2000,
//                     "fmt":2,"proto":3,"bits":32,"value":"20DF10EF","raw":"9000,4500,..."}]}
//   /schedule.json   {"entries":[{"name":"Morning RSG","en":true,"time":"08:00",
//                     "days":62,"seqs":[1,2],"pauses":[10000,3000],"times":[1,30],"every":[500,400],"on":[1,1],"last":"2026-10-01 08:00"}]}
//   days: bit 0 = Sunday ... bit 6 = Saturday (62 = Monday - Friday)
//   times[k]  = how often seqs[k] runs in a row (1-99, e.g. Volume down x30)
//   every[k]  = ms between those runs (100-60000, default 500)
//   on[k]     = 0: line switched off on the page, skipped with its pause
//   pauses[k] = ms to wait after seqs[k] (the last one is not used). Older
//   files with one "gap" for all sequences are read with that gap everywhere.
// All functions are thread-safe (one FS mutex).
// =============================================================================

#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include "IrCode.h"

#define SEQ_MAX_SEQUENCES   40
#define SEQ_MAX_STEPS       40
#define SEQ_NAME_LEN        32
#define SCHED_MAX_ENTRIES   16
#define SCHED_PAUSE_DEFAULT_MS 3000
#define SCHED_PAUSE_MIN_MS   100      // set on the page in ms
#define SCHED_PAUSE_MAX_MS   600000
#define SCHED_TIMES_MAX      99
#define SCHED_EVERY_DEFAULT_MS 500
#define SCHED_EVERY_MIN_MS   100
#define SCHED_EVERY_MAX_MS   60000
#define SCHED_MAX_SEQS       8

struct SeqStep {
    String   label;
    uint32_t delayMs = 500;      // wait after this stroke before the next one
    IrCode   code;
};

struct Sequence {
    uint16_t id = 0;
    String   name;
    std::vector<SeqStep> steps;
};

struct SeqInfo {                 // list entry (no IR data)
    uint16_t id;
    String   name;
    uint16_t steps;
};

struct SchedEntry {
    String   name;
    bool     enabled = true;
    String   time    = "08:00";  // HH:MM
    uint8_t  days    = 0x3E;     // Mon - Fri
    std::vector<uint16_t> seqs;  // sequence ids, run in this order
    std::vector<uint32_t> pauses;// ms pause after each sequence, same length as seqs
    std::vector<uint8_t>  times; // runs of each sequence in a row (1-99), same length
    std::vector<uint32_t> every; // ms between those runs, same length
    std::vector<uint8_t>  on;    // 1 = line runs, 0 = skipped; same length
    String   lastRun;            // "YYYY-MM-DD HH:MM" — never runs twice in one minute
};

bool SetupSeqStore();            // mount LittleFS (formats on first use)

std::vector<SeqInfo> SeqList();
bool     SeqLoad(uint16_t id, Sequence &seq);
uint16_t SeqSave(Sequence &seq);            // id 0 = new; returns id (0 = failed)
bool     SeqDelete(uint16_t id);
bool     SeqNameUsed(const String &name, uint16_t exceptId = 0);
String   SeqName(uint16_t id);              // "" if missing

std::vector<SchedEntry> SchedLoad();
bool     SchedSave(const std::vector<SchedEntry> &entries);
void     SchedReadSeqs(JsonObjectConst o, SchedEntry &e);   // "seqs" + "pauses" (or old "gap") + "times" + "every" + "on"

void     SeqReindex();                      // after files were changed on the Files page

size_t   StoreUsedBytes();
size_t   StoreTotalBytes();
