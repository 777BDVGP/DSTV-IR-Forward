#pragma once

// =============================================================================
// WifiSetup.h — WiFi from /WifiSettings.json (list of networks) + setup portal
// =============================================================================
// TV_IR_IN_ONE version: based on the TX/RX WifiSetup, but the file holds a LIST
// of networks, so a board can be built in one house and used in another.
// No ESP-NOW here, so there is no channel lock.
//
// /WifiSettings.json (entered in the setup portal on first boot — or written
// from WIFI_DEFAULT_NETS in the optional src/Private.h — changed later on the
// /wifi page or the setup portal):
//   {"ver":2,"host":"tv-ir-in-one","networks":[
//     {"ssid":"HomeWiFi","pass":"...","dhcp":false,"ip":"192.168.1.50",
//      "gw":"192.168.1.1","sn":"255.255.255.0","dns":"192.168.1.1"},
//     {"ssid":"Workshop","pass":"...","dhcp":true}]}
//   A file without "ver":2 (e.g. a TX/RX ver 1 file) is replaced by the defaults.
//
// Boot:
//   no file / old file   → Private.h networks: write them, continue
//                          none (public build): setup portal until saved
//   scan                 → join the listed SSID that is seen (both seen: the
//                          stronger one), with that network's IP settings
//   none seen / fails    → caller reboots; after 3 failed boots the setup
//                          portal runs for 5 min, then the saved list is retried
//                          (a router outage must not leave the board in setup)
// Setup portal: own WiFi "IR-FORWARD-IN1-XXXX" (XXXX = end of the MAC),
//   OPEN WiFi (no password in the code), page at http://192.168.4.1 (captive
//   portal). Saving needs the owner password (OwnerPass.h); on a new board
//   the owner chooses it on this page.
//   Red/green LEDs flash alternately while the portal is active.
// Reset: hold the S2 Mini button "0" for 5 s while running → the file is
//   rewritten with the Private.h networks, or deleted (setup portal), reboot.
// =============================================================================

#include <Arduino.h>
#include <FS.h>
#include <IPAddress.h>
#include <vector>

class AsyncWebServer;

#define WIFI_SETTINGS_FILE     "/WifiSettings.json"
#define WIFI_FILE_VERSION      2
#define WIFI_MAX_NETS          4               // networks in the list
#define WIFI_PORTAL_TIMEOUT_MS (5UL * 60UL * 1000UL)
#define WIFI_MAX_FAILED_BOOTS  3
#define WIFI_RESET_PIN         0               // S2 Mini button "0" (LOW = pressed)
#define WIFI_RESET_HOLD_MS     5000

struct WifiNet {
	String    ssid;
	String    pass;
	bool      dhcp = true;
	IPAddress ip, gw, sn, dns;
};

struct WifiCfg {
	String               host;
	std::vector<WifiNet> nets;
};

// role "IN1" (portal name + default host); log = the project's event log.
void           WifiSetupInit(fs::FS &fs, const char *role, void (*log)(const String &));

// Load the file (defaults if missing), scan and connect. Runs the portal (does
// not return) after WIFI_MAX_FAILED_BOOTS failed boots. false = connect failed
// this time → the caller reboots.
bool           WifiStart(uint32_t connectTimeoutMs);

const WifiCfg &WifiConfig();     // settings in use
const WifiNet *WifiActiveNet();  // network joined (nullptr before connect)
String         WifiApInfo();     // WiFi network name (e.g. "HomeWiFi"), for the log

void           WifiSetupLoop();  // loop(): reset button + reboot after a /wifi save
void           WifiAddRoutes(AsyncWebServer &server);   // /wifi page (view / change / defaults)
