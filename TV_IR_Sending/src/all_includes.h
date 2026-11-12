// =============================================================================
// all_includes.h — Single Aggregate Include for All Firmware Modules
// =============================================================================
#pragma once

#include "01_Main.h"        // Global flags, libraries, pin aliases, watchdog
#include "02_Time.h"        // Software timer engine, NTP, date/time utilities
#include "03_Files.h"       // SPIFFS init, file manager route handlers
#include "04_WebPages.h"    // AsyncWebServer, WebSocket, PageVar/PageElement, WT_* builders
#include "05_ThisSystem.h"  // I/O arrays, debounce, SystemLoop, AddRoutes
#include "08_Home.h"        // Home page, MaintainHomePage, PageVar list
#include "09_IR.h"          // IR receive, DSTV detection, output control
#include "10_Reports.h"     // SaveGenReport stub
#include "12_Settings.h"    // AllParams class, settings page, PageBuf
#include "15_About.h"       // OTA update service, About page, ArduinoOTA
#include "17_EspNow.h"      // ESP-NOW sender: DSTV frames → TV IR RX
#include "18_RecSeq.h"      // /recseq: record an IR sequence for the RX
#include "WifiSetup.h"      // /WifiSettings.json, setup portal, /wifi page
#include "forward_decls.h"  // All cross-file forward declarations
