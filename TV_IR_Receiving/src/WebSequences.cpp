// =============================================================================
// WebSequences.cpp — /sequences page: record, run, edit and delete sequences
// =============================================================================
//   GET  /api/rec                       record state (polled by the page)
//   POST /api/rec/start   name          start recording a new sequence
//   POST /api/rec/next    label, delay  keep the received stroke, wait for next
//   POST /api/rec/finish  label, delay  keep it (if any) and save the sequence
//   POST /api/rec/retry | cancel | test
//   GET  /api/seq                       list + running state + storage use
//   GET  /api/seq/get     id            one sequence (no IR data)
//   POST /api/seq/run | delete   id
//   POST /api/seq/test    id, step      send one stroke
//   POST /api/seq/testcode gen          send a known code, e.g. SAMSUNG:E0E09966
//   POST /api/seq/update  JSON {id,name,steps:[{src,gen,label,delay}]}  rename,
//                                        (id 0 = new sequence; a step with gen
//                                        is a known code instead of src)
//                                        re-label, re-time, reorder, remove
// =============================================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "WebUtil.h"
#include "IrRecord.h"
#include "SeqStore.h"
#include "Sequencer.h"
#include "Scheduler.h"
#include "IrTransmit.h"

static const char SEQ_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TV IR RX - Sequences</title>
<link rel="stylesheet" href="/style.css">
</head><body>
<div id="hdr" data-page="sequences" data-msg="Record / run IR sequences"></div><script src="/common.js"></script>

<div class="card"><h3>Record a new sequence</h3><div id="rec">loading…</div>
<div id="recMsg" class="off"></div></div>

<div class="card" id="edCard" style="display:none"><h3>Edit sequence</h3><div id="ed"></div>
<div id="edMsg" class="off"></div></div>

<div class="card"><h3>Saved sequences</h3>
<div class="row"><span class="k">Now running:</span><span id="running" class="v">-</span></div>
<div class="scroll"><table><thead><tr><th>Name</th><th>Steps</th><th></th></tr></thead>
<tbody id="list"><tr><td colspan="3">No sequences yet</td></tr></tbody></table></div>
<div class="row"><button onclick="edNew()">+ New sequence from known codes</button></div>
<div class="hint" id="store"></div><div id="listMsg" class="off"></div></div>

<script>
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function $(id){return document.getElementById(id);}
function msg(id,t,ok){const e=$(id);e.className=ok?'on':'off';e.textContent=t;if(t)setTimeout(()=>{if(e.textContent==t)e.textContent='';},5000);}
function post(url,params){
  return fetch(url,{method:'POST',body:new URLSearchParams(params||{})})
   .then(r=>r.text().then(t=>({ok:r.ok,text:t})))
   .catch(()=>({ok:false,text:'Connection lost'}));
}

