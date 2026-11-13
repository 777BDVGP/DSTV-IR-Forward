// =============================================================================
// WebSettings.cpp — /settings page: System Name, DSTV forwarding, MOV mute
// =============================================================================
//   GET  /api/settings   {"sysName":"...","fwd":true,"remote":"XMP","learn":false,
//                         "echo":false,"movMute":false,"movFit":true,"mov":2}
//   POST /api/settings   sysName (1-18 chars), fwd, remote (text), learn, echo,
//                        movMute, movFit (1/0)
//   remote  which remote is forwarded: "XMP" = DSTV (default), "NEC 0x20DF" ...
//           (RemoteFilter.h). learn: the next button press sets it, then off.
// Saved to /AllParams.json (LittleFS, same file name as TX / RX; visible on /files).
//   fwd   DSTV forwarding on/off — as the TV_IR_Sending Disable / Enable System
//   echo  Echo guard: ignore DSTV frames received while / just after the own IR
//         LED sends. OFF by default: the IR LED is next door, so the TSOP
//         cannot see it, and the guard would swallow held-button repeats.
//         Only switch it on if the IR LED is ever mounted where the TSOP sees it.
// WiFi / OTA host name comes from /WifiSettings.json (hostnames cannot contain spaces).
// =============================================================================

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "Config.h"
#include "WebUtil.h"
#include "Status.h"
#include "Board.h"
#include "IrReceive.h"   // IrSetForwardRemote()

#define SYSNAME_MAX  18

static String            sysName = DEVICE_NAME;
static SemaphoreHandle_t nameMtx = NULL;
static volatile bool     fwdEnabled = true;
static volatile bool     echoGuard  = false;
static volatile bool     learnRemote = false;   // not saved: a restart ends learning

String SysName() {
    xSemaphoreTake(nameMtx, portMAX_DELAY);
    String s = sysName;
    xSemaphoreGive(nameMtx);
    return s;
}

bool FwdEnabled()   { return fwdEnabled; }
bool FwdEchoGuard() { return echoGuard; }
bool FwdLearning()  { return learnRemote; }

// Called from the IR receiver task when "Learn remote" caught a button press
void FwdLearned(const String &remote) {
    learnRemote = false;
    StatusLog("Learn remote: now forwarding " + IrForwardRemoteLabel());
    SaveParams();
}

static const char *PARAMS_PATH = "/AllParams.json";

static bool WriteParams() {
    StaticJsonDocument<256> doc;
    doc["SysName"]        = SysName();
    doc["DstvForwarding"] = (bool)fwdEnabled;
    doc["EchoGuard"]      = (bool)echoGuard;
    doc["ForwardRemote"]  = IrForwardRemoteText();
    doc["MovBuzzerMute"]  = BoardMovMuted();   // copy of the Board.cpp NVS flag
    File f = LittleFS.open(PARAMS_PATH, "w");
    if (!f) return false;
    bool ok = serializeJsonPretty(doc, f) > 0;
    f.close();
    return ok;
}

// Rewrite /AllParams.json with the current values (e.g. after the MOV mute changed)
void SaveParams() {
    WriteParams();
}

// Call after SetupSeqStore() (LittleFS mounted)
void SetupSettings() {
    nameMtx = xSemaphoreCreateMutex();
    File f = LittleFS.open(PARAMS_PATH, "r");
    if (f) {
        StaticJsonDocument<256> doc;
        bool ok = !deserializeJson(doc, f);
        f.close();
        if (ok) {
            String n = doc["SysName"] | "";
            if (n.length()) sysName = n;
            fwdEnabled = doc["DstvForwarding"] | true;
            echoGuard  = doc["EchoGuard"]      | false;
            if (!IrSetForwardRemote(doc["ForwardRemote"] | "XMP")) IrSetForwardRemote("XMP");
            // File wins over the NVS copy (e.g. after an AllParams.json upload)
            if (doc.containsKey("MovBuzzerMute")) BoardMovSetMuted(doc["MovBuzzerMute"].as<bool>());
            if (doc.containsKey("MovBuzzerMute") && doc.containsKey("DstvForwarding") &&
                doc.containsKey("EchoGuard") && doc.containsKey("ForwardRemote")) return;
        }
    }
    WriteParams();   // first boot, or a key missing → write the full file
}

static const char SETTINGS_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TV IR IN ONE - Settings</title>
<link rel="stylesheet" href="/style.css">
</head><body>
<div id="hdr" data-page="settings"></div><script src="/common.js"></script>

<table class="kv">
<tr class="hd"><td>Parameter Description</td><td>Value</td></tr>
<tr><td>System Name (max 18 chars)</td><td><input id="sysName" maxlength="18"></td></tr>
<tr><td>Forward the remote to the IR LED</td><td><input type="checkbox" id="fwd"></td></tr>
<tr><td>Forward remote (XMP = DSTV)</td><td><input id="remote" maxlength="24" placeholder="XMP"></td></tr>
<tr><td>Learn remote: tick, UPDATE, then press a button on the TV box remote</td><td><input type="checkbox" id="learn"></td></tr>
<tr><td>Echo guard (only if the TSOP can see the IR LED)</td><td><input type="checkbox" id="echo"></td></tr>
<tr><td>Surge protection parts fitted (TMOV, 4N35)</td><td><input type="checkbox" id="movFit"></td></tr>
<tr><td>Mute buzzer - MOV blown (until replaced)</td><td><input type="checkbox" id="movMute"></td></tr>
</table>
<div class="row"><button onclick="save()">UPDATE</button></div>
<p class="hint">Echo guard ignores DSTV codes received while the IR LED is sending. Leave it OFF while
the IR LED is in the room next door: it would also drop the repeats of a held button.</p>

