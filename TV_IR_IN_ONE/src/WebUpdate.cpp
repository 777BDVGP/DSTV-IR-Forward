// =============================================================================
// WebUpdate.cpp — /update page: firmware upload from a browser (PC or phone)
// =============================================================================
// For updates without PlatformIO (e.g. a board in someone else's house): send
// them the .bin file (tools/export_bin.py puts it in TV_IR_IN_ONE/firmware_out);
// they open http://<board ip>/update, pick the file, enter the owner password
// and upload.
//
//   GET  /update            page (file picker, password, progress bar)
//   POST /update            multipart upload; password in header X-Update-Password
//   GET  /api/update/pw     {"set":true|false}
//   POST /api/update/pw     cur, new — set the upload password (cur only once set)
//
// Upload password = the board's owner password (OwnerPass.h): chosen on the
// setup page of a new board, or here if none is set yet. The same password is
// needed for WiFi changes and network (pio) uploads. Forgotten? Erase the
// flash and flash over USB (pio run -e usb -t erase, then upload).
//
// Safety:
//   * wrong / missing password → refused before anything is written
//   * the new firmware goes into the spare OTA partition; the board only
//     switches to it when the whole file arrived and checked out — an
//     interrupted upload or a wrong file leaves the running firmware in place
//   * the file must start like an ESP32-S2 app image and must contain
//     FW_MARKER, so a TV_IR_Sending / TV_IR_Receiving .bin is refused
// IR forwarding keeps running during the upload (other tasks).
// =============================================================================

#include <Arduino.h>
#include <Update.h>
#include "OwnerPass.h"
#include <ESPAsyncWebServer.h>
#include "Config.h"
#include "WebUtil.h"
#include "WebPage.h"
#include "Status.h"

// Present in every TV_IR_IN_ONE firmware (it is used below, so it is kept in
// the binary). '#' occurs only once, which keeps the streaming match simple.
static const char FW_MARKER[] = "#TV_IR_IN_ONE-FIRMWARE";
static const size_t kMarkerLen = sizeof(FW_MARKER) - 1;

static const uint8_t  kImageMagic   = 0xE9;   // esp_image_header_t.magic
static const uint16_t kChipIdEsp32S2 = 2;     // esp_image_header_t.chip_id (offset 12)

static AsyncWebServerRequest *updReq     = nullptr;   // upload in progress
static bool                   updOk      = false;
static String                 updError;
static size_t                 updBytes   = 0;
static size_t                 markPos    = 0;
static bool                   markFound  = false;
static uint32_t               updStartMs = 0;
static uint32_t               updLastMs  = 0;   // last data received (stale-upload check)

// Upload password = the board's owner password (OwnerPass.h)

static void ScanMarker(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len && !markFound; i++) {
        if (data[i] == (uint8_t)FW_MARKER[markPos]) {
            if (++markPos == kMarkerLen) markFound = true;
        } else {
            markPos = (data[i] == (uint8_t)FW_MARKER[0]) ? 1 : 0;
        }
    }
}

static void Fail(const String &why) {
    if (updError.isEmpty()) {
        updError = why;
        StatusLog("Firmware upload REFUSED: " + why);
    }
    if (Update.isRunning()) Update.abort();
}