// ── Record ──────────────────────────────────────────────────────────────────
let recKey='';
function stepsTable(steps){
  if(!steps.length)return '';
  let h='<table><thead><tr><th>#</th><th>Label</th><th>Delay after</th><th>IR code</th></tr></thead><tbody>';
  steps.forEach((s,i)=>{h+='<tr><td>'+(i+1)+'</td><td>'+esc(s.label)+'</td><td>'+s.delay+' ms</td><td class="hint">'+esc(s.code)+'</td></tr>';});
  return h+'</tbody></table>';
}
function renderRec(r){
  const key=r.state+'|'+r.pending+'|'+r.steps.length+'|'+r.name;
  if(key===recKey)return;
  recKey=key;
  let h='';
  if(r.state==0){
    h='<div class="row">1. Sequence name <input id="recName" maxlength="32" placeholder="e.g. Reset">'+
      '<button class="ok" onclick="recStart()">Start recording</button></div>'+
      '<div class="hint">Then press the remote buttons one at a time, pointed at this receiver board. '+
      'Any remote works: DSTV, TV, sound bar, aircon. No IR eye here? Record from the TX web page '+
      '(IR SEQUENCES) with the remote pointed at the TX - the sequence still lands in this list.</div>';
  } else {
    h='<div class="row">Recording <b>'+esc(r.name)+'</b> — '+r.steps.length+' stroke(s) so far</div>'+stepsTable(r.steps);
    const n=r.steps.length+1;
    if(r.state==1){
      h+='<p class="big">&#9654; Press button '+n+' on the remote…</p>'+
         '<div class="hint">Waiting for IR. Press the button once, briefly.</div>'+
         '<div class="row"><button class="ok" onclick="recFinish()"'+(r.steps.length?'':' disabled')+
         '>Accept sequence as completed</button><button class="del" onclick="recCancel()">Cancel</button></div>';
    } else {
      h+='<p class="big">&#10004; Received: '+esc(r.pending)+'</p>'+
         '<div class="row">Label <input id="recLabel" maxlength="32" value="Step '+n+'"> '+
         'Delay after this stroke <input id="recDelay" type="number" min="0" max="60000" step="100" value="500" style="width:90px"> ms</div>'+
         '<div class="row"><button onclick="recAct(\'test\')">Test send</button>'+
         '<button class="warn" onclick="recAct(\'retry\')">Re-record this stroke</button></div>'+
         '<div class="row"><button onclick="recNext()">Next IR &#9654;</button>'+
         '<button class="ok" onclick="recFinish()">Accept sequence as completed</button>'+
         '<button class="del" onclick="recCancel()">Cancel</button></div>';
    }
  }
  $('rec').innerHTML=h;
  if(r.state==2){const l=$('recLabel');l.focus();l.select();}
}
function pollRec(){fetch('/api/rec').then(r=>r.json()).then(renderRec).catch(()=>{});}
function recStart(){post('/api/rec/start',{name:$('recName').value}).then(r=>{if(!r.ok)msg('recMsg',r.text);pollRec();});}
function stepParams(){const l=$('recLabel'),d=$('recDelay');return l?{label:l.value,delay:d.value}:{};}
function recNext(){post('/api/rec/next',stepParams()).then(r=>{if(!r.ok)msg('recMsg',r.text);pollRec();});}
function recFinish(){post('/api/rec/finish',stepParams()).then(r=>{msg('recMsg',r.text,r.ok);pollRec();loadList();});}
function recAct(a){post('/api/rec/'+a).then(r=>{if(!r.ok)msg('recMsg',r.text);pollRec();});}
function recCancel(){if(confirm('Cancel this recording? Strokes recorded so far are discarded.'))recAct('cancel');}

// ── Saved list ──────────────────────────────────────────────────────────────
let seqs=[];
function loadList(){fetch('/api/seq').then(r=>r.json()).then(s=>{
  seqs=s.list;
  $('running').textContent=s.running||'-';
  $('store').textContent='Storage: '+Math.round(s.used/1024)+' of '+Math.round(s.total/1024)+' KB used';
  if(!s.list.length){$('list').innerHTML='<tr><td colspan="3">No sequences yet</td></tr>';return;}
  $('list').innerHTML=s.list.map(q=>'<tr><td>'+esc(q.name)+'</td><td>'+q.steps+'</td><td>'+
    '<button class="small ok" onclick="run('+q.id+')">Run</button>'+
    '<button class="small" onclick="edit('+q.id+')">Edit</button>'+
    '<button class="small del" onclick="del('+q.id+')">Delete</button></td></tr>').join('');
}).catch(()=>{});}
function run(id){post('/api/seq/run',{id}).then(r=>{msg('listMsg',r.text,r.ok);loadList();});}
function del(id){const q=seqs.find(x=>x.id==id);if(!q||!confirm('Delete sequence "'+q.name+'"?'))return;
  post('/api/seq/delete',{id}).then(r=>{msg('listMsg',r.text,r.ok);loadList();if(ed&&ed.id==id)closeEd();});}

