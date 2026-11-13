#pragma once

// =============================================================================
// 09_IR.h — IR Receive, DSTV Detection & Forwarding
// =============================================================================
// Receives all IR via TSOP31238 on IPIN_IR_RECEIVE.
// Only the remote set in Settings "Forward remote" is forwarded — DSTV (XMP)
// by default; "Learn remote" takes it from the next button press
// (RemoteFilter.h). That remote → GREEN LED flash (GPIO 5), any other → RED
// (GPIO 4). Diagnostic tool on: logs every decoded frame to /IRLog.txt.
// =============================================================================

void   SetupIR();                              // start the IR receiver
void   IRLoop();                               // loop(), every 10 ms
bool   IrSetForwardRemote(const String &txt);  // false = not understood (filter unchanged)
String IrForwardRemoteText();                  // current setting, tidy ("NEC 0x20DF")
String IrForwardRemoteLabel();                 // for pages: "XMP (DSTV)", "NEC 0x20DF"
