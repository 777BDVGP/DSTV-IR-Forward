// =============================================================================
// WebFiles.cpp — /files page: browse, view, download, delete, upload (LittleFS)
// =============================================================================
//   GET  /api/files              {used,total,files:[{name,size}]}  (/ and /seq)
//   GET  /files/view?f=/path     file as text
//   GET  /download?f=/path       file as attachment
//   POST /files/delete  f=/path  delete (sequence list / schedule reloaded)
//   POST /fupload?dir=/ | /seq   multipart upload (restore a backup)
// =============================================================================

#include <Arduino.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include "WebUtil.h"
#include "SeqStore.h"
#include "Scheduler.h"
#include "Status.h"

static const char FILES_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TV IR RX - Files</title>
<link rel="stylesheet" href="/style.css">
</head><body>
<div id="hdr" data-page="files" data-msg="Files on LittleFS"></div><script src="/common.js"></script>

<!-- Same layout, sizes and colours as the TX FILES page (03_Files.cpp) -->
<div id="fsinfo" style="padding:4px 8px 2px"><p style="color:#94a3b8;font-size:12px;padding:4px 0">Loading&hellip;</p></div>

<div style="display:flex;align-items:center;justify-content:space-between;padding:4px 8px 2px">
<b style="font-size:13px;color:#e2e8f0">Files on LittleFS</b>
<button onclick="loadFiles()" style="background:#0284c7;border:none;color:#fff;padding:4px 10px;font-size:12px;border-radius:4px;cursor:pointer;margin:0">&#8635; Refresh</button></div>

<div id="ftbl"><p style="color:#94a3b8;padding:10px;font-size:13px">Loading&hellip;</p></div>
<div id="msg" class="off" style="padding:0 8px;font-size:12px"></div>

<div id="vpanel" style="display:none;margin:4px 0">
<div style="display:flex;align-items:center;justify-content:space-between;background:#334155;padding:4px 8px;border-radius:4px 4px 0 0">
<span id="vfname" style="font-family:monospace;font-size:11px;color:#e2e8f0"></span>
<button onclick="closeView()" style="background:#dc2626;border:none;color:#fff;padding:3px 8px;font-size:12px;border-radius:4px;cursor:pointer;margin:0">&#10005; Close</button></div>
<pre id="vcontent" style="max-height:320px;overflow:auto;background:#0f172a;border:1px solid #334155;border-radius:0 0 4px 4px;padding:8px;font-size:11px;white-space:pre-wrap;word-break:break-all;margin:0"></pre></div>

<div style="padding:8px;margin-top:4px">
<b style="font-size:13px;color:#e2e8f0">Upload File</b><br><br>
<form id="upf" method="POST" enctype="multipart/form-data" action="/fupload?dir=/">
<div style="margin-bottom:10px;font-size:13px">Folder <select id="updir" style="font-size:13px"
 onchange="document.getElementById('upf').action='/fupload?dir='+this.value">
<option value="/">/ (root - AllParams.json, schedule.json)</option><option value="/seq">/seq (recorded sequences)</option></select></div>
<input type="file" name="fupload" style="margin-bottom:10px;display:block;width:100%;font-size:13px">
<button type="submit" style="display:inline-block;padding:8px 14px;border-radius:4px;font-size:.9em;font-weight:bold;border:none;cursor:pointer;margin:4px 0;background:#0284c7;color:#fff">&#8679; Upload</button>
<p style="color:#94a3b8;font-size:12px;padding-top:6px">To restore a backup: upload sequence files (1.json, 2.json &hellip;) into /seq and schedule.json into /.</p>
</form></div>

