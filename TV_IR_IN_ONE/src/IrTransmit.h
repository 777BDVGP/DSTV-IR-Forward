#pragma once

// =============================================================================
// IrTransmit.h — IR LED output via the ESP32-S2 RMT peripheral
// =============================================================================
// RMT generates the 38 kHz carrier and every mark/space in hardware, so WiFi /
// web server interrupts on the single S2 core cannot distort the timing (XMP
// encodes 4 bits per space in 135 µs steps and does not tolerate jitter).
// Live DSTV codes (IrReceive) and recorded strokes (sequences) share one
// queue and are sent one at a time by IrTxTask, so they never overlap.
// The IR LED is next door (on wires) — no LEDs on the output side; send
// errors are only logged.
// =============================================================================

#include "IrCode.h"

void SetupIrTransmit();
bool QueueDstvCode(uint64_t value, uint16_t bits);   // live DSTV frame; false if queue full
bool ResendLastDstvCode();                           // web "Resend last code" button

// Recorded stroke. waitMs > 0: block until it has been sent (or timeout).
bool SendIrCode(const IrCode &code, uint32_t waitMs = 0);

// millis() when the IR LED last finished sending — record mode (and the
// optional echo guard) ignore anything received during / shortly after this.
uint32_t IrTxLastActiveMs();
bool     IrTxBusy();
