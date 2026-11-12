#pragma once

// =============================================================================
// IrRecord.h — record mode: capture IR strokes from any remote on the RX TSOP
//              or, from the TX web page, on the TX TSOP (RemoteRec.cpp)
// =============================================================================
// Driven step by step from the Sequences web page:
//   RecStart("Reset")          → WAIT_IR   "press the first button on the remote"
//   stroke received            → GOT_IR    shown on the page (Test / Re-record)
//   RecNext(label, delay)      → WAIT_IR   stroke kept, wait for the next one
//   RecFinish(label, delay)    → IDLE      stroke kept, sequence saved to file
//   RecRetry() / RecCancel()
// Only the first frame of a button press is kept (remote repeats are ignored),
// and nothing is captured while/just after the RX's own IR LED is sending.
// =============================================================================

#include <Arduino.h>
#include "SeqStore.h"

enum RecState : uint8_t { REC_IDLE = 0, REC_WAIT_IR, REC_GOT_IR };

#define REC_TIMEOUT_MS   (5UL * 60UL * 1000UL)   // abandon an idle recording

void SetupIrRecord();

String   RecStart(const String &name);                     // "" = ok, else error text
String   RecNext(const String &label, uint32_t delayMs);
String   RecFinish(const String &label, uint32_t delayMs, uint16_t &savedId);
String   RecRetry();
void     RecCancel();
bool     RecTestPending();                                 // send the captured stroke

// Recording from the TX web page (RemoteRec.cpp): the TX captures the stroke
// and sends it over ESP-NOW; it is taken exactly like one from the RX TSOP.
bool     RecOfferStroke(const IrCode &code);               // true = taken (was waiting for IR)

struct RecInfo {
    RecState state;
    String   name;
    uint16_t steps;
    String   pending;      // received stroke, "" unless GOT_IR
    String   lastStep;     // label of the last kept stroke
    bool     hasOwner;     // started from a TX page (owner = that TX's MAC)
    uint8_t  owner[6];
};
RecInfo  RecGetInfo();
void     RecSetOwner(const uint8_t *mac);   // right after RecStart from a TX

String   RecStatusJson();
