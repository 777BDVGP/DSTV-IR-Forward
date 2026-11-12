#pragma once

// =============================================================================
// IrRecord.h — record mode: capture IR strokes from any remote on the TSOP
// =============================================================================
// Driven step by step from the Sequences web page:
//   RecStart("Reset")          → WAIT_IR   "press the first button on the remote"
//   stroke received            → GOT_IR    shown on the page (Test / Re-record)
//   RecNext(label, delay)      → WAIT_IR   stroke kept, wait for the next one
//   RecFinish(label, delay)    → IDLE      stroke kept, sequence saved to file
//   RecRetry() / RecCancel()
// Only the first frame of a button press is kept (remote repeats are ignored),
// and nothing is captured while/just after the own IR LED is sending.
// While a recording is open (WAIT_IR / GOT_IR) live DSTV forwarding is paused,
// so recording a DSTV button does not also change the decoder next door.
// The receiver task itself is in IrReceive.cpp (SetupIrReceive).
// =============================================================================

#include <Arduino.h>
#include "SeqStore.h"

enum RecState : uint8_t { REC_IDLE = 0, REC_WAIT_IR, REC_GOT_IR };

#define REC_TIMEOUT_MS   (5UL * 60UL * 1000UL)   // abandon an idle recording

String   RecStart(const String &name);                     // "" = ok, else error text
String   RecNext(const String &label, uint32_t delayMs);
String   RecFinish(const String &label, uint32_t delayMs, uint16_t &savedId);
String   RecRetry();
void     RecCancel();
bool     RecActive();                                      // a recording is open (WAIT_IR / GOT_IR)
bool     RecTestPending();                                 // send the captured stroke

String   RecStatusJson();
