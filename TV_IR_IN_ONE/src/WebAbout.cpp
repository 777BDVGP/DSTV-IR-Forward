// =============================================================================
// WebAbout.cpp — /about page: OTA info, reboot, project / build information
// =============================================================================
//   GET  /api/about   {fw,build,ip,mac,ota}
//   GET  /api/ota     {pct,err}  OTA progress (-1 = idle)
//   (reboot uses POST /api/reboot from WebPage.cpp)
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "Config.h"
#include "WebUtil.h"
#include "WebPage.h"

volatile int OtaPercent = -1;
volatile int OtaError   = -1;

static const char ABOUT_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TV IR IN ONE - About</title>
<link rel="stylesheet" href="/style.css">
</head><body>
<div id="hdr" data-page="about"></div><script src="/common.js"></script>

<!-- Same table as the TX About page (PageBuf WT_* table) -->
<table class="kv">
<tr><th style="color:#94a3b8;font-weight:normal;padding:6px 10px">PARAMETER</th>
<th style="color:#94a3b8;font-weight:normal;padding:6px 10px">VALUE</th></tr>
<tr><td>OTA (Over-The-Air) Updates</td><td><input id="otast" type="text" readonly value="Ready to Update"
 style="color:#22c55e;width:140px"></td></tr>
<tr><td>OTA Terminal Command</td><td id="ota" style="font-size:11px">…</td></tr>
<tr><td>Firmware upload from this browser</td><td><a href="/update" style="color:#38bdf8">/update &rarr;</a></td></tr>
</table>

<div class="card"><button class="del" onclick="reboot()">REBOOT</button> <span id="msg" class="k"></span></div>

<div class="card" style="text-align:center;line-height:1.7">
<b>Project:</b> TV IR IN ONE - DSTV IR repeater (IR in &rarr; IR LED next door)<br>
<b>Firmware:</b> <span id="fw">…</span><br>
<b>Build Date:</b> <span id="build">…</span><br>
<b>PCB:</b> IR_FORWARD_V1 (WEMOS LOLIN S2 Mini)<br>
<b>IP:</b> <span id="ip">…</span> &nbsp; <b>MAC:</b> <span id="mac">…</span></div>

<script>
function $(id){return document.getElementById(id);}
fetch('/api/about').then(r=>r.json()).then(a=>{
  $('fw').textContent=a.fw;$('build').textContent=a.build;$('ip').textContent=a.ip;$('mac').textContent=a.mac;$('ota').textContent=a.ota;});
// OTA status: poll every 1 s; while uploading show the %, then wait for reboot
var otaBusy=false;
function otaShow(t,c){var e=$('otast');e.value=t;e.style.color=c;}
function otaPoll(){fetch('/api/ota',{cache:'no-store'}).then(r=>r.json()).then(o=>{
  if(o.pct>=100){otaShow('Busy Rebooting','#ef4444');otaBusy=true;}
  else if(o.pct>=0){otaShow('Uploading '+o.pct+' %','#f59e0b');otaBusy=true;}
  else if(o.err>=0){otaShow('OTA error '+o.err,'#ef4444');otaBusy=false;}
  else if(otaBusy){otaBusy=false;location.reload();}
  else otaShow('Ready to Update','#22c55e');
}).catch(()=>{if(otaBusy)otaShow('Busy Rebooting','#ef4444');})
  .finally(()=>setTimeout(otaPoll,1000));}
otaPoll();
function reboot(){if(!confirm('Reboot the board?'))return;
  fetch('/api/reboot',{method:'POST'}).then(r=>r.text()).then(t=>{$('msg').textContent=t+' - page reloads in 15 s';
  setTimeout(()=>location.reload(),15000);});}
</script></body></html>)HTML";

// "Sep 30 2026" + "17:39:23" → "30 Sep 2026 17:39:23" (same as the TX About page)
static String BuildDate() {
    const char *d = __DATE__;
    char buf[32];
    snprintf(buf, sizeof(buf), "%c%c %c%c%c %c%c%c%c %s",
             d[4] == ' ' ? '0' : d[4], d[5], d[0], d[1], d[2], d[7], d[8], d[9], d[10], __TIME__);
    return String(buf);
}

void AddAboutRoutes(AsyncWebServer &server) {
    server.on("/about", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", ABOUT_HTML);
    });

    // Polled by /about every second for the OTA status field
    server.on("/api/ota", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json",
                  "{\"pct\":" + String(OtaPercent) + ",\"err\":" + String(OtaError) + "}");
    });

    server.on("/api/about", HTTP_GET, [](AsyncWebServerRequest *req) {
        String j = "{\"fw\":\"" FW_VERSION "\",\"build\":\"" + BuildDate() + "\"";
        j += ",\"ip\":\"" + WiFi.localIP().toString() + "\",\"mac\":\"" + WiFi.macAddress() + "\"";
        j += ",\"ota\":\"pio run -t upload --upload-port " + WiFi.localIP().toString() + "\"}";
        req->send(200, "application/json", j);
    });
}
