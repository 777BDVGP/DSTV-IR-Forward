#pragma once

// =============================================================================
// 09_IR.h — IR Receive, DSTV Detection & Forwarding
// =============================================================================
// Receives all IR via TSOP31238 on IPIN_IR_RECEIVE.
// DSTV (XMP) frame received     → GREEN LED flash (GPIO 5).
// Any other (non-DSTV) frame     → RED LED flash (GPIO 4).
// In learn mode: logs every decoded frame to /IRLog.txt.
// =============================================================================

void SetupIR();     // start the IR receiver
void IRLoop();      // loop(), every 10 ms
