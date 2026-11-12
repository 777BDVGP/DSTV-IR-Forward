// =============================================================================
// Status.cpp — Event log and board status shown on the web page
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"
#include "Status.h"
#include <LittleFS.h>
#include <vector>
#include "Clock.h"          // ClockText() — same date/time text as the TX log
#include "IrReceive.h"
#include "RouterCheck.h"

String SysName();     // WebSettings.cpp

static SemaphoreHandle_t mtx = NULL;
static String   logLines[STATUS_LOG_LINES];
static uint8_t  logHead  = 0;    // next slot to write
static uint8_t  logCount = 0;

// Lines waiting to be appended to /GeneralLog.txt by StatusFlushLog() (loop()).
// Logging also happens in the IR tasks and before LittleFS is mounted, so
// nothing writes flash directly from logLocked().
#define GENLOG_PATH        "/GeneralLog.txt"
#define GENLOG_KEEP_LINES  200        // like the TX: keep the last 200 lines
#define GENLOG_TRIM_BYTES  24000      // trim once the file grows past this
#define GENLOG_PENDING_MAX 60
static std::vector<String> pendingFile;
static bool fsReady = false;

// -----------------------------------------------------------------------------
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
    pendingFile.push_back(ClockText() + " - " + line);
    if (pendingFile.size() > GENLOG_PENDING_MAX) pendingFile.erase(pendingFile.begin());
    logLines[logHead] = entry;
    logHead = (logHead + 1) % STATUS_LOG_LINES;
    if (logCount < STATUS_LOG_LINES) logCount++;
}

// -----------------------------------------------------------------------------
void SetupStatus() {
    mtx = xSemaphoreCreateMutex();
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

// -----------------------------------------------------------------------------
// StatusJson
// -----------------------------------------------------------------------------
String StatusJson() {
    String j;
    j.reserve(2560);

    // Read before taking mtx: IrForwardStateText() takes the record lock, and
    // record code logs (mtx) while holding it — opposite order would deadlock.
    String fwd = IrForwardStateText(), lastDstv = IrLastDstvText(), name = SysName();

    xSemaphoreTake(mtx, portMAX_DELAY);
    uint32_t now = millis();
    j += "{\"device\":\"" + jsonEscape(name) + "\",\"fw\":\"" FW_VERSION "\"";
    j += ",\"uptime\":" + String(now / 1000);
    j += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    j += ",\"mac\":\"" + WiFi.macAddress() + "\"";
    j += ",\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
    j += ",\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\"";
    j += ",\"rssi\":" + String(WiFi.RSSI());
    j += ",\"channel\":" + String(WiFi.channel());
    j += ",\"heap\":" + String(ESP.getFreeHeap());          // internal RAM free now
    j += ",\"heapMin\":" + String(ESP.getMinFreeHeap());    // lowest since boot
    j += ",\"heapBlk\":" + String(ESP.getMaxAllocHeap());   // largest free block
    j += ",\"fwd\":\"" + fwd + "\"";
    j += ",\"lastDstv\":\"" + lastDstv + "\"";
    j += ",\"mov\":" + String((int)BoardMovGetState());
    j += ",\"movText\":\"" + String(BoardMovText()) + "\"";
    j += ",\"movMuted\":" + String(BoardMovMuted() ? "true" : "false");

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
