// =============================================================================
// Status.cpp — Counters, event log and TX-room table shown on the web page
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"
#include "Status.h"
#include <LittleFS.h>
#include <vector>
#include "Scheduler.h"      // SchedTimeText() — same date/time text as the TX log
#include "RouterCheck.h"
#include "OmadaAps.h"     // Omada AP name from the connected BSSID

String RxSysName();   // WebSettings.cpp

struct RoomInfo {
    bool     used;
    uint8_t  mac[6];
    char     name[IRLINK_NAME_LEN];
    uint32_t lastSeenMs;
    uint32_t frames;
    uint16_t lastSeq;
    bool     haveSeq;
    bool     online;       // last evaluated state, for online/offline log lines
    int8_t   txRssi;
    uint32_t txUptimeSec;
    uint32_t txSentOk;
    uint32_t txSentFail;
};

static SemaphoreHandle_t mtx = NULL;
static RoomInfo rooms[STATUS_MAX_ROOMS];
static String   logLines[STATUS_LOG_LINES];
static uint8_t  logHead  = 0;    // next slot to write
static uint8_t  logCount = 0;

static uint32_t irSent = 0, irErrors = 0, irDropped = 0;

// Lines waiting to be appended to /GeneralLog.txt by StatusFlushLog() (loop()).
// Logging also happens in the WiFi task (ESP-NOW callback) and before LittleFS
// is mounted, so nothing writes flash directly from logLocked().
#define GENLOG_PATH        "/GeneralLog.txt"
#define GENLOG_KEEP_LINES  200        // like the TX: keep the last 200 lines
#define GENLOG_TRIM_BYTES  24000      // trim once the file grows past this
#define GENLOG_PENDING_MAX 60
static std::vector<String> pendingFile;
static bool fsReady = false;

// -----------------------------------------------------------------------------
static String macToString(const uint8_t *mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

static String uptimeStamp() {
    uint32_t s = millis() / 1000;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
             (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
    return String(buf);
}

static String jsonEscape(const String &s) {
    String out;
    out.reserve(s.length() + 4);
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if ((uint8_t)c < 0x20) out += ' ';
        else out += c;
    }
    return out;
}

// Caller holds mtx
static void logLocked(const String &line) {
    String entry = uptimeStamp() + "  " + line;
    Serial.println(entry);
    pendingFile.push_back(SchedTimeText() + " - " + line);
    if (pendingFile.size() > GENLOG_PENDING_MAX) pendingFile.erase(pendingFile.begin());
    logLines[logHead] = entry;
    logHead = (logHead + 1) % STATUS_LOG_LINES;
    if (logCount < STATUS_LOG_LINES) logCount++;
}

// Caller holds mtx. Finds the room for this MAC, or claims a free slot.
static RoomInfo *roomForMac(const uint8_t *mac, const char *name) {
    RoomInfo *freeSlot = NULL;
    for (auto &r : rooms) {
        if (r.used && memcmp(r.mac, mac, 6) == 0) return &r;
        if (!r.used && !freeSlot) freeSlot = &r;
    }
    if (!freeSlot) return NULL;
    memset(freeSlot, 0, sizeof(RoomInfo));
    freeSlot->used = true;
    memcpy(freeSlot->mac, mac, 6);
    strlcpy(freeSlot->name, name, IRLINK_NAME_LEN);
    logLocked("New TX node '" + String(freeSlot->name) + "' " + macToString(mac));
    return freeSlot;
}

// Caller holds mtx
static void touchRoom(RoomInfo *r, const char *name) {
    strlcpy(r->name, name, IRLINK_NAME_LEN);
    r->lastSeenMs = millis();
    if (!r->online) {
        r->online = true;
        logLocked("'" + String(r->name) + "' online");
    }
}

// -----------------------------------------------------------------------------
void SetupStatus() {
    mtx = xSemaphoreCreateMutex();
    memset(rooms, 0, sizeof(rooms));
}

// -----------------------------------------------------------------------------
// trimGeneralLog — keep the last GENLOG_KEEP_LINES lines
// -----------------------------------------------------------------------------
static void trimGeneralLog() {
    File f = LittleFS.open(GENLOG_PATH, "r");
    if (!f) return;
    size_t size = f.size();
    if (size <= GENLOG_TRIM_BYTES) { f.close(); return; }
    char *buf = (char *)ps_malloc(size + 1);
    if (!buf) { f.close(); return; }
    size_t n = f.readBytes(buf, size);
    f.close();
    buf[n] = '\0';
    size_t start = n, lines = 0;
    while (start > 0) {                          // walk back GENLOG_KEEP_LINES line ends
        if (buf[start - 1] == '\n' && start != n && ++lines >= GENLOG_KEEP_LINES) break;
        start--;
    }
    File w = LittleFS.open(GENLOG_PATH, "w");
    if (w) { w.write((const uint8_t *)buf + start, n - start); w.close(); }
    free(buf);
}

// -----------------------------------------------------------------------------
// StatusFlushLog — append queued lines to /GeneralLog.txt (call from loop())
// -----------------------------------------------------------------------------
void StatusFlushLog() {
    if (!fsReady) {
        fsReady = LittleFS.totalBytes() > 0;     // mounted by SetupSeqStore()
        if (!fsReady) return;
    }
    std::vector<String> lines;
    xSemaphoreTake(mtx, portMAX_DELAY);
    lines.swap(pendingFile);
    xSemaphoreGive(mtx);
    if (lines.empty()) return;

    File f = LittleFS.open(GENLOG_PATH, "a");
    if (!f) return;
    for (auto &l : lines) f.println(l);
    f.close();
    trimGeneralLog();
}

void StatusLog(const String &line) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    logLocked(line);
    xSemaphoreGive(mtx);
}

