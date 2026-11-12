#pragma once

// =============================================================================
// Sequencer.h — plays recorded sequences on the IR LED (web "Run")
// =============================================================================
// Runs are queued and played one after another by SeqTask. Each stroke goes
// through the IrTransmit queue, so live DSTV codes from the remote still get
// through in between.
// =============================================================================

#include <Arduino.h>
#include <vector>

void   SetupSequencer();

// Queue one or more sequences (in order, pausesMs[k] after
// sequence k; missing = no pause). source is shown
// in the log, e.g. "web". false = queue full.
bool   SeqRun(const std::vector<uint16_t> &ids, const std::vector<uint32_t> &pausesMs,
              const std::vector<uint8_t> &times, const std::vector<uint32_t> &everyMs,
              const String &source);
// times[k]: sequence k runs that often in a row (missing = once), everyMs[k]
// apart (missing = the delay of its last step, at least 100 ms).
bool   SeqTestStep(uint16_t id, uint16_t step);   // send one stroke of a saved sequence

String SeqRunningText();                          // "" when idle
