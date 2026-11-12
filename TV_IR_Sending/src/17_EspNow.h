#pragma once

// =============================================================================
// 17_EspNow.h — ESP-NOW Sender: forwards DSTV IR frames to the TV IR RX
// =============================================================================
// The RX broadcasts a PKT_BEACON every 2 s. The first beacon
// tells us its MAC, which is then added as the unicast peer — no MAC is
// hard-coded, and a replaced RX board is picked up automatically.
// Packet format: include/IrLink.h (identical copy in TV_IR_Receiving).
// =============================================================================

#include "IrLink.h"

class decode_results;

// Link threshold
#define ESPNOW_RX_LOST_MS   30000UL   // no beacon this long → link lost

struct EspNowStats {
	bool     rxKnown;          // RX MAC learned from a beacon
	bool     linkUp;           // beacon seen within ESPNOW_RX_LOST_MS
	uint8_t  rxMac[6];
	char     rxName[IRLINK_NAME_LEN];   // RX System Name (from its beacon)
	uint32_t beaconAgeMs;      // time since last beacon
	uint32_t forwarded;        // frames handed to ESP-NOW
	uint32_t sentOk;           // frames acknowledged by the RX
	uint32_t sentFail;         // frames not acknowledged (after MAC retries)
	uint32_t skipped;          // frames not sent (RX unknown / too long)
	bool     rxIrSentKnown;    // RX firmware reports its IR-out count
	uint32_t rxIrSent;         // IR codes the RX played out on its IR LED (from beacon)
	uint8_t  lastFormat;       // IrLinkFormat of last forwarded frame (0 = none)
	uint64_t lastValue;
	uint16_t lastRawCount;
};

void        SetupEspNow();                          // after WiFi connected
void        EspNowSetRoomName(const String &name);  // Settings "System Name" (max 15 chars)
void        EspNowForwardIr(const decode_results *r);  // IRLoop
void        EspNowMaintain();                       // SystemLoop, every 10 ms
EspNowStats EspNowGetStats();                       // web page

// ── Recording a sequence for the RX (page /recseq, 18_RecSeq.cpp) ─────────────
bool     EspNowRecActive();                         // IRLoop: forward every remote + send strokes
void     EspNowRecStroke(const decode_results *r);  // IRLoop, while recording
uint16_t EspNowRecCommand(uint8_t cmd, const String &text, uint32_t delayMs, String &err);
                                                    // cmd = IrLinkRecCmdCode; cmdSeq, 0 = not sent
String   EspNowRecJson();                           // /api/rec