static void OnUpload(AsyncWebServerRequest *req, const String &filename, size_t index,
                     uint8_t *data, size_t len, bool final) {
    if (index == 0) {
        if (updReq && updReq != req && Update.isRunning() && millis() - updLastMs < 30000) {
            // Another upload is still going — leave it alone
            return;
        }
        if (Update.isRunning()) Update.abort();   // left over from a dropped upload
        updReq = req; updOk = false; updError = ""; updBytes = 0;
        markPos = 0; markFound = false; updStartMs = updLastMs = millis();

        const AsyncWebHeader *pw = req->getHeader("X-Update-Password");
        if (!OwnerPassSet())                   { Fail("no upload password set yet - set one on this page first"); return; }
        if (!pw || !OwnerPassOk(pw->value()))  { Fail("wrong password"); return; }
        if (len < 16 || data[0] != kImageMagic ||
            (uint16_t)(data[12] | (data[13] << 8)) != kChipIdEsp32S2) {
            Fail("'" + filename + "' is not ESP32-S2 firmware (use firmware.bin)");
            return;
        }
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            Fail(String("cannot start: ") + Update.errorString());
            return;
        }
        StatusLog("Firmware upload started: " + filename);
    }
    if (req != updReq || updError.length()) return;

    updLastMs = millis();
    ScanMarker(data, len);
    if (Update.write(data, len) != len) { Fail(String("write failed: ") + Update.errorString()); return; }
    updBytes += len;

    if (final) {
        if (!markFound) { Fail("not a TV_IR_IN_ONE firmware (TX / RX file?)"); return; }
        if (!Update.end(true)) { Fail(String("check failed: ") + Update.errorString()); return; }
        updOk = true;
        StatusLog("Firmware upload OK, " + String(updBytes / 1024) + " KB in " +
                  String((millis() - updStartMs) / 1000) + " s - rebooting into the new firmware");
    }
}

static void OnRequestDone(AsyncWebServerRequest *req) {
    if (req != updReq) { req->send(409, "text/plain", "Another upload is busy - try again"); return; }
    updReq = nullptr;
    if (updOk) {
        req->send(200, "text/plain", "Update OK - the board reboots now (about 15 s)");
        RebootRequested = true;   // restart from loop(), after the reply is sent
        return;
    }
    if (updError.isEmpty()) Fail("upload incomplete");
    req->send(400, "text/plain", "Update refused: " + updError + ". The old firmware keeps running.");
}

