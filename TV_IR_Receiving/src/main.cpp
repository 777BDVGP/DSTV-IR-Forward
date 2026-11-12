// =============================================================================
// main.cpp — TV IR RX: ESP-NOW → IR forwarder (sits at the DSTV decoder)
// =============================================================================
//
// Platform  : WEMOS LOLIN S2 Mini (ESP32-S2, 240 MHz, 4 MB Flash, 2 MB PSRAM)
// Framework : Arduino / FreeRTOS  (PlatformIO)
//
// TX nodes (TV_Remote boards, one per room) decode DSTV IR in the
// bedrooms and send each frame here over ESP-NOW. This board replays it on
// the IR LED in front of the decoder.
//
// Tasks:
//   loopTask   — OTA, ESP-NOW beacon, WiFi watchdog
//   IrTxTask   — RMT IR output (IrTransmit.cpp)
//   WiFi task  — ESP-NOW receive callback (EspNowLink.cpp)
//   async_tcp  — web server (WebPage.cpp)
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
//     GPIO 4   RED LED  (330R)                   TX: IR received, NOT forwarded / RX: IR send error
//     GPIO 5   GREEN LED (330R)                  TX: DSTV forwarded / RX: IR sent
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
//   No TSOP at the RX? Record from the TX page /recseq instead (v1.6.0): the
//   remote is pointed at the TX, which sends the strokes over ESP-NOW; the RX
//   keeps the recording (RemoteRec.cpp, TX 18_RecSeq.cpp + 17_EspNow.cpp).
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
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "OwnerPass.h"
#include "Config.h"
#include "Status.h"
#include "IrTransmit.h"
#include "EspNowLink.h"
#include "WebPage.h"
#include "WebUtil.h"
#include "SeqStore.h"
#include "IrRecord.h"
#include "Sequencer.h"
#include "Scheduler.h"
#include "RouterCheck.h"
#include "WifiSetup.h"
#include <LittleFS.h>

static uint32_t wifiLostSinceMs = 0;
static bool     wifiWasUp       = false;

// -----------------------------------------------------------------------------
// MovWatch — log surge protection (TMOV) state changes
// -----------------------------------------------------------------------------
static void MovWatch() {
    // Mute ticked / auto-cleared (MOV replaced) → keep AllParams.json in step
    static bool lastMuted = BoardMovMuted();
    if (BoardMovMuted() != lastMuted) {
        lastMuted = BoardMovMuted();
        RxSaveParams();
    }

    static BoardMovState last = MOV_UNKNOWN;
    BoardMovState state = BoardMovGetState();
    if (state == last) return;
    if (state == MOV_BLOWN)                          StatusLog("SURGE PROTECTION (MOV) BLOWN - replace it");
    else if (state == MOV_OK && last == MOV_BLOWN)   StatusLog("Surge protection (MOV) OK again");
    last = state;
}

// -----------------------------------------------------------------------------
// SetupOTA
// -----------------------------------------------------------------------------
static void SetupOTA() {
    ArduinoOTA.setPort(3232);
    ArduinoOTA.setHostname(WifiConfig().host.c_str());
    ArduinoOTA
        .onStart([]() { OtaError = -1; OtaPercent = 0; StatusLog("OTA update started"); })
        .onProgress([](unsigned int done, unsigned int total) {
            if (total) OtaPercent = (int)((uint64_t)done * 100 / total);
        })
        .onEnd([]()   { OtaPercent = 100; Serial.println("[OTA] done"); })
        .onError([](ota_error_t e) {
            OtaError = (int)e; OtaPercent = -1;
            StatusLog("OTA error " + String((int)e));
        });
    // Network upload needs the owner password (OwnerPass.h). PlatformIO sends it
    // from the environment variable IR_OTA_PASS (upload_flags in platformio.ini).
    if (OwnerPassMd5().length() == 32) ArduinoOTA.setPasswordHash(OwnerPassMd5().c_str());
    ArduinoOTA.begin();
}

// -----------------------------------------------------------------------------
// WiFiWatchdog — log drops/reconnects; reboot if down for WIFI_LOST_REBOOT_MS
// -----------------------------------------------------------------------------
static void WiFiWatchdog() {
    bool up = WiFi.status() == WL_CONNECTED;
    if (up != wifiWasUp) {
        wifiWasUp = up;
        if (up) StatusLog("WiFi reconnected, channel " + String(WiFi.channel()));
        else  { StatusLog("WiFi lost"); wifiLostSinceMs = millis(); }
    }
    if (!up && millis() - wifiLostSinceMs > WIFI_LOST_REBOOT_MS) {
        StatusLog("REBOOT - WiFi lost for 10 min");
        StatusFlushLog();
        delay(200);
        ESP.restart();
    }
}

// -----------------------------------------------------------------------------
// RouterWatch — gateway ping once a minute (RouterCheck.cpp). Catches "WiFi
// connected but no traffic", which WiFiWatchdog() cannot see.
// -----------------------------------------------------------------------------
static void RouterWatch() {
    switch (RouterCheckLoop()) {
        case RC_RECONNECT:
            StatusLog("Router " + WiFi.gatewayIP().toString() + " unreachable for 3 min - reconnecting WiFi");
            WiFi.reconnect();
            break;
        case RC_REBOOT:
            StatusLog("REBOOT - router unreachable for 6 min");
            StatusFlushLog();
            delay(200);
            ESP.restart();
            break;
        case RC_RECOVERED:
            StatusLog("Router reachable again");
            break;
        default:
            break;
    }
}

// -----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[BOOT] " DEVICE_NAME " v" FW_VERSION);

    // Time zone first: after a soft reboot the clock is still valid, and the
    // first log lines must be in SAST, not UTC (NTP is started later)
    setenv("TZ", SCHED_TZ, 1);
    tzset();

    BoardSetup(BRD_HEARTBEAT_RX_MS);   // PCB pins, slow blue heartbeat, TMOV monitor
    SetupStatus();
    SetupIrTransmit();
    SetupSeqStore();
    SetupRxSettings();                 // System Name (/AllParams.json)
    SetupIrRecord();
    SetupSequencer();

    // WiFi settings: /WifiSettings.json (setup portal "IR-FORWARD-RX-xxxx" when missing)
    WifiSetupInit(LittleFS, "RX", StatusLog);
    if (!WifiStart(30000)) {
        StatusLog("REBOOT - WiFi connect failed");
        StatusFlushLog();
        delay(500);
        ESP.restart();
    }
    wifiWasUp = true;
    StatusLog("WiFi connected " + WiFi.localIP().toString() + " (" + WifiConfig().host + ")  ch " +
              String(WiFi.channel()) + "  RSSI " + String(WiFi.RSSI()) + " dBm  " + WifiApInfo());

    SetupOTA();
    SetupScheduler();
    SetupEspNow();
    SetupWebPage();

    StatusLog("- Up and Running after Reboot  (firmware " FW_VERSION ")");
}

void loop() {
    ArduinoOTA.handle();
    WifiSetupLoop();   // button 0 reset + reboot after a /wifi save
    EspNowLoop();
    WiFiWatchdog();
    RouterWatch();
    MovWatch();
    SchedulerLoop();
    StatusFlushLog();

    if (RebootRequested) {
        StatusLog("REBOOT - requested from web page");
        StatusFlushLog();
        delay(500);
        ESP.restart();
    }
    delay(10);
}
