#pragma once

// =============================================================================
// 08_Home.h — Home Page Globals & Display Constants
// =============================================================================

// ─── Process State Machine Constants ─────────────────────────────────────────
	#define NoStat          0
	#define Stat1           1
	#define Stat2           2
	#define Stat3           3
	#define Stat4           4
	#define Stat5           5

// ─── Home Page Row Index Constants ───────────────────────────────────────────
// ESP-NOW link to the TV IR RX (values refreshed in MaintainHomePage)
	#define HOME_RX_LINK       0   // Linked / Searching / Lost
	#define HOME_RX_ROOM       1   // Receiver Room = RX System Name (from its beacon)
	#define HOME_RX_MAC        2   // Receiver MAC learned from its beacon
	#define HOME_RX_BEACON     3   // Time since last receiver beacon
	#define HOME_FORWARDED     4   // DSTV frames forwarded
	#define HOME_IR_SENT       5   // IR codes the RX played out on its IR LED (from beacon)
	#define HOME_DELIVERED     6   // Frames + heartbeats acknowledged by RX
	#define HOME_FAILED        7   // Not acknowledged
	#define HOME_LAST_CODE     8   // Last forwarded DSTV code
	#define HOME_WIFI          9   // WiFi channel / RSSI
	#define HOME_AP           10   // Omada AP name (from connected BSSID, OmadaAps.h)
	#define HOME_MY_MAC       11   // This TX board's own WiFi MAC
	#define HOME_ROUTER       12   // Router (gateway) ping result
	#define HOME_MOV          13   // Surge protection (TMOV) OK / BLOWN

	#define PAGE_VAR_HOME     14

	inline bool     HomePageReady = false;
	inline PageVar* HomeList      = nullptr;
	inline int      InternetSec   = 0;   // Current seconds value from SNTP (updated by CompileDateTime)

// ─── Background Colour Attribute Strings ─────────────────────────────────────
	#define ATT_RED    "color:white;background-color:red;padding:4px 10px;border-radius:6px"
	#define ATT_WHITE  "color:#e2e8f0"
	#define ATT_ORANGE "color:black;background-color:orange;padding:4px 10px;border-radius:6px"
	#define ATT_YELLOW "color:black;background-color:yellow;padding:4px 10px;border-radius:6px"
	#define ATT_GREEN  "color:white;background-color:green;padding:4px 10px;border-radius:6px"

// ─── Button Attribute Strings ─────────────────────────────────────────────────
	inline String ONATT  = F("color:white;background-color:red;padding:4px 10px;border-radius:6px");
	inline String OFFATT = F("color:white;background-color:green;padding:4px 10px;border-radius:6px");