void StatusCountIrSent()    { xSemaphoreTake(mtx, portMAX_DELAY); irSent++;    xSemaphoreGive(mtx); }
void StatusCountIrError()   { xSemaphoreTake(mtx, portMAX_DELAY); irErrors++;  xSemaphoreGive(mtx); }
void StatusCountIrDropped() { xSemaphoreTake(mtx, portMAX_DELAY); irDropped++; xSemaphoreGive(mtx); }

uint32_t StatusIrSentCount() {
    xSemaphoreTake(mtx, portMAX_DELAY);
    uint32_t n = irSent;
    xSemaphoreGive(mtx);
    return n;
}

bool StatusRoomIrFrame(const uint8_t *mac, const IrLinkIrFrame &frame) {
    char name[IRLINK_NAME_LEN];
    strlcpy(name, frame.room, IRLINK_NAME_LEN);

    xSemaphoreTake(mtx, portMAX_DELAY);
    RoomInfo *r = roomForMac(mac, name);
    bool isNew = true;
    if (r) {
        touchRoom(r, name);
        if (r->haveSeq && r->lastSeq == frame.seq) {
            isNew = false;   // TX retried a frame we already have
        } else {
            r->lastSeq = frame.seq;
            r->haveSeq = true;
            r->frames++;
            char buf[64];
            if (frame.format == FMT_XMP)
                snprintf(buf, sizeof(buf), "XMP 0x%016llX", (unsigned long long)frame.value);
            else
                snprintf(buf, sizeof(buf), "RAW proto %u, %u durations",
                         frame.protocol, frame.rawCount);
            logLocked("'" + String(r->name) + "' #" + String(frame.seq) + "  " + buf);
        }
    }
    xSemaphoreGive(mtx);
    return isNew;
}

void StatusRoomHeartbeat(const uint8_t *mac, const IrLinkHeartbeat &hb) {
    char name[IRLINK_NAME_LEN];
    strlcpy(name, hb.room, IRLINK_NAME_LEN);

    xSemaphoreTake(mtx, portMAX_DELAY);
    RoomInfo *r = roomForMac(mac, name);
    if (r) {
        touchRoom(r, name);
        r->txRssi      = hb.wifiRssi;
        r->txUptimeSec = hb.uptimeSec;
        r->txSentOk    = hb.sentOk;
        r->txSentFail  = hb.sentFail;
    }
    xSemaphoreGive(mtx);
}

// -----------------------------------------------------------------------------
// StatusJson — also evaluates online → offline transitions
// -----------------------------------------------------------------------------
String StatusJson() {
    String j;
    j.reserve(3072);

    xSemaphoreTake(mtx, portMAX_DELAY);
    uint32_t now = millis();

    j += "{\"device\":\"" + jsonEscape(RxSysName()) + "\",\"fw\":\"" FW_VERSION "\"";
    j += ",\"uptime\":" + String(now / 1000);
    j += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    j += ",\"mac\":\"" + WiFi.macAddress() + "\"";
    j += ",\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
    j += ",\"rssi\":" + String(WiFi.RSSI());
    j += ",\"channel\":" + String(WiFi.channel());
    j += ",\"ap\":\"" + OmadaApName() + "\"";
    j += ",\"heap\":" + String(ESP.getFreeHeap());          // internal RAM free now
    j += ",\"heapMin\":" + String(ESP.getMinFreeHeap());    // lowest since boot
    j += ",\"heapBlk\":" + String(ESP.getMaxAllocHeap());   // largest free block
    j += ",\"router\":\"" + RouterCheckText() + "\"";
    j += ",\"routerOk\":" + String(RouterReachable() ? "true" : "false");
    j += ",\"irSent\":" + String(irSent);
    j += ",\"irErrors\":" + String(irErrors);
    j += ",\"irDropped\":" + String(irDropped);
    j += ",\"mov\":" + String((int)BoardMovGetState());
    j += ",\"movText\":\"" + String(BoardMovText()) + "\"";
    j += ",\"movMuted\":" + String(BoardMovMuted() ? "true" : "false");

    j += ",\"rooms\":[";
    bool first = true;
    for (auto &r : rooms) {
        if (!r.used) continue;
        uint32_t age = now - r.lastSeenMs;
        bool online = age < IRLINK_OFFLINE_MS;
        if (r.online && !online) {
            r.online = false;
            logLocked("'" + String(r.name) + "' OFFLINE (no heartbeat)");
        }
        if (!first) j += ",";
        first = false;
        j += "{\"name\":\"" + jsonEscape(r.name) + "\"";
        j += ",\"mac\":\"" + macToString(r.mac) + "\"";
        j += ",\"online\":" + String(online ? "true" : "false");
        j += ",\"ago\":" + String(age / 1000);
        j += ",\"frames\":" + String(r.frames);
        j += ",\"rssi\":" + String(r.txRssi);
        j += ",\"txUptime\":" + String(r.txUptimeSec);
        j += ",\"ok\":" + String(r.txSentOk);
        j += ",\"fail\":" + String(r.txSentFail) + "}";
    }
    j += "]";

    j += ",\"log\":[";
    for (uint8_t i = 0; i < logCount; i++) {
        // newest first
        uint8_t idx = (logHead + STATUS_LOG_LINES - 1 - i) % STATUS_LOG_LINES;
        if (i) j += ",";
        j += "\"" + jsonEscape(logLines[idx]) + "\"";
    }
    j += "]}";
    xSemaphoreGive(mtx);
    return j;
}
