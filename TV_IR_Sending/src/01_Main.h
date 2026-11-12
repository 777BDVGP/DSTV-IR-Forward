#pragma once

// =============================================================================
// 01_Main.h — Global Definitions, System Libraries & Core State Variables
// =============================================================================

// ─── Runtime Object Presence Flags ───────────────────────────────────────────
	inline bool MainPageObject       = false;  // Home page PageVar list allocated

// ─── Board Revision ──────────────────────────────────────────────────────────
	#define BRD_VERSION 1
	#define FW_VERSION  "1.7.0"   // system version — keep equal to RX Config.h

// ─── Debug / Production Switch ───────────────────────────────────────────────
	#define DEBUG 1

	#if DEBUG
		#define DEBUG_PRINT(x)          Serial.print(x)
		#define DEBUG_PRINTLN(x)        Serial.println(x)
		#define DEBUG_PRINTF(x, ...)    Serial.printf(x, ##__VA_ARGS__)
	#else
		#define DEBUG_PRINT(x)
		#define DEBUG_PRINTLN(x)
		#define DEBUG_PRINTF(x, ...)
	#endif

// ─── TX Identity ─────────────────────────────────────────────────────────────
// One firmware for every TX board. WiFi, IP and host name come from
// /WifiSettings.json (WifiSetup.h — setup portal when missing); the room name
// is Settings "System Name".
	#define TX_ROOM_NAME  "TV IR TX"   // default room name — Settings "System Name" overrides it

// ─── Third-Party Library Includes ────────────────────────────────────────────
	#include <WiFi.h>
	#include <WiFiUdp.h>
	#include <HTTPClient.h>
	#include <SPIFFS.h>
	#include <ESPAsyncWebServer.h>
	#include <ArduinoJson.h>
	#include <Ticker.h>
	#include <ESPPerfectTime.h>
	#include "EmonLib.h"

// ─── Heap Monitor Thresholds ─────────────────────────────────────────────────
	#define MIN_FREE_HEAP    10000
	#define HEAP_FREE_MIN    18000
	#define HEAP_MAXBLK_MIN  10000

// ─── Watchdog Variables ───────────────────────────────────────────────────────
	inline Ticker secondTick;
	inline volatile int watchdogCount = 0;
	inline String localIP;
	inline long int cycles = 0;

// ─── Pre-Boot WiFi Setup ──────────────────────────────────────────────────────
	inline Ticker MsTick;

// ─── Global System State Flags ────────────────────────────────────────────────
	inline bool NORMAL_OPERATION = true;
	inline bool RAIN_OK_DO_IRRI  = true;
	inline bool MANUAL_STARTED   = false;
	inline bool FILTER_ON        = false;