<script>
function $(id){return document.getElementById(id);}
let learnPoll=null;
function load(){fetch('/api/settings').then(r=>r.json()).then(s=>{$('sysName').value=s.sysName;
$('fwd').checked=s.fwd;$('echo').checked=s.echo;$('movMute').checked=s.movMute;$('movFit').checked=s.movFit;
$('learn').checked=s.learn;if(document.activeElement!==$('remote'))$('remote').value=s.remote;
clearTimeout(learnPoll);if(s.learn)learnPoll=setTimeout(load,1000);});}
load();
function save(){
  fetch('/api/settings',{method:'POST',body:new URLSearchParams({sysName:$('sysName').value,
    fwd:$('fwd').checked?1:0,remote:$('remote').value,learn:$('learn').checked?1:0,
    echo:$('echo').checked?1:0,movMute:$('movMute').checked?1:0,movFit:$('movFit').checked?1:0})})
   .then(r=>r.text().then(t=>{hdrMsg(t,'red');load();}))
   .catch(()=>hdrMsg('Connection lost','red'));
}
</script></body></html>)HTML";

void AddSettingsRoutes(AsyncWebServer &server) {
    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", SETTINGS_HTML);
    });

    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", "{\"sysName\":\"" + JsonEscape(SysName()) +
                  "\",\"fwd\":" + (fwdEnabled ? "true" : "false") +
                  ",\"remote\":\"" + JsonEscape(IrForwardRemoteText()) + "\"" +
                  ",\"learn\":" + (learnRemote ? "true" : "false") +
                  ",\"echo\":" + (echoGuard ? "true" : "false") +
                  ",\"movMute\":" + (BoardMovMuted() ? "true" : "false") +
                  ",\"movFit\":" + (BoardMovFitted() ? "true" : "false") +
                  ",\"mov\":" + String((int)BoardMovGetState()) + "}");
    });

    server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *req) {
        String n = ArgStr(req, "sysName");
        n.trim();
        if (n.isEmpty() || n.length() > SYSNAME_MAX) {
            req->send(400, "text/plain", "System Name must be 1-18 characters");
            return;
        }
        bool changed = false;
        if (n != SysName()) {
            xSemaphoreTake(nameMtx, portMAX_DELAY);
            sysName = n;
            xSemaphoreGive(nameMtx);
            StatusLog("System Name set to '" + n + "'");
            changed = true;
        }
        bool fwd = ArgStr(req, "fwd") == "1";
        if (fwd != fwdEnabled) {
            fwdEnabled = fwd;
            StatusLog(fwd ? "DSTV forwarding ENABLED (Settings)" : "DSTV forwarding DISABLED (Settings)");
            changed = true;
        }
        String note;
        if (req->hasArg("remote")) {
            String r = ArgStr(req, "remote");
            String before = IrForwardRemoteText();
            if (!IrSetForwardRemote(r)) {
                note = " (Forward remote '" + r + "' not understood - kept " + before + ")";
            } else if (IrForwardRemoteText() != before) {
                StatusLog("Forward remote set to " + IrForwardRemoteLabel() + " (Settings)");
                changed = true;
            }
        }
        bool learn = ArgStr(req, "learn") == "1";
        if (learn != learnRemote) {
            learnRemote = learn;
            StatusLog(learn ? "Learn remote: waiting for a button press" : "Learn remote cancelled");
        }
        bool echo = ArgStr(req, "echo") == "1";
        if (echo != echoGuard) {
            echoGuard = echo;
            StatusLog(echo ? "Echo guard ON (Settings)" : "Echo guard OFF (Settings)");
            changed = true;
        }
        // Same as the TX: Board.cpp clears the mute again while the MOV reads OK
        bool mute = ArgStr(req, "movMute") == "1";
        if (mute != BoardMovMuted()) {
            BoardMovSetMuted(mute);
            StatusLog(mute ? "MOV buzzer muted (Settings)" : "MOV buzzer un-muted (Settings)");
            changed = true;
        }
        if (changed) SaveParams();
        bool fit = ArgStr(req, "movFit") == "1";
        if (req->hasArg("movFit") && fit != BoardMovFitted()) {
            BoardMovSetFitted(fit);
            StatusLog(fit ? "Surge protection set to FITTED - checking again (Settings)"
                          : "Surge protection set to NOT FITTED - no check, no buzzer (Settings)");
        }
        String msg = "Parameters Updated!" + note;
        if (learnRemote) msg += " Now press a button on the TV box remote.";
        if (mute && BoardMovGetState() == MOV_OK) msg += " (MOV is OK - mute not needed, cleared)";
        req->send(200, "text/plain", msg);
    });
}
