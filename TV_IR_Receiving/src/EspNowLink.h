#pragma once

// =============================================================================
// EspNowLink.h — ESP-NOW receiver side of the IR link
// =============================================================================
// Receives PKT_IR_FRAME / PKT_HEARTBEAT from any TX node and broadcasts a
// PKT_BEACON every IRLINK_BEACON_MS so TX nodes can learn this board's MAC.
// ESP-NOW runs on the channel of the WiFi access point, so every TX node must
// be on the same WiFi channel (see main.cpp, Network).
// =============================================================================

#include <stddef.h>

bool SetupEspNow();     // call after WiFi is connected
void EspNowLoop();      // call from loop(): sends the beacon, handles record packets
void EspNowBroadcast(const void *data, size_t len);   // PKT_REC_STATUS (RemoteRec.cpp)
