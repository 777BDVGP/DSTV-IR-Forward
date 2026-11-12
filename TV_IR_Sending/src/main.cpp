// =============================================================================
// main.cpp — Boot Sequence & Main Loop
// =============================================================================
//
// Platform  : WEMOS LOLIN S2 Mini (ESP32-S2, 240 MHz, 4 MB Flash, 2 MB PSRAM)
// Framework : Arduino / FreeRTOS  (PlatformIO)
//
// Everything runs from loop() every 10 ms (single-core S2 — the former
// FreeRTOS tasks gave no parallelism, only shared-data races):
//   TimeCheckLoop  watchdog, timers, debounce, scheduled reboot, heap monitor
//   IRLoop         decode IR, forward DSTV frames, red/green LED
//   SystemLoop     TMOV logging, ESP-NOW heartbeat / link state
//   WebSocketLoop  OTA, web page updates
// Only BoardTask (Board.cpp) stays a task: heartbeat + TMOV buzzer must also
// run while setup() is still connecting to WiFi.
// =============================================================================

// =============================================================================
// TV IR FORWARDING SYSTEM — PROJECT OVERVIEW
// (identical block in TV_IR_Sending/src/main.cpp and TV_IR_Receiving/src/main.cpp
//  — keep both copies in sync when updating)
// =============================================================================
//
// ── Why ──────────────────────────────────────────────────────────────────────
// An earlier project (TV_Remote) switched a relay that powered an "Ellies Smart Remote Blaster".
// The Blaster was unreliable, so it is replaced by our own IR forwarding:
// bedroom boards pick up the DSTV remote and send the codes by radio to a
// board at the DSTV decoder, which replays them as IR. Only DSTV (XMP) remote
// codes are forwarded; other remotes are ignored.
//
// Link options considered: home network (UDP/HTTP), STX882 433 MHz RF modules,
// ESP-NOW. Chosen: ESP-NOW — direct board-to-board WiFi frames, low latency,
// acknowledged delivery, no router/cloud needed for the IR path itself.
//
// ── Projects (all in this IR_FORWARD folder) ─────────────────────────────────
//   TV_IR_Sending    TX firmware — a COPY of TV_Remote (original left untouched)
//                    plus ESP-NOW sender (src/17_EspNow.cpp). One firmware for
//                    both TX boards; build flag -D TX_NODE=1/2 picks IP + name.
//   TV_IR_Receiving  RX firmware — written from scratch, small and dedicated.
//   (TV_IR_Forwarding was the first RX folder; superseded by TV_IR_Receiving.)
//
// ── Layout ───────────────────────────────────────────────────────────────────
//   Bedroom 1               Bedroom 2               Sitting room
//   TV IR TX 1  (.104)      TV IR TX 2  (.105)      TV IR RX  (.106)
//   TSOP ─► S2 Mini         TSOP ─► S2 Mini          S2 Mini ─► IR LED ─► DSTV
//              │                       │                ▲
//              └──────── ESP-NOW ──────┴────────────────┘
//
// ── Network ──────────────────────────────────────────────────────────────────
//   Each board has its own /WifiSettings.json (WIFI page / setup WiFi): home
//   WiFi, DHCP or a fixed IP, host name (default ir-rx-xxxx / ir-tx-xxxx).
//   ESP-NOW needs every board on ONE WiFi channel:
//     * one router / access point: leave "Channel lock" on Any — all boards
//       follow the router's channel
//     * several access points on different channels: set the same Channel
//       lock on every board, fix those APs' 2.4 GHz channel (not Auto) and, if
//       the system can lock clients to APs (e.g. Omada "Lock to AP"), allow
//       only the APs on that channel. Use /wifiscan on each board to choose.
//       ORDER when moving channel: board Channel lock first, AP lock last.
//   WiFi power-save is OFF (modem sleep drops ESP-NOW packets).
//   Router check (RouterCheck.cpp, identical in both): ping the gateway once a
//   minute; 3 failures → WiFi reconnect, 6 → reboot ("connected but no traffic").
//
// ── ESP-NOW protocol — include/IrLink.h (MUST be identical in both projects)
//   RX → broadcast   PKT_BEACON     every 2 s  — TX learns the RX MAC from it
//   TX → RX unicast  PKT_IR_FRAME   one per DSTV frame (seq no. → RX drops dups)
//   TX → RX unicast  PKT_HEARTBEAT  every 30 s — room name, RSSI, ok/fail counts
//   A TX is shown offline on the RX page after 95 s without a heartbeat.
//   Formats: FMT_XMP (RX rebuilds XMP from value/bits — exact timing)
//            FMT_RAW (RX replays raw mark/space µs; max 100 entries)
//   Packet ≤ 250 bytes (ESP-NOW limit, enforced by static_assert).
//
// ── Hardware — IR_FORWARD_V1 PCB (PCB/IR_FORWARD_V1.sch), one PCB for TX + RX
//   WEMOS LOLIN S2 Mini, 220 VAC in → MORNSUN LD05-23B05R2 (5 V) → AMS1117 3.3 V.
//   The loaded project decides the role. Pin map + shared code: src/Board.h/.cpp
//   (identical copy in both projects).
//     GPIO 1   TSOP31238 (IR_IN header)          TX: DSTV remote in
//     GPIO 2   IR OUT, 2K2 → 2N2222, 4 × 130R ∥  RX: IR LED(s) to decoder (RMT)
//     GPIO 3   BUZZER via 4K7 → 2N2222           TMOV-blown alarm
//     GPIO 4   RED LED  (330R)                   TX: non-DSTV IR received
//     GPIO 5   GREEN LED (330R)                  TX: DSTV IR received / RX: IR sent
//     GPIO 6   INPUT header (to GND)             function to be determined
//     GPIO 15  on-board blue LED                 RX: 1.5 s blink / TX: 300 ms blink
//     GPIO 36  4N35 — Littelfuse TMOV monitor    50 Hz pulses = OK, none = BLOWN
//   TMOV BLOWN → buzzer beeps (250 ms every 2 s) until muted on the web page
//   (TX: Settings checkbox, RX: MOV card). The mute is kept in NVS and clears
//   itself when the TMOV reads OK again. FUSE_LINK jumper off = test "blown".
//
// ── RX only: recorded IR sequences + weekly schedule ─────────────────────────
//   The RX board also has the TSOP fitted. Web page /sequences records a named
//   sequence step by step (any remote: DSTV, TV, sound bar, aircon): enter the
//   name → press a remote button → label it + set the delay after it → "Next IR"
//   or "Accept sequence as completed". Stored in LittleFS (/seq/<id>.json).
//   /schedule runs one or more sequences in order at HH:MM on ticked weekdays
//   (NTP time, SAST). DSTV strokes replay as exact XMP, others as raw 38 kHz.
//   Two IR LEDs in series on IR OUT: one at the DSTV decoder, one aimed at the
//   TV / sound bar / aircon. Code: IrRecord, SeqStore, Sequencer, Scheduler,
//   WebSequences, WebSchedule (TV_IR_Receiving/src).
//
// ── Version ──────────────────────────────────────────────────────────────────
//   One system version for TX and RX (FW_VERSION in TX 01_Main.h and RX
//   Config.h, shown on both About pages). Raise BOTH whenever either changes.
//
// ── Build & flash (PlatformIO, espressif32@6.6.0, partitions_ota.csv:
//    2 × 1.5 MB app + 960 KB SPIFFS) ─────────────────────────────────────────
//   First flash over USB (hold BOOT(0), tap RST), afterwards OTA:
//   TX:  pio run -e usb -t upload       /  pio run -t upload --upload-port <TX ip>
//   RX:  pio run -e rx_usb -t upload    /  pio run -e rx_ota -t upload (.106)
//
// ── WiFi ─────────────────────────────────────────────────────────────────────
//   /WifiSettings.json on each board (WifiSetup.h). Missing → setup portal:
//   join the open WiFi "IR-FORWARD-TX-xxxx" / "IR-FORWARD-RX-xxxx", open
//   http://192.168.4.1, enter the home WiFi and choose the owner password
//   (OwnerPass.h - needed for later WiFi changes and network uploads; set it on
//   the PC as IR_OTA_PASS). Change later on the WIFI page; hold button 0 for
//   5 s to delete the settings and get the portal again.
//
// ── Open items / decisions pending ───────────────────────────────────────────
//   1. INPUT (GPIO 6) — function to be determined (read + debounced only:
//      BoardInputActive()).
//   2. BUZZER — further uses to be determined (now TMOV alarm only).
//   Done: TX local IR repeat removed; TRIAC/relay/indicator LED code removed.
// =============================================================================