// ── Edit ────────────────────────────────────────────────────────────────────
let ed=null;
function edit(id){fetch('/api/seq/get?id='+id).then(r=>r.json()).then(s=>{
  ed={id:s.id,name:s.name,steps:s.steps.map((x,i)=>({src:i,label:x.label,delay:x.delay,code:x.code}))};
  renderEd();$('edCard').style.display='';$('edCard').scrollIntoView();
});}
function syncEd(){ed.name=$('edName').value;ed.steps.forEach((s,i)=>{s.label=$('edL'+i).value;s.delay=+$('edD'+i).value;});}
function renderEd(){
  let h='<div class="row">Name <input id="edName" maxlength="32" value="'+esc(ed.name)+'"></div>'+
    '<div class="scroll"><table><thead><tr><th>#</th><th>Label</th><th>Delay after (ms)</th><th>IR code</th><th></th></tr></thead><tbody>';
  ed.steps.forEach((s,i)=>{h+='<tr><td>'+(i+1)+'</td><td><input id="edL'+i+'" maxlength="32" value="'+esc(s.label)+'"></td>'+
    '<td><input id="edD'+i+'" type="number" min="0" max="60000" step="100" value="'+s.delay+'" style="width:80px"></td>'+
    '<td class="hint">'+esc(s.code)+'</td><td>'+
    '<button class="small" onclick="edTest('+i+')">Test</button>'+
    '<button class="small" onclick="edMove('+i+',-1)"'+(i?'':' disabled')+'>&#8593;</button>'+
    '<button class="small" onclick="edMove('+i+',1)"'+(i<ed.steps.length-1?'':' disabled')+'>&#8595;</button>'+
    '<button class="small del" onclick="edRemove('+i+')"'+(ed.steps.length>1?'':' disabled')+'>&#10005;</button></td></tr>';});
  if(!ed.steps.length)h+='<tr><td colspan="5" class="hint">No steps yet - add a known code below.</td></tr>';
  h+='</tbody></table></div>'+
     '<div class="row">Add a known code <select id="kc" onchange="kcPick()">'+
       KNOWN.map((k,j)=>'<option value="'+j+'"'+(j==kcSel?' selected':'')+'>'+esc(k[0])+'</option>').join('')+'</select>'+
     '<input id="kcHex" maxlength="10" placeholder="hex, e.g. E0E09966" style="width:140px;display:none">'+
     '<button class="small" onclick="kcTest()">Test</button>'+
     '<button class="small ok" onclick="kcAdd()">+ Add step</button></div>'+
     '<div class="row"><button class="ok" onclick="edSave()">Save changes</button>'+
     '<button onclick="closeEd()">Close</button></div>'+
     '<div class="hint">Test sends the step as it is now. To add recorded strokes, record a new sequence. '+
     'Known codes are built by the board, they are not on the remote: Samsung TV Power ON only switches on '+
     'and Power OFF only switches off, whatever state the TV is in. Volume up also cancels mute.</div>';
  $('ed').innerHTML=h;
  kcPick();
}
// Known codes (Samsung TV standard codes, as used by universal remotes)
const KNOWN=[['Samsung TV - Power ON','SAMSUNG:E0E09966'],['Samsung TV - Power OFF','SAMSUNG:E0E019E6'],
  ['Samsung TV - Power toggle','SAMSUNG:E0E040BF'],['Samsung TV - Volume up','SAMSUNG:E0E0E01F'],
  ['Samsung TV - Volume down','SAMSUNG:E0E0D02F'],['Samsung TV - Mute toggle','SAMSUNG:E0E0F00F'],
  ['Samsung TV - Source','SAMSUNG:E0E0807F'],['Other Samsung code...','SAMSUNG:'],['Other NEC code...','NEC:']];
let kcSel=0;
function kcPick(){const k=$('kc');if(!k)return;kcSel=+k.value;$('kcHex').style.display=KNOWN[kcSel][1].endsWith(':')?'':'none';}
function kcSpec(){const k=KNOWN[kcSel];return k[1].endsWith(':')?k[1]+$('kcHex').value.trim():k[1];}
function kcTest(){post('/api/seq/testcode',{gen:kcSpec()}).then(r=>msg('edMsg',r.text,r.ok));}
function kcAdd(){const g=kcSpec();if(g.endsWith(':')){msg('edMsg','Type the hex code first');return;}
  syncEd();const k=KNOWN[kcSel],p=g.split(':');
  ed.steps.push({src:-1,gen:g,label:(k[1].endsWith(':')?p[0]+' '+p[1]:k[0]).slice(0,32),delay:1000,
    code:p[0]+' 0x'+p[1].replace(/^0x/i,'').toUpperCase()+' (new - not saved yet)'});
  renderEd();}
function edNew(){ed={id:0,name:'',steps:[]};renderEd();$('edCard').style.display='';$('edCard').scrollIntoView();$('edName').focus();}
function edMove(i,d){syncEd();const t=ed.steps[i];ed.steps[i]=ed.steps[i+d];ed.steps[i+d]=t;renderEd();}
function edRemove(i){syncEd();ed.steps.splice(i,1);renderEd();}
function edTest(i){const s=ed.steps[i];
  (s.gen?post('/api/seq/testcode',{gen:s.gen}):post('/api/seq/test',{id:ed.id,step:s.src})).then(r=>msg('edMsg',r.text,r.ok));}
