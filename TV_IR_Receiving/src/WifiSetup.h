#pragma once

// =============================================================================
// WifiSetup.h — WiFi from /WifiSettings.json + setup portal
// =============================================================================
// IDENTICAL copy in TV_IR_Sending/src and TV_IR_Receiving/src — keep in sync.
//
// /WifiSettings.json (written by the portal or the /wifi page):
//   {"ver":1,"ssid":"MyWiFi","pass":"secret","channel":1,"dhcp":false,
//    "ip":"192.168.1.50","gw":"192.168.1.1","sn":"255.255.255.0",
//    "dns":"192.168.1.1","host":"tv-ir-tx1"}
//   channel 0  = join the strongest AP of that SSID on any channel
//   channel N  = only an AP on channel N (ESP-NOW: RX and every TX must share
//                one channel — needed when several APs use different channels)
//   channel 99 = TX only, "Follow receiver": lock to the channel where the RX
//                beacon was last heard (NVS "wifi"/"rxch"); the TX searches
//                channels 1-13 for the RX when it is lost (17_EspNow.cpp)
//   A file without "ver":1 (e.g. an old TV_Remote file) is ignored.
//
// Boot:
//   valid file           → connect (static IP or DHCP, optional channel lock)
//   no valid file        → setup portal until saved
//   3 failed boots       → setup portal for 5 min, then retry the saved WiFi
//                          (a router outage must not leave the boards in setup)
// Setup portal: own WiFi "IR-FORWARD-<role>-XXXX" (XXXX = end of the MAC),
//   OPEN WiFi (no password in the code), page at http://192.168.4.1 (captive
//   portal). Saving needs the owner password (OwnerPass.h); on a new board
//   the owner chooses it on this page.
//   Red/green LEDs flash alternately while the portal is active.
// Reset: hold the S2 Mini button "0" for 5 s while running → file deleted,
//   reboot into the setup portal.
// =============================================================================

#include <Arduino.h>
#include <FS.h>
#include <IPAddress.h>

class AsyncWebServer;

#define WIFI_SETTINGS_FILE     "/WifiSettings.json"
#define WIFI_PORTAL_TIMEOUT_MS (5UL * 60UL * 1000UL)
#define WIFI_MAX_FAILED_BOOTS  3
#define WIFI_RESET_PIN         0               // S2 Mini button "0" (LOW = pressed)
#define WIFI_RESET_HOLD_MS     5000
#define WIFI_CH_FOLLOW_RX      99              // channel lock "Follow receiver" (TX)

struct WifiCfg {
	String    ssid;
	String    pass;
	String    host;
	uint8_t   channel = 0;      // 0 = any
	bool      dhcp    = true;
	IPAddress ip, gw, sn, dns;
};

// role "TX" / "RX" (portal name + default host); log = the project's event log.
void           WifiSetupInit(fs::FS &fs, const char *role, void (*log)(const String &));

// Load the file and connect. Runs the portal (does not return) when there is no
// valid file or after WIFI_MAX_FAILED_BOOTS failed boots. false = connect
// failed this time → the caller reboots, as before.
bool           WifiStart(uint32_t connectTimeoutMs);

const WifiCfg &WifiConfig();     // settings in use
String         WifiApInfo();     // "AP xx:xx.. (scan -60 dBm)" or a warning, for the log

void           WifiSetupLoop();  // loop(): reset button + reboot after a /wifi save

// "Follow receiver" (channel lock WIFI_CH_FOLLOW_RX, TX only)
bool           WifiFollowRx();               // this board follows the receiver
uint8_t        WifiFollowChannel();          // last channel the RX was heard on (0 = unknown)
void           WifiFollowStore(uint8_t ch);  // remember it (used from the next connect)
void           WifiSearchMode(bool on);      // RX search running: no auto-reconnect,
                                             // lost-WiFi / channel checks paused
void           WifiAddRoutes(AsyncWebServer &server);   // /wifi page (view / change / forget)
