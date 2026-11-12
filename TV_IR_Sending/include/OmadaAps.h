#pragma once

// =============================================================================
// OmadaAps.h — name of the access point this board is connected to
// =============================================================================
// SHARED FILE: identical copy in TV_IR_Sending/include and
// TV_IR_Receiving/include — keep in sync.
//
// Shown on the status page and on /wifiscan. The names are your own, so they
// come from the optional src/Private.h (never committed):
//   #define AP_NAMES { \
//       { {0x8C,0x86,0xDD,0x00,0x00,0x01}, "LivingRoom" }, \
//       { {0x8C,0x86,0xDD,0x00,0x00,0x02}, "Bedroom"    }, \
//   }
// The MAC is the access point's 2.4 GHz BSSID (on Omada / TP-Link EAPs the
// device MAC in the controller). Without AP_NAMES the BSSID itself is shown.
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>

#if __has_include("Private.h")
#include "Private.h"
#endif

struct OmadaAp { uint8_t mac[6]; const char *name; };

#ifdef AP_NAMES
static const OmadaAp OMADA_APS[] = AP_NAMES;
#else
static const OmadaAp OMADA_APS[] = { { {0, 0, 0, 0, 0, 0}, nullptr } };
#endif

// Name of the AP we are connected to: its name from AP_NAMES, raw BSSID if
// unknown, or "-" when WiFi is down.
inline String OmadaApName() {
	if (WiFi.status() != WL_CONNECTED) return "-";
	const uint8_t *b = WiFi.BSSID();
	if (!b) return "-";
	for (const OmadaAp &ap : OMADA_APS) {
		if (ap.name && memcmp(ap.mac, b, 6) == 0) return ap.name;
	}
	return WiFi.BSSIDstr();
}
