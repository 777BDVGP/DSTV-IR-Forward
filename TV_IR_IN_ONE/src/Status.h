#pragma once

// =============================================================================
// Status.h — Event log and board status shown on the web page
// =============================================================================
// Written from the IR tasks, the sequencer and the web server task, so
// everything is guarded by one FreeRTOS mutex.
// =============================================================================

#include <Arduino.h>

#define STATUS_LOG_LINES  25

void SetupStatus();

void StatusLog(const String &line);   // Status page event log + /GeneralLog.txt
void StatusFlushLog();                 // loop(): write queued lines to /GeneralLog.txt

String StatusJson();   // full status for /api/status
