// =============================================================================
// WebSettings.cpp — /settings page: RX System Name (shown in every page header)
// =============================================================================
//   GET  /api/settings            {"sysName":"...","movMute":false,"mov":2}
//   POST /api/settings  sysName   save (1-18 chars) to /AllParams.json (LittleFS,
//                                 same file name as the TX; visible on /files)
// WiFi / OTA host name comes from /WifiSettings.json (hostnames cannot contain spaces).
// =============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "Config.h"
#include "IrLink.h"
#include "WebUtil.h"
#include "Status.h"
#include "Board.h"

#define SYSNAME_MAX  18   // the ESP-NOW beacon carries only the first IRLINK_NAME_LEN-1 (15)

static String            sysName = DEVICE_NAME;
static SemaphoreHandle_t nameMtx = NULL;

String RxSysName() {
    xSemaphoreTake(nameMtx, portMAX_DELAY);
    String s = sysName;
    xSemaphoreGive(nameMtx);
    return s;
}

static const char *PARAMS_PATH = "/AllParams.json";

static bool WriteParams(const String &name) {
    StaticJsonDocument<256> doc;
    doc["SysName"]       = name;
    doc["MovBuzzerMute"] = BoardMovMuted();   // copy of the Board.cpp NVS flag
    File f = LittleFS.open(PARAMS_PATH, "w");
    if (!f) return false;
    bool ok = serializeJsonPretty(doc, f) > 0;
    f.close();
    return ok;
}

static void SetSysName(const String &name) {
    xSemaphoreTake(nameMtx, portMAX_DELAY);
    sysName = name;
    xSemaphoreGive(nameMtx);
    WriteParams(name);
}

// Rewrite /AllParams.json with the current values (e.g. after the MOV mute changed)
void RxSaveParams() {
    WriteParams(RxSysName());
}

// Call after SetupSeqStore() (LittleFS mounted)
void SetupRxSettings() {
    nameMtx = xSemaphoreCreateMutex();
    File f = LittleFS.open(PARAMS_PATH, "r");
    if (f) {
        StaticJsonDocument<256> doc;
        if (!deserializeJson(doc, f)) {
            String n = doc["SysName"] | "";
            if (n.length()) sysName = n;
            // File wins over the NVS copy (e.g. after an AllParams.json upload)
            if (doc.containsKey("MovBuzzerMute")) BoardMovSetMuted(doc["MovBuzzerMute"].as<bool>());
            else                                  { f.close(); WriteParams(sysName); return; }
        }
        f.close();
        return;
    }
    // First boot with this firmware: take over the name firmware 1.3.0 kept in NVS
    Preferences prefs;
    if (prefs.begin("rx", true)) {
        String n = prefs.getString("sysName", DEVICE_NAME);
        if (n.length()) sysName = n;
        prefs.end();
    }
    WriteParams(sysName);
}

static const char SETTINGS_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TV IR RX - Settings</title>
<link rel="stylesheet" href="/style.css">
</head><body>
<div id="hdr" data-page="settings"></div><script src="/common.js"></script>

<table class="kv">
<tr class="hd"><td>Parameter Description</td><td>Value</td></tr>
<tr><td>System Name (max 18 chars)</td><td><input id="sysName" maxlength="18"></td></tr>
<tr><td>Mute buzzer - MOV blown (until replaced)</td><td><input type="checkbox" id="movMute"></td></tr>
</table>
<div class="row"><button onclick="save()">UPDATE</button></div>

<script>
function $(id){return document.getElementById(id);}
fetch('/api/settings').then(r=>r.json()).then(s=>{$('sysName').value=s.sysName;$('movMute').checked=s.movMute;});
function save(){
  fetch('/api/settings',{method:'POST',body:new URLSearchParams({sysName:$('sysName').value,movMute:$('movMute').checked?1:0})})
   .then(r=>r.text().then(t=>{hdrMsg(t,'red');
     fetch('/api/settings').then(r=>r.json()).then(s=>{$('movMute').checked=s.movMute;});}))
   .catch(()=>hdrMsg('Connection lost','red'));
}
</script></body></html>)HTML";

void AddSettingsRoutes(AsyncWebServer &server) {
    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", SETTINGS_HTML);
    });

    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", "{\"sysName\":\"" + JsonEscape(RxSysName()) + "\",\"movMute\":" +
                  (BoardMovMuted() ? "true" : "false") + ",\"mov\":" + String((int)BoardMovGetState()) + "}");
    });

    server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *req) {
        String n = ArgStr(req, "sysName");
        n.trim();
        if (n.isEmpty() || n.length() > SYSNAME_MAX) {
            req->send(400, "text/plain", "System Name must be 1-18 characters");
            return;
        }
        if (n != RxSysName()) {
            SetSysName(n);
            StatusLog("System Name set to '" + n + "'");
        }
        // Same as the TX: Board.cpp clears the mute again while the MOV reads OK
        bool mute = ArgStr(req, "movMute") == "1";
        if (mute != BoardMovMuted()) {
            BoardMovSetMuted(mute);
            RxSaveParams();
            StatusLog(mute ? "MOV buzzer muted (Settings)" : "MOV buzzer un-muted (Settings)");
        }
        String msg = "Parameters Updated!";
        if (mute && BoardMovGetState() == MOV_OK) msg += " (MOV is OK - mute not needed, cleared)";
        req->send(200, "text/plain", msg);
    });
}
