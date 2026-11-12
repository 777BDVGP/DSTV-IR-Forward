// =============================================================================
// 05_WiFi.cpp — WiFi Station Connect (settings from /WifiSettings.json)
// =============================================================================
// All WiFi settings live in /WifiSettings.json (WifiSetup.cpp, shared with the
// RX). Every board of one system must join the same AP channel for ESP-NOW,
// which is what the file's "channel" lock is for.
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include "01_Main.h"
#include "02_Time.h"
#include "03_Files.h"
#include "05_ThisSystem.h"
#include "05_WiFi.h"
#include "10_Reports.h"
#include "WifiSetup.h"

static void WifiLog(const String &s) { SaveGenReport r(s); }

bool SetupWiFi() {
    WifiSetupInit(SPIFFS, "TX", WifiLog);
    if (!WifiStart(15000)) return false;

    DEBUG_PRINT("WiFi: connected, IP = "); DEBUG_PRINTLN(WiFi.localIP().toString());
    SaveGenReport("WiFi connected " + WiFi.localIP().toString() + " (" + WifiConfig().host + ")" +
                  " ch " + String(WiFi.channel()) + " RSSI " + String(WiFi.RSSI()) + " " + WifiApInfo());
    return true;
}
