#pragma once

// =============================================================================
// IrTransmit.h — IR LED output via the ESP32-S2 RMT peripheral
// =============================================================================
// RMT generates the 38 kHz carrier and every mark/space in hardware, so WiFi /
// ESP-NOW interrupts on the single S2 core cannot distort the timing (XMP
// encodes 4 bits per space in 135 µs steps and does not tolerate jitter).
// Forwarded frames (ESP-NOW) and recorded strokes (sequences) share one
// queue and are sent one at a time by IrTxTask, so they never overlap.
// =============================================================================

#include "IrLink.h"
#include "IrCode.h"

void SetupIrTransmit();
bool QueueIrFrame(const IrLinkIrFrame &frame);   // false if queue full / invalid
bool ResendLastIrFrame();                         // web "Resend last" button

// Recorded stroke. waitMs > 0: block until it has been sent (or timeout).
bool SendIrCode(const IrCode &code, uint32_t waitMs = 0);

// millis() when the IR LED last finished sending — the RX's own TSOP sees
// its IR LED, so IrRecord ignores anything received shortly after this.
uint32_t IrTxLastActiveMs();
bool     IrTxBusy();