function edSave(){syncEd();
  if(!ed.steps.length){msg('edMsg','Add at least one step');return;}
  const body={id:ed.id,name:ed.name,steps:ed.steps.map(s=>({src:s.src,gen:s.gen||'',label:s.label,delay:s.delay}))};
  fetch('/api/seq/update',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
   .then(r=>r.text().then(t=>{msg(r.ok&&!ed.id?'listMsg':'edMsg',t,r.ok);
     if(r.ok){loadList();if(ed.id)edit(ed.id);else closeEd();}}))
   .catch(()=>msg('edMsg','Connection lost'));}
function closeEd(){ed=null;$('edCard').style.display='none';}

pollRec();loadList();setInterval(pollRec,600);setInterval(loadList,3000);
</script></body></html>)HTML";

static void sendResult(AsyncWebServerRequest *req, const String &err, const String &okText) {
    if (err.length()) req->send(400, "text/plain", err);
    else              req->send(200, "text/plain", okText);
}

static uint32_t clampDelay(long ms) {
    return ms < 0 ? 0 : ms > 60000 ? 60000 : (uint32_t)ms;
}

void AddSequenceRoutes(AsyncWebServer &server) {
    server.on("/sequences", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", SEQ_HTML);
    });

    // ── Record mode ──────────────────────────────────────────────────────────
    server.on("/api/rec/start", HTTP_POST, [](AsyncWebServerRequest *req) {
        sendResult(req, RecStart(ArgStr(req, "name")), "Recording started");
    });
    server.on("/api/rec/next", HTTP_POST, [](AsyncWebServerRequest *req) {
        sendResult(req, RecNext(ArgStr(req, "label"), clampDelay(ArgInt(req, "delay", 500))), "Waiting for next IR");
    });
    server.on("/api/rec/finish", HTTP_POST, [](AsyncWebServerRequest *req) {
        uint16_t id = 0;
        sendResult(req, RecFinish(ArgStr(req, "label"), clampDelay(ArgInt(req, "delay", 500)), id),
                   "Sequence saved - it can now be run and scheduled");
    });
    server.on("/api/rec/retry", HTTP_POST, [](AsyncWebServerRequest *req) {
        sendResult(req, RecRetry(), "Press the button again");
    });
    server.on("/api/rec/cancel", HTTP_POST, [](AsyncWebServerRequest *req) {
        RecCancel();
        req->send(200, "text/plain", "Recording cancelled");
    });
    server.on("/api/rec/test", HTTP_POST, [](AsyncWebServerRequest *req) {
        sendResult(req, RecTestPending() ? "" : "Nothing to send", "Stroke sent");
    });

    // ── Saved sequences ──────────────────────────────────────────────────────

    server.on("/api/seq/get", HTTP_GET, [](AsyncWebServerRequest *req) {
        Sequence seq;
        if (!SeqLoad(ArgInt(req, "id", 0), seq)) { req->send(404, "text/plain", "Sequence not found"); return; }
        String j = "{\"id\":" + String(seq.id) + ",\"name\":\"" + JsonEscape(seq.name) + "\",\"steps\":[";
        for (size_t i = 0; i < seq.steps.size(); i++) {
            const SeqStep &s = seq.steps[i];
            if (i) j += ",";
            j += "{\"label\":\"" + JsonEscape(s.label) + "\",\"delay\":" + String(s.delayMs) +
                 ",\"code\":\"" + JsonEscape(s.code.describe()) + "\"}";
        }
        j += "]}";
        req->send(200, "application/json", j);
    });

    server.on("/api/seq/run", HTTP_POST, [](AsyncWebServerRequest *req) {
        uint16_t id = ArgInt(req, "id", 0);
        String name = SeqName(id);
        if (name.isEmpty()) { req->send(404, "text/plain", "Sequence not found"); return; }
        sendResult(req, SeqRun({ id }, {}, {}, {}, "web") ? "" : "Busy - try again shortly", "Running '" + name + "'");
    });

    server.on("/api/seq/test", HTTP_POST, [](AsyncWebServerRequest *req) {
        bool ok = SeqTestStep(ArgInt(req, "id", 0), ArgInt(req, "step", 0));
        sendResult(req, ok ? "" : "Could not send", "Stroke sent");
    });

    server.on("/api/seq/delete", HTTP_POST, [](AsyncWebServerRequest *req) {
        uint16_t id = ArgInt(req, "id", 0);
        for (auto &e : SchedGet())
            for (uint16_t s : e.seqs)
                if (s == id) {
                    req->send(400, "text/plain", "Used by schedule '" + e.name + "' - remove it there first");
                    return;
                }
        String name = SeqName(id);
        sendResult(req, SeqDelete(id) ? "" : "Delete failed", "Deleted '" + name + "'");
    });

    server.on("/api/seq/update", HTTP_POST, [](AsyncWebServerRequest *req) {
        DynamicJsonDocument doc(8192);
        if (deserializeJson(doc, RequestBody(req))) { req->send(400, "text/plain", "Bad request"); return; }

        Sequence seq;                      // id 0 = new sequence (known codes only)
        uint16_t id = doc["id"] | 0;
        if (id && !SeqLoad(id, seq)) { req->send(404, "text/plain", "Sequence not found"); return; }
        if (!id && SeqList().size() >= SEQ_MAX_SEQUENCES) {
            req->send(400, "text/plain", "Maximum number of sequences reached"); return;
        }

        String name = doc["name"] | "";
        name.trim();
        if (name.isEmpty() || name.length() > SEQ_NAME_LEN) { req->send(400, "text/plain", "Name must be 1-32 characters"); return; }
        if (SeqNameUsed(name, seq.id)) { req->send(400, "text/plain", "Another sequence is called '" + name + "'"); return; }

        std::vector<SeqStep> steps;
        for (JsonObject o : doc["steps"].as<JsonArray>()) {
            SeqStep st;
            const char *gen = o["gen"] | "";
            if (*gen) {                    // known code typed in the editor
                String err;
                if (!IrCodeFromSpec(gen, st.code, err)) { req->send(400, "text/plain", err); return; }
            } else {
                int src = o["src"] | -1;
                if (src < 0 || src >= (int)seq.steps.size()) { req->send(400, "text/plain", "Bad step"); return; }
                st = seq.steps[src];
            }
            st.label   = o["label"] | "";
            st.delayMs = clampDelay(o["delay"] | 500);
            steps.push_back(st);
        }
        if (steps.empty()) { req->send(400, "text/plain", "A sequence needs at least one stroke"); return; }
        if (steps.size() > SEQ_MAX_STEPS) {
            req->send(400, "text/plain", "Too many steps (max " + String(SEQ_MAX_STEPS) + ")"); return;
        }

        seq.name  = name;
        seq.steps = steps;
        sendResult(req, SeqSave(seq) ? "" : "Saving failed", id ? "Saved" : "Sequence '" + name + "' created");
    }, NULL, CollectBody);

    // Not a sub-path of "/api/seq/test" ("/api/seq/test/..." would be)
    server.on("/api/seq/testcode", HTTP_POST, [](AsyncWebServerRequest *req) {
        IrCode code;
        String err;
        if (!IrCodeFromSpec(ArgStr(req, "gen"), code, err)) { req->send(400, "text/plain", err); return; }
        sendResult(req, SendIrCode(code) ? "" : "IR output busy - try again", "Sent " + code.describe());
    });

    // Registered last: "/api/seq" and "/api/rec" would also match their
    // sub-paths (/api/seq/get, /api/rec/start ...) — ESPAsyncWebServer prefix rule
    server.on("/api/rec", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", RecStatusJson());
    });
    server.on("/api/seq", HTTP_GET, [](AsyncWebServerRequest *req) {
        String j = "{\"running\":\"" + JsonEscape(SeqRunningText()) + "\"";
        j += ",\"used\":" + String(StoreUsedBytes()) + ",\"total\":" + String(StoreTotalBytes());
        j += ",\"list\":[";
        bool first = true;
        for (auto &s : SeqList()) {
            if (!first) j += ",";
            first = false;
            j += "{\"id\":" + String(s.id) + ",\"name\":\"" + JsonEscape(s.name) +
                 "\",\"steps\":" + String(s.steps) + "}";
        }
        j += "]}";
        req->send(200, "application/json", j);
    });
}
