// =============================================================================
// 18_RecSeq.cpp — /recseq page: record an IR sequence for the TV IR RX
// =============================================================================

#include <Arduino.h>
#include "IrLink.h"
#include "all_includes.h"

static const char RECSEQ_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TV IR TX - IR Sequences</title>
<style>
body{font-family:sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:16px;font-size:.95em}
h2{color:#0284c7;margin:0 0 4px}p{color:#94a3b8;margin:4px 0 10px;max-width:620px}a{color:#38bdf8}
.card{background:#1e293b;border-radius:8px;padding:12px;max-width:620px;margin-bottom:12px}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:8px 0}
input{background:#0f172a;color:#e2e8f0;border:1px solid #334155;border-radius:4px;padding:6px;font-size:1em}
button{background:#0284c7;color:#fff;border:none;border-radius:4px;padding:9px 14px;font-size:1em;cursor:pointer}
button:disabled{opacity:.4;cursor:default}
button.ok{background:#15803d}button.warn{background:#b45309}button.del{background:#b91c1c}
.hint{color:#94a3b8;font-size:.9em}.big{font-size:1.15em;margin:10px 0}
.on{color:#4ade80}.off{color:#f87171}
.nav{display:flex;gap:6px;margin-bottom:12px;flex-wrap:wrap}.nav a{color:#e2e8f0;text-decoration:none;background:#1e293b;padding:6px 12px;border-radius:6px}
.nav a.act{background:#0284c7}
</style></head><body>
<div class="nav"><a href="/">STATUS</a><a class="act">IR SEQUENCES</a><a href="/settings">SETTINGS</a><a href="/wifi">WIFI</a><a href="/files">FILES</a><a href="/about">ABOUT</a></div>
<h2>Record an IR sequence</h2>
<p>Point the remote at <b>this</b> board and press the buttons one at a time. Any remote works:
DSTV, TV, sound bar. The <b>receiver</b> keeps the recording and saves the sequence in its list;
run, edit and schedule it on the receiver's web page. While recording, every button you press
is also sent out by the receiver, so you can see it work there.</p>
<div class="card"><div id="link">…</div><div id="rec">loading…</div><div id="msg"></div></div>
<script>
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function $(id){return document.getElementById(id);}
function msg(t,ok){const e=$('msg');e.className=ok?'on':'off';e.textContent=t;
  if(t)setTimeout(()=>{if(e.textContent==t)e.textContent='';},6000);}
let sent=0,sentAt=0,shown=0,key='',link=false;
function poll(){fetch('/api/rec').then(r=>r.json()).then(render)
  .catch(()=>{$('link').innerHTML='<span class="off">Connection to this board lost…</span>';});}
function render(s){
  link=s.link;
  let l=s.link?'<span class="on">Receiver: '+esc(s.rx||'found')+'</span>'
              :'<span class="off">Receiver not found - recording needs the link to the receiver</span>';
  if(s.link&&s.age<0)l+=' <span class="off">- no answer: update the receiver firmware to 1.6.0 or later</span>';
  else if(s.link&&s.age>5000)l+=' <span class="off">- not answering</span>';
  if(s.ip)l+=' &nbsp;<a href="http://'+s.ip+'/sequences" target="_blank">Open receiver page</a>';
  $('link').innerHTML=l;
  if(sent&&shown!=sent){
    if(s.ack==sent){shown=sent;msg(s.msg,s.ok);}
    else if(Date.now()-sentAt>3000){shown=sent;msg('No answer from the receiver - try again');}
  }
  if(s.note)msg(s.note);
  const k=s.state+'|'+s.pending+'|'+s.steps+'|'+s.name+'|'+s.link+'|'+s.mine;
  if(k===key)return;
  key=k;
  let h='';
  const kept=s.steps+' stroke(s) kept'+(s.last?' (last: '+esc(s.last)+')':'');
  if(s.state==0){
    h='<div class="row">Sequence name <input id="name" maxlength="32" placeholder="e.g. Reset TV">'+
      '<button class="ok" onclick="start()"'+(s.link?'':' disabled')+'>Start recording</button></div>';
  }else if(!s.mine){
    h='<p class="big">&#9888; <b>'+esc(s.name)+'</b> is being recorded from another TX room or the receiver page.</p>'+
      '<div class="hint">Finish it there, or cancel it here.</div>'+
      '<div class="row"><button class="del" onclick="cancel()">Cancel that recording</button></div>';
  }else if(s.state==1){
    h='<div class="row">Recording <b>'+esc(s.name)+'</b> - '+kept+'</div>'+
      '<p class="big">&#9654; Press button '+(s.steps+1)+' on the remote, pointed at this board…</p>'+
      '<div class="hint">Press the button once, briefly.</div>'+
      '<div class="row"><button class="ok" onclick="cmd(\'finish\')"'+(s.steps?'':' disabled')+
      '>Accept sequence as completed</button><button class="del" onclick="cancel()">Cancel</button></div>';
  }else{
    const n=s.steps+1;
    h='<div class="row">Recording <b>'+esc(s.name)+'</b> - '+kept+'</div>'+
      '<p class="big">&#10004; Received: '+esc(s.pending)+'</p>'+
      '<div class="row">Label <input id="label" maxlength="32" value="Step '+n+'"></div>'+
      '<div class="row">Delay after this stroke <input id="delay" type="number" min="0" max="60000" step="100" value="500" style="width:90px"> ms</div>'+
      '<div class="row"><button onclick="cmd(\'test\')">Test (receiver sends it)</button>'+
      '<button class="warn" onclick="cmd(\'retry\')">Re-record this stroke</button></div>'+
      '<div class="row"><button onclick="step(\'next\')">Next IR &#9654;</button>'+
      '<button class="ok" onclick="step(\'finish\')">Accept sequence as completed</button>'+
      '<button class="del" onclick="cancel()">Cancel</button></div>';
  }
  $('rec').innerHTML=h;
  if(s.state==2){const e=$('label');e.focus();e.select();}
}
function cmd(c,p){
  p=Object.assign({cmd:c},p||{});
  fetch('/api/rec/cmd',{method:'POST',body:new URLSearchParams(p)})
   .then(r=>r.text().then(t=>{if(!r.ok){msg(t);return;}sent=+t;sentAt=Date.now();shown=0;setTimeout(poll,200);}))
   .catch(()=>msg('Connection lost'));
}
function start(){const n=$('name').value.trim();if(!n){msg('Enter a sequence name');return;}cmd('start',{text:n});}
function step(c){const l=$('label'),d=$('delay');cmd(c,l?{text:l.value,delay:d.value}:{});}
function cancel(){if(confirm('Cancel this recording? Strokes recorded so far are discarded.'))cmd('cancel');}
poll();setInterval(poll,500);
</script></body></html>)HTML";

static uint8_t cmdCode(const String &c) {
	if (c == "start")  return RECCMD_START;
	if (c == "next")   return RECCMD_NEXT;
	if (c == "finish") return RECCMD_FINISH;
	if (c == "retry")  return RECCMD_RETRY;
	if (c == "cancel") return RECCMD_CANCEL;
	if (c == "test")   return RECCMD_TEST;
	return 0xFF;
}

static String postArg(AsyncWebServerRequest *req, const char *name) {
	return req->hasParam(name, true) ? req->getParam(name, true)->value() : String("");
}

void RecSeqAddRoutes() {
	server.on("/recseq", HTTP_GET, [](AsyncWebServerRequest *req) {
		req->send(200, "text/html", RECSEQ_HTML);
	});

	// Before "/api/rec": that handler would also match this sub-path
	server.on("/api/rec/cmd", HTTP_POST, [](AsyncWebServerRequest *req) {
		uint8_t cmd = cmdCode(postArg(req, "cmd"));
		if (cmd == 0xFF) { req->send(400, "text/plain", "Unknown command"); return; }
		String   text    = postArg(req, "text");
		long     delayMs = postArg(req, "delay").toInt();
		text.trim();
		if (delayMs < 0)     delayMs = 0;
		if (delayMs > 60000) delayMs = 60000;
		String   err;
		uint16_t seq = EspNowRecCommand(cmd, text, (uint32_t)delayMs, err);
		if (!seq) req->send(503, "text/plain", err);
		else      req->send(200, "text/plain", String(seq));
	});

	server.on("/api/rec", HTTP_GET, [](AsyncWebServerRequest *req) {
		req->send(200, "application/json", EspNowRecJson());
	});
}
