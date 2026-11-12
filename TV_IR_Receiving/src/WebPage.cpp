// =============================================================================
// WebPage.cpp — Status web page, shared CSS + header script
// =============================================================================

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "Config.h"
#include "WebPage.h"
#include "IrTransmit.h"
#include "Status.h"
#include "WebUtil.h"
#include "WifiSetup.h"

volatile bool RebootRequested = false;

static AsyncWebServer server(80);

const char WEB_STYLE_CSS[] PROGMEM = R"CSS(body{font-family:sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:16px;font-size:.9em}
h2{font-size:1.5em;color:#0284c7;margin:0 0 4px}
h4{font-size:.8em;font-weight:normal;color:#94a3b8;margin:0 0 4px}
h4.msg{color:#38bdf8;margin-bottom:10px}
.sub{color:#94a3b8;font-size:.8em;margin-bottom:14px}
.card{background:#1e293b;border-radius:8px;padding:12px;margin-bottom:12px}
.card h3{margin:0 0 8px;font-size:1em;color:#38bdf8}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px}
.k{color:#94a3b8;font-size:.75em}.v{font-weight:bold}
table{width:100%;border-collapse:collapse;font-size:.85em}
th,td{text-align:left;padding:4px 6px;border-bottom:1px solid #334155}
th{color:#94a3b8;font-weight:normal}
.on{color:#22c55e;font-weight:bold}.off{color:#ef4444;font-weight:bold}
.log{font-family:monospace;font-size:.8em;white-space:pre-wrap;max-height:320px;overflow-y:auto}
button{background:#0284c7;color:#fff;border:none;border-radius:4px;padding:8px 14px;
font-size:.9em;cursor:pointer;margin-right:6px}
button:hover{background:#0369a1}button.warn{background:#b45309}
.scroll{overflow-x:auto}
.nav{display:flex;gap:6px;margin-bottom:12px;flex-wrap:wrap}
.nav a{color:#e2e8f0;text-decoration:none;background:#1e293b;padding:6px 12px;border-radius:6px}
.nav a.act{background:#0284c7}
input,select{background:#0f172a;color:#e2e8f0;border:1px solid #334155;border-radius:4px;padding:5px 6px;font-size:.95em}
input[type=checkbox]{width:auto}
button.ok{background:#15803d}button.ok:hover{background:#166534}
button.del{background:#b91c1c}button.del:hover{background:#991b1b}
button.small{padding:4px 8px;font-size:.8em;margin:2px}
button:disabled{opacity:.4;cursor:default}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:6px 0}
.big{font-size:1.15em;font-weight:bold;color:#facc15}
.hint{color:#94a3b8;font-size:.85em}
table.kv{background:#1e293b;border-radius:8px;overflow:hidden;margin-bottom:12px;font-size:.95em}
table.kv td{padding:6px 10px}
table.kv tr:last-child td{border-bottom:none}
table.kv tr.hd td{color:#38bdf8;font-size:.85em;font-weight:bold;text-transform:uppercase;letter-spacing:.5px}
table.kv td:last-child{text-align:right;white-space:nowrap}
table.kv tbody tr:last-child td{border-bottom:none}
table.rooms td{white-space:nowrap}
.badge{display:inline-block;padding:4px 10px;border-radius:6px;color:#fff}
.badge.ok{background:green}.badge.bad{background:red}.badge.warn{background:orange;color:#000}.badge.off{background:#64748b}
input[type=checkbox]{accent-color:#0284c7;width:16px;height:16px;cursor:pointer}
@media(max-width:600px){body{padding:10px}}
)CSS";

// ── Shared page header (same layout as the TV IR TX pages) ──────────────────
// <div id="hdr" data-page="status" data-msg="Selection Option"></div>
const char WEB_COMMON_JS[] PROGMEM = R"JS((function(){
const P=[['/','STATUS','status'],['/sequences','IR SEQUENCES','sequences'],['/schedule','SCHEDULE','schedule'],
['/settings','SETTINGS','settings'],['/wifi','WIFI','wifi'],['/files','FILES','files'],['/about','ABOUT','about']];
const h=document.getElementById('hdr');if(!h)return;
const cur=h.dataset.page,msg=h.dataset.msg||'Selection Option';
h.innerHTML='<h2 id="H01">&nbsp;</h2><h4 id="H02">&nbsp;</h4><h4 id="H03" class="msg">'+msg+'</h4>'+
'<div class="nav">'+P.map(p=>'<a href="'+p[0]+'"'+(p[2]==cur?' class="act"':'')+'>'+p[1]+'</a>').join('')+'</div>';
function tick(){fetch('/api/time').then(r=>r.json()).then(t=>{
document.getElementById('H01').textContent=t.name;document.getElementById('H02').textContent=t.time;
const pg=P.find(p=>p[2]==cur);document.title=t.name+(pg?' - '+pg[1]:'');
}).catch(()=>{document.getElementById('H02').textContent='connection lost...';});}
tick();setInterval(tick,1000);
// Same as the TX: message in the header line (red), back to the page default after 2 s
let hdrTimer=null;
window.hdrMsg=function(text,color){const m=document.getElementById('H03');
m.textContent=text;m.style.color=color||'red';clearTimeout(hdrTimer);
hdrTimer=setTimeout(()=>{m.textContent=msg;m.style.color='';},2000);};
})();)JS";

static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TV IR RX</title>
<link rel="stylesheet" href="/style.css">
</head><body>
<div id="hdr" data-page="status"></div><script src="/common.js"></script>

<table class="kv">
<tr class="hd"><td>This board</td><td>Instant values</td></tr>
<tr><td>Uptime</td><td id="upt">-</td></tr>
<tr><td>WiFi channel / RSSI</td><td id="wifi">-</td></tr>
<tr><td>Omada AP</td><td id="ap">-</td></tr>
<tr><td>Router reachable</td><td><span id="router">-</span></td></tr>
<tr><td>This RX MAC</td><td id="mac">-</td></tr>
<tr><td>IR codes sent out on IR</td><td id="irs">-</td></tr>
<tr><td>IR errors</td><td id="ire">-</td></tr>
<tr><td>Dropped (queue full)</td><td id="ird">-</td></tr>
<tr><td>Free heap</td><td id="heap">-</td></tr>
<tr><td>Lowest free heap since boot</td><td id="heapMin">-</td></tr>
<tr><td>Largest free block</td><td id="heapBlk">-</td></tr>
<tr><td>Surge protection (MOV)</td><td><span id="mov" class="badge warn">-</span></td></tr>
</table>

<div class="scroll"><table class="kv rooms">
<thead><tr class="hd"><td>TX rooms</td><td>Status</td><td>Last seen</td><td>Frames</td>
<td>TX WiFi</td><td>Delivered</td><td>MAC</td></tr></thead>
<tbody id="rooms"><tr><td colspan="7">No TX node seen yet</td></tr></tbody>
</table></div>

<div class="card"><h3>Actions</h3>
<button onclick="post('/api/resend')">Resend last code</button>
<button class="warn" onclick="if(confirm('Reboot receiver?'))post('/api/reboot')">Reboot</button>
<span id="msg" class="k"></span></div>

<div class="card"><h3>Event log (newest first)</h3><div class="log" id="log"></div></div>

<script>
function fmtUp(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);
return (d?d+'d ':'')+h+'h '+m+'m';}
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}
function set(id,v){document.getElementById(id).textContent=v;}
function post(u){fetch(u,{method:'POST'}).then(r=>r.text()).then(t=>{
document.getElementById('msg').textContent=t;setTimeout(()=>document.getElementById('msg').textContent='',3000);});}
function load(){fetch('/api/status').then(r=>r.json()).then(s=>{
set('upt',fmtUp(s.uptime));set('wifi',s.wifi?('ch '+s.channel+' / '+s.rssi+' dBm'):'down');set('ap',s.ap||'-');
set('router',s.router);document.getElementById('router').className=s.routerOk?'':'badge bad';
set('mac',s.mac);set('irs',s.irSent);set('ire',s.irErrors);set('ird',s.irDropped);
set('heap',Math.round(s.heap/1024)+' KB');
set('heapMin',Math.round(s.heapMin/1024)+' KB');set('heapBlk',Math.round(s.heapBlk/1024)+' KB');
let rows='';
s.rooms.forEach(r=>{const tot=r.ok+r.fail;const pct=tot?(100*r.ok/tot).toFixed(1)+'%':'-';
rows+='<tr><td>'+esc(r.name)+'</td><td><span class="badge '+(r.online?'ok">Online':'bad">Offline')+'</span></td><td>'+
r.ago+' s ago</td><td>'+r.frames+'</td><td>'+(r.rssi?r.rssi+' dBm':'-')+'</td><td>'+pct+
'</td><td>'+r.mac+'</td></tr>';});
if(rows)document.getElementById('rooms').innerHTML=rows;
document.getElementById('log').textContent=s.log.join('\n');
const mv=document.getElementById('mov');mv.textContent=s.movText;
mv.className='badge '+(s.mov==1?'ok':s.mov==2?'bad':s.mov==3?'off':'warn');
}).catch(()=>{});}
load();setInterval(load,2000);
</script></body></html>)HTML";

void SetupWebPage() {
    // Never let the browser show an old copy of a page after a firmware update
    DefaultHeaders::Instance().addHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", INDEX_HTML);
    });

    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/css", WEB_STYLE_CSS);
    });
    server.on("/common.js", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/javascript", WEB_COMMON_JS);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", StatusJson());
    });

    server.on("/api/resend", HTTP_POST, [](AsyncWebServerRequest *req) {
        bool ok = ResendLastIrFrame();
        if (ok) StatusLog("Manual resend of last code");
        req->send(200, "text/plain", ok ? "Last code resent" : "No code received yet");
    });

    server.on("/api/movmute", HTTP_POST, [](AsyncWebServerRequest *req) {
        bool on = req->hasArg("on") && req->arg("on") == "1";
        if (on && BoardMovGetState() != MOV_BLOWN) {
            req->send(200, "text/plain", "MOV is not blown - nothing to mute");
            return;
        }
        BoardMovSetMuted(on);
        StatusLog(on ? "MOV buzzer muted from web page" : "MOV buzzer un-muted from web page");
        req->send(200, "text/plain", on ? "Buzzer muted until MOV replaced" : "Buzzer un-muted");
    });

    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *req) {
        req->send(200, "text/plain", "Rebooting…");
        RebootRequested = true;   // restart from loop(), after the reply is sent
    });

    AddSequenceRoutes(server);
    AddScheduleRoutes(server);
    AddSettingsRoutes(server);
    AddFileRoutes(server);
    AddAboutRoutes(server);
    WifiAddRoutes(server);      // /wifi settings (WifiSetup.cpp)

    server.onNotFound([](AsyncWebServerRequest *req) {
        req->send(404, "text/plain", "Not found");
    });

    server.begin();
}
