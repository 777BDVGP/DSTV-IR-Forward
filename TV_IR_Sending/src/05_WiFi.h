#pragma once

// =============================================================================
// 05_WiFi.h — WiFi Station Connect (settings from /WifiSettings.json)
// =============================================================================
// SSID, password, channel lock, static IP / DHCP and host name come from
// /WifiSettings.json via WifiSetup.cpp, which also runs the setup portal when
// the file is missing. Returns false if the AP is not reached within 15 s
// (setup() then reboots; 3 failed boots in a row → setup portal).
// =============================================================================

bool SetupWiFi();