static const char UPDATE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TV IR IN ONE - Firmware update</title>
<link rel="stylesheet" href="/style.css">
<style>progress{width:100%;height:18px;accent-color:#0284c7}</style>
</head><body>
<div id="hdr" data-page="about" data-msg="Firmware update"></div><script src="/common.js"></script>

<div class="card" id="pwset" style="display:none"><h3 id="pwhd">Choose an upload password</h3>
<div class="hint" id="pwhint">This board has no upload password yet. Choose one (6 or more characters) -
you need it for every firmware update. Keep it somewhere safe.</div>
<table class="kv">
<tr id="pwcur" style="display:none"><td>Current password</td><td><input type="password" id="pc" autocomplete="current-password"></td></tr>
<tr><td>New password</td><td><input type="password" id="pn" autocomplete="new-password"></td></tr>
<tr><td>Repeat new password</td><td><input type="password" id="pr" autocomplete="new-password"></td></tr>
</table>
<div class="row"><button class="ok" onclick="setPw()">SAVE PASSWORD</button>
<button id="pwcancel" style="display:none" onclick="showPw(false)">Cancel</button></div>
<div id="pwst" class="v"></div></div>

<div id="upd">
<table class="kv">
<tr class="hd"><td>Firmware update</td><td></td></tr>
<tr><td>Running now</td><td id="cur">-</td></tr>
<tr><td>Firmware file (.bin)</td><td><input type="file" id="file" accept=".bin"></td></tr>
<tr><td>Password</td><td><input type="password" id="pw" autocomplete="current-password"></td></tr>
</table>
<div class="row"><button class="ok" id="go" onclick="upload()">UPLOAD &amp; UPDATE</button></div>
<div class="card"><progress id="bar" max="100" value="0"></progress>
<div id="st" class="v" style="margin-top:6px">Choose the file you received, enter the password, press UPLOAD.</div></div>
<p class="hint">Do not switch the board off during the upload. If anything goes wrong the old firmware
simply keeps running - just try again. <a href="#" onclick="showPw(true);return false">Change upload password</a></p>
</div>

<script>
function $(id){return document.getElementById(id);}
let pwIsSet=true;
function pwState(){fetch('/api/update/pw',{cache:'no-store'}).then(r=>r.json()).then(s=>{pwIsSet=s.set;
  if(!s.set){$('pwset').style.display='';$('upd').style.display='none';}}).catch(()=>{});}
function showPw(on){$('pwset').style.display=on?'':'none';$('pwcur').style.display=on&&pwIsSet?'':'none';
  $('pwcancel').style.display=on&&pwIsSet?'':'none';$('pwhd').textContent=pwIsSet?'Change upload password':'Choose an upload password';
  $('pwhint').style.display=pwIsSet?'none':'';$('pwst').textContent='';}
function setPw(){const n=$('pn').value,r=$('pr').value;
  if(n.length<6){$('pwst').textContent='At least 6 characters';$('pwst').style.color='#ef4444';return;}
  if(n!==r){$('pwst').textContent='The two new passwords differ';$('pwst').style.color='#ef4444';return;}
  fetch('/api/update/pw',{method:'POST',body:new URLSearchParams({cur:$('pc').value,new:n})})
   .then(x=>x.text().then(t=>{$('pwst').textContent=t;$('pwst').style.color=x.ok?'#22c55e':'#ef4444';
     if(x.ok){pwIsSet=true;$('pc').value=$('pn').value=$('pr').value='';
       setTimeout(()=>{showPw(false);$('upd').style.display='';},1500);}}))
   .catch(()=>{$('pwst').textContent='Connection lost';});}
pwState();
function cur(){fetch('/api/about').then(r=>r.json()).then(a=>{$('cur').textContent='v'+a.fw+'  ('+a.build+')';}).catch(()=>{});}
cur();
function st(t,c){$('st').textContent=t;$('st').style.color=c||'';}
function upload(){
  const f=$('file').files[0];
  if(!f){st('Choose the firmware file first','#ef4444');return;}
  if(!$('pw').value){st('Enter the password','#ef4444');return;}
  const fd=new FormData();fd.append('firmware',f,f.name);
  const x=new XMLHttpRequest();
  x.open('POST','/update');x.setRequestHeader('X-Update-Password',$('pw').value);
  x.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(100*e.loaded/e.total);$('bar').value=p;st('Uploading '+p+' %','#f59e0b');}};
  x.onload=()=>{if(x.status==200){$('bar').value=100;st(x.responseText,'#22c55e');waitBack();}
    else{$('bar').value=0;st(x.responseText||('Error '+x.status),'#ef4444');$('go').disabled=false;}};
  x.onerror=()=>{st('Connection lost during upload - the old firmware keeps running. Try again.','#ef4444');$('go').disabled=false;};
  $('go').disabled=true;st('Uploading 0 %','#f59e0b');x.send(fd);
}
function waitBack(){let n=0;const t=setInterval(()=>{n++;
  fetch('/api/about',{cache:'no-store'}).then(r=>r.json()).then(a=>{if(n>3){clearInterval(t);
    st('Done - now running v'+a.fw+'  ('+a.build+')','#22c55e');$('cur').textContent='v'+a.fw+'  ('+a.build+')';$('go').disabled=false;}})
  .catch(()=>{st('Rebooting...','#f59e0b');});
  if(n>40){clearInterval(t);st('Board not back after 2 min - check it is powered and on WiFi','#ef4444');}},3000);}
</script></body></html>)HTML";

void AddUpdateRoutes(AsyncWebServer &server) {
    server.on("/api/update/pw", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", OwnerPassSet() ? "{\"set\":true}" : "{\"set\":false}");
    });
    server.on("/api/update/pw", HTTP_POST, [](AsyncWebServerRequest *req) {
        String cur = req->hasParam("cur", true) ? req->getParam("cur", true)->value() : String();
        String nw  = req->hasParam("new", true) ? req->getParam("new", true)->value() : String();
        if (OwnerPassSet() && !OwnerPassOk(cur)) {
            StatusLog("Upload password change REFUSED: wrong current password");
            req->send(403, "text/plain", "Current password is wrong");
            return;
        }
        if (nw.length() < OWNER_PW_MIN || nw.length() > OWNER_PW_MAX) {
            req->send(400, "text/plain", "The password must be 6 to 63 characters");
            return;
        }
        bool first = !OwnerPassSet();
        if (!OwnerPassStore(nw)) { req->send(500, "text/plain", "Could not save the password"); return; }
        StatusLog(first ? "Owner password set" : "Owner password changed");
        req->send(200, "text/plain", "Password saved");
    });
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", UPDATE_HTML);
    });
    server.on("/update", HTTP_POST, OnRequestDone, OnUpload);
}
