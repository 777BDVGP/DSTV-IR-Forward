#pragma once

// =============================================================================
// IrReceive.h — TSOP31238 receiver: live DSTV forwarding + record mode
// =============================================================================
// One receiver task (IrReceive.cpp) reads every IR frame from the TSOP:
//   forward remote    → GREEN LED flash, and sent out on the IR LED unless
//                       forwarding is off or a recording is in progress.
//                       Settings "Forward remote": DSTV (XMP, rebuilt as clean
//                       XMP) by default, or a learnt remote (sent as raw
//                       timing) — RemoteFilter.h
//   any other remote  → RED LED flash, never forwarded
//   record mode       → the frame (any remote) becomes the pending stroke on
//                       the IR SEQUENCES page (IrRecord.h)
// Held buttons: every repeat frame is forwarded, like TV_IR_Sending.
// =============================================================================

#include <Arduino.h>

void   SetupIrReceive();     // starts the receiver task (record API: IrRecord.h)

String IrForwardStateText(); // "Forwarding", "PAUSED - recording a sequence", "OFF (Settings)"
String IrLastDstvText();     // last forwarded code, "" if none yet

bool   IrSetForwardRemote(const String &txt);  // false = not understood (filter unchanged)
String IrForwardRemoteText();                  // current setting, tidy ("NEC 0x20DF")
String IrForwardRemoteLabel();                 // for pages: "XMP (DSTV)", "NEC 0x20DF"
