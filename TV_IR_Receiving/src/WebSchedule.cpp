// =============================================================================
// WebSchedule.cpp — /schedule page: run sequences at set times on set weekdays
// =============================================================================
//   GET  /api/schedule        entries + sequence list for the dropdowns
//   POST /api/schedule        JSON {entries:[{name,en,time,days,seqs,pauses,times,every,on}]}
//   POST /api/schedule/run    idx — run a saved entry now
//   GET  /api/time            board time + NTP sync state
// =============================================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "Config.h"
#include "WebUtil.h"
#include "SeqStore.h"
#include "Scheduler.h"

static const char SCHED_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TV IR RX - Schedule</title>
<link rel="stylesheet" href="/style.css">
<style>.entry{background:#0f172a;border-radius:6px;padding:10px;margin-bottom:10px}
.days label{margin-right:8px;white-space:nowrap}</style>
</head><body>
<div id="hdr" data-page="schedule" data-msg="Weekly schedule"></div><script src="/common.js"></script>

<div class="card"><h3>Schedule</h3>
<div class="row"><span class="k">Board time:</span><span id="time" class="v">…</span></div>
<div class="hint">Each entry runs its sequences in order at the set time on the ticked days.
Use &#8593; &#8595; to change the order; the pause after a sequence gives that device time to start
before the next one (the pause moves with its sequence).
&#215; runs a sequence several times in a row, e.g. Volume down &#215;30 every 400 ms;
then pause is waited once, after all those runs. Untick a line to skip it (and its pause)
without removing it. Runs missed while the board was off are not caught up.</div></div>

<div class="card"><div id="entries">loading…</div>
<div class="row"><button onclick="addEntry()">+ Add entry</button>
<button class="ok" onclick="save()">Save schedule</button></div>
<div id="msg" class="off"></div></div>

<script>
const DAYS=[['Mon',1],['Tue',2],['Wed',3],['Thu',4],['Fri',5],['Sat',6],['Sun',0]];
let sch=[],seqs=[],dirty=false;
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function $(id){return document.getElementById(id);}
function msg(t,ok){const e=$('msg');e.className=ok?'on':'off';e.textContent=t;if(t)setTimeout(()=>{if(e.textContent==t)e.textContent='';},6000);}
function seqName(id){const q=seqs.find(x=>x.id==id);return q?q.name:'(deleted #'+id+')';}

function load(){fetch('/api/schedule').then(r=>r.json()).then(s=>{
  seqs=s.seqs;sch=s.entries;dirty=false;render();
}).catch(()=>{$('entries').textContent='connection lost…';});}

function sync(){sch.forEach((e,i)=>{
  e.en=$('en'+i).checked;e.name=$('nm'+i).value;e.time=$('tm'+i).value;
  e.days=0;DAYS.forEach(d=>{if($('d'+i+'_'+d[1]).checked)e.days|=1<<d[1];});
  e.seqs=e.seqs.map((_,k)=>+$('s'+i+'_'+k).value);
  e.pauses=e.pauses.map((p,k)=>$('p'+i+'_'+k)?Math.max(100,Math.round(+$('p'+i+'_'+k).value)||100):p);
  e.times=e.seqs.map((_,k)=>Math.max(1,Math.min(99,Math.round(+$('t'+i+'_'+k).value)||1)));
  e.every=e.seqs.map((_,k)=>Math.max(100,Math.min(60000,Math.round(+$('e'+i+'_'+k).value)||500)));
  e.on=e.seqs.map((_,k)=>$('o'+i+'_'+k).checked?1:0);
});}

function seqSelect(i,k,val){
  let h='<select id="s'+i+'_'+k+'" onchange="dirty=true">';
  if(!seqs.find(x=>x.id==val))h+='<option value="'+val+'">'+esc(seqName(val))+'</option>';
  seqs.forEach(q=>{h+='<option value="'+q.id+'"'+(q.id==val?' selected':'')+'>'+esc(q.name)+'</option>';});
  return h+'</select>';
}

function render(){
  if(!sch.length){$('entries').innerHTML='<div class="hint">No entries yet - click "+ Add entry".</div>';return;}
  $('entries').innerHTML=sch.map((e,i)=>{
    let h='<div class="entry"><div class="row">'+
      '<label><input type="checkbox" id="en'+i+'"'+(e.en?' checked':'')+' onchange="dirty=true"> Enabled</label>'+
      'Name <input id="nm'+i+'" maxlength="32" value="'+esc(e.name)+'" oninput="dirty=true">'+
      'Time <input id="tm'+i+'" type="time" value="'+esc(e.time)+'" onchange="dirty=true">'+
      '<button class="small ok" onclick="runNow('+i+')">Run now</button>'+
      '<button class="small del" onclick="delEntry('+i+')">Delete</button></div>'+
      '<div class="row days">Days '+DAYS.map(d=>'<label><input type="checkbox" id="d'+i+'_'+d[1]+'"'+
        ((e.days>>d[1])&1?' checked':'')+' onchange="dirty=true"> '+d[0]+'</label>').join('')+
      '<button class="small" onclick="setDays('+i+',62)">Mon-Fri</button>'+
      '<button class="small" onclick="setDays('+i+',127)">Every day</button></div>'+
      '<div class="hint">Sequences, in order:</div>';
    e.seqs.forEach((id,k)=>{const last=k==e.seqs.length-1,on=!e.on||e.on[k]!==0;
      h+='<div class="row" id="r'+i+'_'+k+'"'+(on?'':' style="opacity:.45"')+'>'+
        '<input type="checkbox" id="o'+i+'_'+k+'"'+(on?' checked':'')+' title="Untick to skip this line"'+
        ' onchange="dirty=true;onShow('+i+','+k+')">'+(k+1)+'. '+seqSelect(i,k,id)+
        '<span title="How often this sequence runs in a row">&#215;<input id="t'+i+'_'+k+'" type="number" min="1" max="99" step="1" value="'+
          ((e.times&&e.times[k])||1)+'" style="width:55px" oninput="dirty=true;evShow('+i+','+k+')"></span>'+
        '<span id="ev'+i+'_'+k+'"'+(((e.times&&e.times[k])||1)>1?'':' style="display:none"')+'>every '+
          '<input id="e'+i+'_'+k+'" type="number" min="100" max="60000" step="100" value="'+
          ((e.every&&e.every[k])||500)+'" style="width:80px" oninput="dirty=true"> ms</span>'+
        '<button class="small" title="Move up" onclick="moveSeq('+i+','+k+',-1)"'+(k?'':' disabled')+'>&#8593;</button>'+
        '<button class="small" title="Move down" onclick="moveSeq('+i+','+k+',1)"'+(last?' disabled':'')+'>&#8595;</button>'+
        '<button class="small del" title="Remove from this entry" onclick="delSeq('+i+','+k+')">&#10005;</button>'+
        (last?'':'<span>then pause <input id="p'+i+'_'+k+'" type="number" min="100" max="600000" step="100" value="'+
          e.pauses[k]+'" style="width:90px" oninput="dirty=true"> ms</span>')+'</div>';});
    h+='<div class="row"><button class="small" onclick="addSeq('+i+')"'+(seqs.length&&e.seqs.length<8?'':' disabled')+'>+ sequence</button>'+
      '<span class="hint">Last run: '+(e.last?esc(e.last):'never')+'</span></div></div>';
    return h;
  }).join('');
}

function addEntry(){sync();sch.push({name:'Entry '+(sch.length+1),en:true,time:'08:00',days:62,
  seqs:seqs.length?[seqs[0].id]:[],pauses:seqs.length?[3000]:[],times:seqs.length?[1]:[],every:seqs.length?[500]:[],on:seqs.length?[1]:[],last:''});dirty=true;render();}
function delEntry(i){if(!confirm('Delete entry "'+sch[i].name+'"?'))return;sync();sch.splice(i,1);dirty=true;render();}
function onShow(i,k){const c=$('o'+i+'_'+k),r=$('r'+i+'_'+k);if(c&&r)r.style.opacity=c.checked?'':'.45';}
function evShow(i,k){const t=$('t'+i+'_'+k),e=$('ev'+i+'_'+k);if(t&&e)e.style.display=(+t.value>1)?'':'none';}
function addSeq(i){sync();sch[i].seqs.push(seqs[0].id);sch[i].pauses.push(3000);sch[i].times.push(1);sch[i].every.push(500);sch[i].on.push(1);dirty=true;render();}
function delSeq(i,k){sync();sch[i].seqs.splice(k,1);sch[i].pauses.splice(k,1);sch[i].times.splice(k,1);sch[i].every.splice(k,1);sch[i].on.splice(k,1);dirty=true;render();}
function moveSeq(i,k,d){sync();const e=sch[i],j=k+d;if(j<0||j>=e.seqs.length)return;   // its pause moves with it
  [e.seqs[k],e.seqs[j]]=[e.seqs[j],e.seqs[k]];[e.pauses[k],e.pauses[j]]=[e.pauses[j],e.pauses[k]];[e.times[k],e.times[j]]=[e.times[j],e.times[k]];[e.every[k],e.every[j]]=[e.every[j],e.every[k]];[e.on[k],e.on[j]]=[e.on[j],e.on[k]];
  dirty=true;render();}
function setDays(i,m){sync();sch[i].days=m;dirty=true;render();}

function save(){sync();
  const body={entries:sch.map(e=>({name:e.name,en:e.en,time:e.time,days:e.days,seqs:e.seqs,pauses:e.pauses,times:e.times,every:e.every,on:e.on}))};
  fetch('/api/schedule',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
   .then(r=>r.text().then(t=>{msg(t,r.ok);if(r.ok)load();}))
   .catch(()=>msg('Connection lost'));}

function runNow(i){
  if(dirty){msg('Save the schedule first - Run now uses the saved version');return;}
  fetch('/api/schedule/run',{method:'POST',body:new URLSearchParams({idx:i})})
   .then(r=>r.text().then(t=>msg(t,r.ok)));}

function tick(){fetch('/api/time').then(r=>r.json()).then(t=>{
  $('time').textContent=t.time;$('time').className='v '+(t.synced?'on':'off');}).catch(()=>{});}
window.addEventListener('beforeunload',e=>{if(dirty){e.preventDefault();e.returnValue='';}});
load();tick();setInterval(tick,5000);
</script></body></html>)HTML";

void AddScheduleRoutes(AsyncWebServer &server) {
    server.on("/schedule", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", SCHED_HTML);
    });

    server.on("/api/time", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json",
                  "{\"name\":\"" + JsonEscape(RxSysName()) + "\",\"time\":\"" + SchedTimeText() +
                  "\",\"synced\":" + (SchedTimeSynced() ? "true" : "false") + "}");
    });

    // Before "/api/schedule": that handler would also match this sub-path
    server.on("/api/schedule/run", HTTP_POST, [](AsyncWebServerRequest *req) {
        bool ok = SchedRunNow(ArgInt(req, "idx", -1));
        if (ok) req->send(200, "text/plain", "Running - see Status page log");
        else    req->send(400, "text/plain", "Could not start (busy or unknown entry)");
    });

    server.on("/api/schedule", HTTP_GET, [](AsyncWebServerRequest *req) {
        String j = "{\"seqs\":[";
        bool first = true;
        for (auto &s : SeqList()) {
            if (!first) j += ",";
            first = false;
            j += "{\"id\":" + String(s.id) + ",\"name\":\"" + JsonEscape(s.name) + "\"}";
        }
        j += "],\"entries\":[";
        first = true;
        for (auto &e : SchedGet()) {
            if (!first) j += ",";
            first = false;
            j += "{\"name\":\"" + JsonEscape(e.name) + "\",\"en\":" + (e.enabled ? "true" : "false");
            j += ",\"time\":\"" + JsonEscape(e.time) + "\",\"days\":" + String(e.days);
            j += ",\"last\":\"" + JsonEscape(e.lastRun) + "\",\"seqs\":[";
            for (size_t k = 0; k < e.seqs.size(); k++) j += (k ? "," : "") + String(e.seqs[k]);
            j += "],\"pauses\":[";
            for (size_t k = 0; k < e.pauses.size(); k++) j += (k ? "," : "") + String(e.pauses[k]);
            j += "],\"times\":[";
            for (size_t k = 0; k < e.times.size(); k++) j += (k ? "," : "") + String(e.times[k]);
            j += "],\"every\":[";
            for (size_t k = 0; k < e.every.size(); k++) j += (k ? "," : "") + String(e.every[k]);
            j += "],\"on\":[";
            for (size_t k = 0; k < e.on.size(); k++) j += (k ? "," : "") + String(e.on[k] ? 1 : 0);
            j += "]}";
        }
        j += "]}";
        req->send(200, "application/json", j);
    });

    server.on("/api/schedule", HTTP_POST, [](AsyncWebServerRequest *req) {
        DynamicJsonDocument doc(16384);
        if (deserializeJson(doc, RequestBody(req))) { req->send(400, "text/plain", "Bad request"); return; }
        std::vector<SchedEntry> entries;
        for (JsonObject o : doc["entries"].as<JsonArray>()) {
            SchedEntry e;
            e.name    = o["name"] | "";
            e.name.trim();
            e.enabled = o["en"]   | false;
            e.time    = o["time"] | "";
            e.days    = (o["days"] | 0) & 0x7F;
            SchedReadSeqs(o, e);
            entries.push_back(e);
        }
        String err = SchedSet(entries);
        if (err.length()) req->send(400, "text/plain", err);
        else              req->send(200, "text/plain", "Schedule saved");
    }, NULL, CollectBody);

}
