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
//   GET  /api/schedule                  entries + sequence list + time sync
//   POST /api/schedule    JSON {entries:[{name,en,time,days,seqs,pauses,times,every,on}]}
//   POST /api/schedule/run idx          run a saved entry now
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
#include "Clock.h"

static const char SEQ_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TV IR IN ONE - Sequences</title>
<link rel="stylesheet" href="/style.css">
<style>.entry{background:#0f172a;border-radius:6px;padding:10px;margin-bottom:10px}
.days label{margin-right:8px;white-space:nowrap}</style>
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

<div class="card"><h3>Schedule</h3>
<div class="hint">Each entry runs its sequences, in order, at the set time on the ticked days
(board time, top of the page). Use &#8593; &#8595; to change the order; the pause after a sequence
gives that device time to start before the next one (the pause moves with its sequence).
&#215; runs a sequence several times in a row, e.g. Volume down &#215;30 every 400 ms;
then pause is waited once, after all those runs. Untick a line to skip it (and its pause)
without removing it.
Runs missed while the board was off or had no time are not caught up.</div>
<div id="schSync"></div>
<div id="sch">loading…</div>
<div class="row"><button onclick="schAdd()">+ Add entry</button>
<button class="ok" onclick="schSave()">Save schedule</button></div>
<div id="schMsg" class="off"></div></div>

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
      '<div class="hint">Then press the remote buttons one at a time, pointed at this board&#39;s IR receiver. '+
      'Any remote works: DSTV, TV, sound bar, aircon. While recording, DSTV forwarding to the IR LED is paused.</div>';
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
let seqs=[],seqSig='';
function loadList(){fetch('/api/seq').then(r=>r.json()).then(s=>{
  seqs=s.list;
  const sig=s.list.map(q=>q.id+':'+q.name).join('|');
  if(sig!==seqSig){seqSig=sig;if(!schDirty)schLoad(false);}
  $('running').textContent=s.running||'-';
  $('store').textContent='Storage: '+Math.round(s.used/1024)+' of '+Math.round(s.total/1024)+' KB used';
  if(!s.list.length){$('list').innerHTML='<tr><td colspan="3">No sequences yet</td></tr>';return;}
  $('list').innerHTML=s.list.map(q=>'<tr><td>'+esc(q.name)+'</td><td>'+q.steps+'</td><td>'+
    '<button class="small ok" onclick="run('+q.id+')">Run</button>'+
    '<button class="small" onclick="edit('+q.id+')">Edit</button>'+
    '<button class="small del" onclick="del('+q.id+')">Delete</button></td></tr>').join('');
}).catch(()=>{});}
function run(id){post('/api/seq/run',{id}).then(r=>{msg('listMsg',r.text,r.ok);loadList();});}
function del(id){const q=seqs.find(x=>x.id==id);if(!q)return;
  const used=sch.filter(e=>e.seqs.includes(id)).map(e=>e.name);
  if(!confirm('Delete sequence "'+q.name+'"?'+(used.length?'\n\nIt is also removed from schedule entries: '+used.join(', '):'')))return;
  post('/api/seq/delete',{id}).then(r=>{msg('listMsg',r.text,r.ok);loadList();schLoad(true);if(ed&&ed.id==id)closeEd();});}

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

// ── Schedule ────────────────────────────────────────────────────────────────
const DAYS=[['Mon',1],['Tue',2],['Wed',3],['Thu',4],['Fri',5],['Sat',6],['Sun',0]];
let sch=[],schSeqs=[],schDirty=false;
function schSeqName(id){const q=schSeqs.find(x=>x.id==id);return q?q.name:'(deleted #'+id+')';}
function schLoad(force){if(schDirty&&!force)return;fetch('/api/schedule').then(r=>r.json()).then(s=>{
  schSeqs=s.seqs;sch=s.entries;schDirty=false;
  $('schSync').innerHTML=s.synced?'':'<p class="off">Board time is not synced (no internet) - nothing runs until it is.</p>';
  schRender();
}).catch(()=>{$('sch').textContent='connection lost…';});}
function schSync(){sch.forEach((e,i)=>{
  e.en=$('en'+i).checked;e.name=$('nm'+i).value;e.time=$('tm'+i).value;
  e.days=0;DAYS.forEach(d=>{if($('d'+i+'_'+d[1]).checked)e.days|=1<<d[1];});
  e.seqs=e.seqs.map((_,k)=>+$('s'+i+'_'+k).value);
  e.pauses=e.pauses.map((p,k)=>$('p'+i+'_'+k)?Math.max(100,Math.round(+$('p'+i+'_'+k).value)||100):p);
  e.times=e.seqs.map((_,k)=>Math.max(1,Math.min(99,Math.round(+$('t'+i+'_'+k).value)||1)));
  e.every=e.seqs.map((_,k)=>Math.max(100,Math.min(60000,Math.round(+$('e'+i+'_'+k).value)||500)));
  e.on=e.seqs.map((_,k)=>$('o'+i+'_'+k).checked?1:0);
});}
function schSel(i,k,val){
  let h='<select id="s'+i+'_'+k+'" onchange="schDirty=true">';
  if(!schSeqs.find(x=>x.id==val))h+='<option value="'+val+'">'+esc(schSeqName(val))+'</option>';
  schSeqs.forEach(q=>{h+='<option value="'+q.id+'"'+(q.id==val?' selected':'')+'>'+esc(q.name)+'</option>';});
  return h+'</select>';
}
function schRender(){
  if(!sch.length){$('sch').innerHTML='<div class="hint">No entries yet - click "+ Add entry".'+
    (schSeqs.length?'':' Record a sequence first.')+'</div>';return;}
  $('sch').innerHTML=sch.map((e,i)=>{
    let h='<div class="entry"><div class="row">'+
      '<label><input type="checkbox" id="en'+i+'"'+(e.en?' checked':'')+' onchange="schDirty=true"> Enabled</label>'+
      'Name <input id="nm'+i+'" maxlength="32" value="'+esc(e.name)+'" oninput="schDirty=true">'+
      'Time <input id="tm'+i+'" type="time" value="'+esc(e.time)+'" onchange="schDirty=true">'+
      '<button class="small ok" onclick="schRunNow('+i+')">Run now</button>'+
      '<button class="small del" onclick="schDel('+i+')">Delete</button></div>'+
      '<div class="row days">Days '+DAYS.map(d=>'<label><input type="checkbox" id="d'+i+'_'+d[1]+'"'+
        ((e.days>>d[1])&1?' checked':'')+' onchange="schDirty=true"> '+d[0]+'</label>').join('')+
      '<button class="small" onclick="schDays('+i+',62)">Mon-Fri</button>'+
      '<button class="small" onclick="schDays('+i+',65)">Sat-Sun</button>'+
      '<button class="small" onclick="schDays('+i+',127)">Every day</button></div>'+
      '<div class="hint">Sequences, in order:</div>';
    e.seqs.forEach((id,k)=>{const last=k==e.seqs.length-1,on=!e.on||e.on[k]!==0;
      h+='<div class="row" id="r'+i+'_'+k+'"'+(on?'':' style="opacity:.45"')+'>'+
        '<input type="checkbox" id="o'+i+'_'+k+'"'+(on?' checked':'')+' title="Untick to skip this line"'+
        ' onchange="schDirty=true;onShow('+i+','+k+')">'+(k+1)+'. '+schSel(i,k,id)+
        '<span title="How often this sequence runs in a row">&#215;<input id="t'+i+'_'+k+'" type="number" min="1" max="99" step="1" value="'+
          ((e.times&&e.times[k])||1)+'" style="width:55px" oninput="schDirty=true;evShow('+i+','+k+')"></span>'+
        '<span id="ev'+i+'_'+k+'"'+(((e.times&&e.times[k])||1)>1?'':' style="display:none"')+'>every '+
          '<input id="e'+i+'_'+k+'" type="number" min="100" max="60000" step="100" value="'+
          ((e.every&&e.every[k])||500)+'" style="width:80px" oninput="schDirty=true"> ms</span>'+
        '<button class="small" title="Move up" onclick="schMove('+i+','+k+',-1)"'+(k?'':' disabled')+'>&#8593;</button>'+
        '<button class="small" title="Move down" onclick="schMove('+i+','+k+',1)"'+(last?' disabled':'')+'>&#8595;</button>'+
        '<button class="small del" title="Remove from this entry" onclick="schDelSeq('+i+','+k+')">&#10005;</button>'+
        (last?'':'<span>then pause <input id="p'+i+'_'+k+'" type="number" min="100" max="600000" step="100" value="'+
          e.pauses[k]+'" style="width:90px" oninput="schDirty=true"> ms</span>')+'</div>';});
    h+='<div class="row"><button class="small" onclick="schAddSeq('+i+')"'+(schSeqs.length&&e.seqs.length<8?'':' disabled')+'>+ sequence</button>'+
      '<span class="hint">Last run: '+(e.last?esc(e.last):'never')+'</span></div></div>';
    return h;
  }).join('');
}
function schAdd(){schSync();sch.push({name:'Entry '+(sch.length+1),en:true,time:'08:00',days:62,
  seqs:schSeqs.length?[schSeqs[0].id]:[],pauses:schSeqs.length?[3000]:[],times:schSeqs.length?[1]:[],every:schSeqs.length?[500]:[],on:schSeqs.length?[1]:[],last:''});schDirty=true;schRender();}
function schDel(i){if(!confirm('Delete schedule entry "'+sch[i].name+'"? (Save schedule to make it final)'))return;
  schSync();sch.splice(i,1);schDirty=true;schRender();}
function onShow(i,k){const c=$('o'+i+'_'+k),r=$('r'+i+'_'+k);if(c&&r)r.style.opacity=c.checked?'':'.45';}
function evShow(i,k){const t=$('t'+i+'_'+k),e=$('ev'+i+'_'+k);if(t&&e)e.style.display=(+t.value>1)?'':'none';}
function schAddSeq(i){schSync();sch[i].seqs.push(schSeqs[0].id);sch[i].pauses.push(3000);sch[i].times.push(1);sch[i].every.push(500);sch[i].on.push(1);schDirty=true;schRender();}
function schDelSeq(i,k){schSync();sch[i].seqs.splice(k,1);sch[i].pauses.splice(k,1);sch[i].times.splice(k,1);sch[i].every.splice(k,1);sch[i].on.splice(k,1);schDirty=true;schRender();}
function schMove(i,k,d){schSync();const e=sch[i],j=k+d;if(j<0||j>=e.seqs.length)return;   // its pause moves with it
  [e.seqs[k],e.seqs[j]]=[e.seqs[j],e.seqs[k]];[e.pauses[k],e.pauses[j]]=[e.pauses[j],e.pauses[k]];[e.times[k],e.times[j]]=[e.times[j],e.times[k]];[e.every[k],e.every[j]]=[e.every[j],e.every[k]];[e.on[k],e.on[j]]=[e.on[j],e.on[k]];
  schDirty=true;schRender();}
function schDays(i,m){schSync();sch[i].days=m;schDirty=true;schRender();}
function schSave(){schSync();
  const body={entries:sch.map(e=>({name:e.name,en:e.en,time:e.time,days:e.days,seqs:e.seqs,pauses:e.pauses,times:e.times,every:e.every,on:e.on}))};
  fetch('/api/schedule',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
   .then(r=>r.text().then(t=>{msg('schMsg',t,r.ok);if(r.ok)schLoad(true);}))
   .catch(()=>msg('schMsg','Connection lost'));}
function schRunNow(i){
  if(schDirty){msg('schMsg','Save the schedule first - Run now uses the saved version');return;}
  post('/api/schedule/run',{idx:i}).then(r=>{msg('schMsg',r.text,r.ok);loadList();});}
window.addEventListener('beforeunload',e=>{if(schDirty){e.preventDefault();e.returnValue='';}});

pollRec();loadList();schLoad(true);setInterval(pollRec,600);setInterval(loadList,3000);
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
                   "Sequence saved - it can now be run");
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
        String name = SeqName(id);
        bool ok = SeqDelete(id);
        if (ok) SchedForgetSequence(id);
        sendResult(req, ok ? "" : "Delete failed", "Deleted '" + name + "'");
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

    // ── Schedule ─────────────────────────────────────────────────────────────
    // Before "/api/schedule": that handler would also match this sub-path
    server.on("/api/schedule/run", HTTP_POST, [](AsyncWebServerRequest *req) {
        bool ok = SchedRunNow(ArgInt(req, "idx", -1));
        if (ok) req->send(200, "text/plain", "Running - see Now running above");
        else    req->send(400, "text/plain", "Could not start (busy or unknown entry)");
    });

    server.on("/api/schedule", HTTP_GET, [](AsyncWebServerRequest *req) {
        String j = "{\"synced\":" + String(ClockSynced() ? "true" : "false") + ",\"seqs\":[";
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
