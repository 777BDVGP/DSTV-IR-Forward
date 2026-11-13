// =============================================================================
// main.cpp — TV IR IN ONE: DSTV IR in → IR LED out, on one board
// =============================================================================
//
// Platform  : WEMOS LOLIN S2 Mini (ESP32-S2, 240 MHz, 4 MB Flash, 2 MB PSRAM)
// Framework : Arduino / FreeRTOS  (PlatformIO)
//
// Tasks:
//   loopTask       — OTA, WiFi watchdog, router check, log flush
//   IrReceiveTask  — TSOP decode: DSTV forwarding + record mode (IrReceive.cpp)
//   IrTxTask       — RMT IR output (IrTransmit.cpp)
//   SeqTask        — plays recorded sequences (Sequencer.cpp)
//   BoardTask      — LEDs, heartbeat, TMOV monitor, buzzer (Board.cpp)
//   async_tcp      — web server (WebPage.cpp)
// =============================================================================

// =============================================================================
// TV IR IN ONE — PROJECT OVERVIEW
// =============================================================================
//
// ── Why ──────────────────────────────────────────────────────────────────────
// For a house where the TV room and the decoder are next to each other. Made
// from TV_IR_Sending (TX) and TV_IR_Receiving (RX): there a TX board in a
// bedroom decodes the DSTV remote and sends the codes over ESP-NOW to an RX
// board at the decoder. Here ONE board does both: the DSTV remote is received on IR IN and sent out again on
// IR OUT, and the IR LED is wired through the wall to the decoder next door.
// No ESP-NOW, no second board.
//
// ── Layout ───────────────────────────────────────────────────────────────────
//   Room with the TV                         Room next door
//   DSTV remote ─► TSOP ─► S2 Mini ── wires ──► IR LED ─► DSTV decoder
//
// ── IR rules ─────────────────────────────────────────────────────────────────
//   * Only DSTV (XMP) codes are forwarded; other remotes are ignored. Since
//     1.3.0 another TV box's remote can be chosen instead (Settings "Forward
//     remote" / "Learn remote", RemoteFilter.h), sent out as raw timing.
//   * Forwarded codes are rebuilt as clean XMP (exact timing via RMT), not
//     copied raw, so a poorly received frame still reaches the decoder clean.
//     A tolerant XMP decode (RecoverXmp, from TV_IR_Sending v1.4.4) catches
//     frames whose marks the TSOP stretched.
//   * Every frame of a held button is forwarded (volume / channel repeat).
//   * LEDs as TV_IR_Sending: GREEN = DSTV code received, RED = other remote.
//     There are no LEDs on the IR OUT side.
//   * Echo guard (Settings, default OFF): would ignore DSTV codes received
//     while the IR LED sends. Not needed with the LED next door, and it would
//     swallow held-button repeats.
//   * Settings "Forward DSTV codes" = the TV_IR_Sending Disable / Enable System.
//
// ── IR sequences (from TV_IR_Receiving, without the schedule) ────────────────
//   /sequences records a named sequence step by step with any remote (DSTV,
//   TV, sound bar, aircon) on this board's TSOP, and plays it on the IR LED when
//   "Run" is pressed. DSTV strokes replay as exact XMP, others as raw 38 kHz.
//   While a recording is open, DSTV forwarding is paused so recording a DSTV
//   button does not also change the decoder. Stored in LittleFS /seq/<id>.json.
//
// ── Network ──────────────────────────────────────────────────────────────────
//   /WifiSettings.json holds a list of up to 4 networks (WifiSetup.h), entered
//   in the setup WiFi on first boot (or pre-loaded from the optional
//   src/Private.h) — e.g. the house where it is used and the workshop where it
//   is built. On boot the board scans and joins whichever is in range. Host name
//   tv-ir-in-one. Router check: ping the gateway once a minute; 3 failures →
//   WiFi reconnect, 6 → reboot.
//
// ── Hardware — IR_FORWARD_V1 PCB (PCB/IR_FORWARD_V1.sch), same PCB as TX / RX
//     GPIO 1   TSOP31238 (IR_IN header)          DSTV remote in
//     GPIO 2   IR OUT, 2K2 → 2N2222, 4 × 130R ∥  IR LED(s) next door (RMT)
//     GPIO 3   BUZZER via 4K7 → 2N2222           TMOV-blown alarm
//     GPIO 4   RED LED  (330R)                   IR received, not DSTV
//     GPIO 5   GREEN LED (330R)                  DSTV received (and forwarded)
//     GPIO 6   INPUT header (to GND)             not used
//     GPIO 15  on-board blue LED                 heartbeat, 800 ms blink
//     GPIO 36  4N35 — Littelfuse TMOV monitor    50 Hz pulses = OK, none = BLOWN
//   TMOV BLOWN → buzzer beeps (250 ms every 2 s) until muted on Settings.
//
// ── Build & flash (PlatformIO, espressif32@6.6.0, partitions_ota.csv) ────────
//   First flash over USB (hold BOOT(0), tap RST):  pio run -e usb -t upload
//   Afterwards OTA:  pio run -e ota -t upload --upload-port <board ip>
//                    (owner password in the environment variable IR_OTA_PASS)
//   Or in a browser: http://<board ip>/update with firmware_out/*.bin
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "OwnerPass.h"
#include <LittleFS.h>
#include "Config.h"
#include "Status.h"
#include "Clock.h"
#include "IrTransmit.h"
#include "IrReceive.h"
#include "WebPage.h"
#include "WebUtil.h"
#include "SeqStore.h"
#include "Sequencer.h"
#include "Scheduler.h"
#include "RouterCheck.h"
#include "WifiSetup.h"

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
        SaveParams();
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
        else  { StatusLog("WiFi lost (IR forwarding keeps working)"); wifiLostSinceMs = millis(); }
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
    ClockSetTimeZone();

    BoardSetup(BRD_HEARTBEAT_IN_ONE_MS);   // PCB pins, heartbeat, TMOV monitor
    SetupStatus();
    SetupIrTransmit();
    SetupSeqStore();                       // mounts LittleFS
    SetupSettings();                       // /AllParams.json
    SetupSequencer();
    SetupScheduler();                      // /schedule.json, runs once the clock is synced
    SetupIrReceive();                      // DSTV forwarding works from here on, also without WiFi

    // WiFi: /WifiSettings.json, list of networks (setup WiFi on first boot)
    WifiSetupInit(LittleFS, "IN1", StatusLog);
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
    SetupClock();
    SetupWebPage();

    StatusLog("- Up and Running after Reboot  (firmware " FW_VERSION ")");
}

void loop() {
    ArduinoOTA.handle();
    WifiSetupLoop();   // button 0 reset + reboot after a /wifi save
    WiFiWatchdog();
    RouterWatch();
    MovWatch();
    ClockLoop();
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