#include <Arduino.h>
#include "all_includes.h"

// Default loop() stack is 8 KB; the web page builders need more
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static uint32_t lastPrint = 0;
String PrevMessage = "";
String FSInitLog   = "";

void TimeCheckLoop();
void SystemLoop();
void WebSocketLoop();

void setup(){
	Serial.begin(115200);
	#if DEBUG
		delay(5000);
	#endif

	InitHardware();

	InitFileSystem();

	delay(1000);

	DEBUG_PRINTLN("\nBootUp " TX_ROOM_NAME);

	if (FSInitLog.length() > 0) DEBUG_PRINT(FSInitLog);

	if (!SetupWiFi()) {
		SaveGenReport F("REBOOT - WiFi connect failed");
		delay(500);
		ESP.restart();
	}
	else {
	 
		DEBUG_PRINTLN("Server has been Connected!");
		SaveGenReport("heap after WiFi connect: " + String(ESP.getFreeHeap()));
		SetupTime();
		SaveGenReport("heap after SetupTime: " + String(ESP.getFreeHeap()));
		SetupAbout();
		SaveGenReport("heap after SetupAbout: " + String(ESP.getFreeHeap()));
		SetupOTA();
		SaveGenReport("heap after SetupOTA: " + String(ESP.getFreeHeap()));
		SetupWebPage();
		SaveGenReport("heap after SetupWebPage: " + String(ESP.getFreeHeap()));
		SetupEspNow();
		SaveGenReport("heap after SetupEspNow: " + String(ESP.getFreeHeap()));
		SetupIR();
		SaveGenReport("heap after SetupIR: " + String(ESP.getFreeHeap()));

		SaveGenReport F("- Up and Running after Reboot");

		IPAddress locIP = WiFi.localIP();
		localIP = locIP.toString();

		activateTimer(TIM_RESTART_DELAY);

		WatchdogSetup();
		activateTimer(TIM_STABILIZE_DELAY);

		DEBUG_PRINTLN("Main loop started");
	}
}

void loop() {
	TimeCheckLoop();
	IRLoop();
	WifiSetupLoop();   // button 0 reset + reboot after a /wifi save
	SystemLoop();
	WebSocketLoop();
	delay(10);
}

void PrintTestMsg(String Message, uint32_t seconds){
	if (Message != PrevMessage) {
		PrevMessage = Message;
		lastPrint   = millis();
		DEBUG_PRINTLN(Message);
	} else if (millis() - lastPrint >= seconds * 1000UL) {
		lastPrint = millis();
		DEBUG_PRINTLN(Message);
	}
}