<script>
function $(id){return document.getElementById(id);}
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function fmtSz(b){if(b<1024)return b+' B';if(b<1048576)return (b/1024).toFixed(1)+' KB';return (b/1048576).toFixed(1)+' MB';}
function msg(t,ok){const e=$('msg');e.className=ok?'on':'off';e.textContent=t;if(t)setTimeout(()=>{if(e.textContent==t)e.textContent='';},5000);}
function nameColor(n){return n.endsWith('.json')?'#f87171':n.endsWith('.txt')?'#e2e8f0':'#4ade80';}
let files=[];
function loadFiles(){fetch('/api/files').then(r=>r.json()).then(j=>{
  const pct=j.total?Math.round(j.used/j.total*100):0;
  $('fsinfo').innerHTML="<div style='padding:4px 8px 2px;font-size:12px;color:#94a3b8'>Used: "+fmtSz(j.used)+" of "+fmtSz(j.total)+" ("+pct+"%)</div>"
    +"<div style='background:#334155;border-radius:4px;height:6px;margin:0 8px 8px'>"
    +"<div style='background:#0284c7;height:6px;border-radius:4px;width:"+pct+"%'></div></div>";
  files=j.files;
  if(!files.length){$('ftbl').innerHTML="<p style='color:#94a3b8;padding:10px;font-size:13px'>No files.</p>";return;}
  let h='<table class="kv"><tr class="hd"><td>File Name</td><td style="text-align:right">Size</td><td></td></tr>';
  files.forEach((f,i)=>{const enc=encodeURIComponent(f.name);
    h+='<tr><td style="font-family:monospace;font-size:12px;width:100%;color:'+nameColor(f.name)+'">'+esc(f.name.replace(/^\//,''))+'</td>'+
       '<td style="font-size:12px;color:#94a3b8;white-space:nowrap;text-align:right">'+fmtSz(f.size)+'</td>'+
       '<td style="white-space:nowrap;text-align:right">'+
       '<a href="/download?f='+enc+'" style="display:inline-block;padding:3px 8px;background:#0284c7;color:#fff;border-radius:4px;font-size:11px;text-decoration:none;margin-right:3px">&#8595;&nbsp;DL</a>'+
       '<button onclick="viewFile('+i+')" style="padding:3px 8px;background:#475569;color:#fff;border:none;border-radius:4px;font-size:11px;cursor:pointer;margin:0 3px 0 0">&#128065;&nbsp;View</button>'+
       '<button onclick="delFile('+i+')" style="padding:3px 8px;background:#dc2626;color:#fff;border:none;border-radius:4px;font-size:11px;cursor:pointer;margin:0">&#10005;&nbsp;Del</button></td></tr>';});
  $('ftbl').innerHTML=h+'</table>';
}).catch(()=>{$('ftbl').textContent='connection lost…';});}
function viewFile(i){const n=files[i].name;fetch('/files/view?f='+encodeURIComponent(n)).then(r=>r.text()).then(t=>{
  $('vfname').textContent=n.replace(/^\//,'');$('vcontent').textContent=t;$('vpanel').style.display='';$('vpanel').scrollIntoView();});}
function closeView(){$('vpanel').style.display='none';}
function delFile(i){const n=files[i].name;if(!confirm('Delete '+n.replace(/^\//,'')+'?'))return;
  fetch('/files/delete',{method:'POST',body:new URLSearchParams({f:n})}).then(r=>r.text().then(t=>{msg(t,r.ok);loadFiles();}));}
loadFiles();
</script></body></html>)HTML";

// Only absolute paths inside the file system, no ".."
static bool validPath(const String &p) {
    return p.startsWith("/") && p.indexOf("..") < 0 && p.length() < 64;
}

static void listDir(const char *dirPath, String &j, bool &first) {
    File dir = LittleFS.open(dirPath);
    if (!dir || !dir.isDirectory()) return;
    String base = String(dirPath);
    if (!base.endsWith("/")) base += "/";
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String path = base + f.name();
        if (f.isDirectory()) {
            f.close();
            listDir(path.c_str(), j, first);
            continue;
        }
        if (!first) j += ",";
        first = false;
        j += "{\"name\":\"" + JsonEscape(path) + "\",\"size\":" + String(f.size()) + "}";
        f.close();
    }
}

// Files may be sequences or the schedule — refresh the in-memory copies
static void reloadStores() {
    SeqReindex();
    SchedReload();
}

void AddFileRoutes(AsyncWebServer &server) {
    server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest *req) {
        String j = "{\"used\":" + String(StoreUsedBytes()) + ",\"total\":" + String(StoreTotalBytes()) + ",\"files\":[";
        bool first = true;
        listDir("/", j, first);
        j += "]}";
        req->send(200, "application/json", j);
    });

    server.on("/files/view", HTTP_GET, [](AsyncWebServerRequest *req) {
        String p = ArgStr(req, "f");
        if (!validPath(p) || !LittleFS.exists(p)) { req->send(404, "text/plain", "File not found"); return; }
        req->send(LittleFS, p, "text/plain");
    });

    server.on("/download", HTTP_GET, [](AsyncWebServerRequest *req) {
        String p = ArgStr(req, "f");
        if (!validPath(p) || !LittleFS.exists(p)) { req->send(404, "text/plain", "File not found"); return; }
        req->send(LittleFS, p, "application/octet-stream", true);
    });

    server.on("/files/delete", HTTP_POST, [](AsyncWebServerRequest *req) {
        String p = ArgStr(req, "f");
        if (!validPath(p) || !LittleFS.exists(p)) { req->send(404, "text/plain", "File not found"); return; }
        bool ok = LittleFS.remove(p);
        reloadStores();
        if (ok) StatusLog("File deleted: " + p);
        req->send(ok ? 200 : 500, "text/plain", ok ? "Deleted " + p : "Delete failed");
    });

    server.on("/fupload", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            reloadStores();
            req->redirect("/files");
        },
        [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0) {
                String dir = req->hasParam("dir") ? req->getParam("dir")->value() : "/";
                if (dir != "/seq") dir = "";
                int slash = filename.lastIndexOf('/');
                if (slash >= 0) filename = filename.substring(slash + 1);
                String path = dir + "/" + filename;
                if (!validPath(path) || filename.isEmpty()) return;
                req->_tempFile = LittleFS.open(path, "w");
                StatusLog("File upload: " + path);
            }
            if (req->_tempFile) {
                req->_tempFile.write(data, len);
                if (final) req->_tempFile.close();
            }
        });

    // Registered last: "/files" would also match "/files/view" and "/files/delete"
    server.on("/files", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", FILES_HTML);
    });
}
