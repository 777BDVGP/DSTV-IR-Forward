#pragma once

// =============================================================================
// IrReceive.h — TSOP31238 receiver: live DSTV forwarding + record mode
// =============================================================================
// One receiver task (IrReceive.cpp) reads every IR frame from the TSOP:
//   DSTV (XMP) frame  → GREEN LED flash, and forwarded to the IR LED (rebuilt
//                       as clean XMP) unless forwarding is off or a recording
//                       is in progress
//   any other remote  → RED LED flash, never forwarded
//   record mode       → the frame (any remote) becomes the pending stroke on
//                       the IR SEQUENCES page (IrRecord.h)
// Held buttons: every repeat frame is forwarded, like TV_IR_Sending.
// =============================================================================

#include <Arduino.h>

void   SetupIrReceive();     // starts the receiver task (record API: IrRecord.h)

String IrForwardStateText(); // "Forwarding", "PAUSED - recording a sequence", "OFF (Settings)"
String IrLastDstvText();     // last DSTV code received, "" if none yet
