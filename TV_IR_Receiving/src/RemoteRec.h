#pragma once

// =============================================================================
// RemoteRec.h — record a sequence from the TX web page
// =============================================================================
// The remote is pointed at the TX board; the TX page sends the record commands
// (PKT_REC_CMD) and the captured strokes (PKT_REC_STROKE) over ESP-NOW. They
// drive the same recorder as the RX /sequences page (IrRecord.cpp), so the
// sequence lands in the RX list and is run / edited / scheduled on the RX.
// Every command, and every stroke taken, is answered with a broadcast
// PKT_REC_STATUS. Packets come from EspNowLoop() (loop task, not WiFi task).
// =============================================================================

#include <Arduino.h>

void RemoteRecPacket(const uint8_t *mac, const uint8_t *data, int len);
