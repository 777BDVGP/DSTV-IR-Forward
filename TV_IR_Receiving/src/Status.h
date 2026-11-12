#pragma once

// =============================================================================
// Status.h — Counters, event log and TX-room table shown on the web page
// =============================================================================
// Written from the WiFi task (ESP-NOW callback), the IR task and the web
// server task, so everything is guarded by one FreeRTOS mutex.
// =============================================================================

#include <Arduino.h>
#include "IrLink.h"

#define STATUS_MAX_ROOMS  8
#define STATUS_LOG_LINES  25

void SetupStatus();

void StatusLog(const String &line);   // Status page event log + /GeneralLog.txt
void StatusFlushLog();                 // loop(): write queued lines to /GeneralLog.txt

void StatusCountIrSent();
void StatusCountIrError();
void StatusCountIrDropped();
uint32_t StatusIrSentCount();   // reported to the TX nodes in the beacon

// Called by the ESP-NOW link. Return false for a duplicate frame (same seq).
bool StatusRoomIrFrame(const uint8_t *mac, const IrLinkIrFrame &frame);
void StatusRoomHeartbeat(const uint8_t *mac, const IrLinkHeartbeat &hb);

String StatusJson();   // full status for /api/status
