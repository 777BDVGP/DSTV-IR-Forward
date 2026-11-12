// =============================================================================
// WifiSetup.cpp — WiFi from /WifiSettings.json (list of networks) + setup portal
// =============================================================================
// See WifiSetup.h for the file format and the boot / portal / reset rules.
// Derived from the TX/RX WifiSetup.cpp (single network + channel lock).
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "Config.h"
#include "Board.h"
#include "WifiSetup.h"
#include "OwnerPass.h"        // owner password: setup / WiFi changes / OTA / update

struct DefaultNet { const char *ssid, *pass, *ip, *gw, *sn, *dns; };
#ifndef WIFI_DEFAULT_NETS          // no src/Private.h: no built-in networks (public build)
#define WIFI_DEFAULT_NETS { { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr } }
#endif
static const DefaultNet kDefaultNets[] = WIFI_DEFAULT_NETS;

static bool HaveDefaults() {
	for (const DefaultNet &d : kDefaultNets) if (d.ssid && *d.ssid) return true;
	return false;
}

static String DefaultNames() {     // e.g. "HomeWiFi + Workshop"
	String s;
	for (const DefaultNet &d : kDefaultNets) {
		if (!d.ssid || !*d.ssid) continue;
		if (s.length()) s += " + ";
		s += d.ssid;
	}
	return s;
}

static fs::FS  *wfs   = nullptr;
static String   role  = "IN1";
static void   (*logFn)(const String &) = nullptr;

static WifiCfg  cfg;
static int      activeIdx = -1;
static String   apInfo;

static volatile bool rebootPending = false;   // set by a save, executed by the loop
static uint32_t      rebootAtMs    = 0;
static uint32_t      resetPressMs  = 0;

static void wlog(const String &s) { if (logFn) logFn(s); Serial.println("[WiFi] " + s); }

static String macSuffix() {
	uint8_t m[6];
	WiFi.macAddress(m);
	char b[5];
	snprintf(b, sizeof(b), "%02X%02X", m[4], m[5]);
	return String(b);
}

static String htmlEsc(const String &s) {
	String o;
	o.reserve(s.length() + 8);
	for (char c : s) {
		switch (c) {
			case '&':  o += F("&amp;");  break;
			case '<':  o += F("&lt;");   break;
			case '>':  o += F("&gt;");   break;
			case '"':  o += F("&quot;"); break;
			case '\'': o += F("&#39;");  break;
			default:   o += c;
		}
	}
	return o;
}

// =============================================================================
// File
// =============================================================================

static bool parseIp(JsonVariantConst v, IPAddress &out) {
	return v.is<const char *>() && out.fromString(v.as<const char *>());
}

static WifiCfg DefaultCfg() {
	WifiCfg c;
	c.host = WIFI_DEFAULT_HOST;
	for (const DefaultNet &d : kDefaultNets) {
		if (!d.ssid || !*d.ssid) continue;
		WifiNet n;
		n.ssid = d.ssid;
		n.pass = d.pass ? d.pass : "";
		n.dhcp = !(d.ip && *d.ip);              // ip "" = automatic
		if (!n.dhcp) {
			n.ip.fromString(d.ip);
			n.gw.fromString(d.gw);
			n.sn.fromString(d.sn);
			if (!(d.dns && n.dns.fromString(d.dns))) n.dns = n.gw;
		}
		c.nets.push_back(n);
	}
	return c;
}

static bool LoadCfg(WifiCfg &c) {
	if (!wfs) return false;
	File f = wfs->open(WIFI_SETTINGS_FILE, "r");
	if (!f) return false;
	DynamicJsonDocument doc(2048);
	DeserializationError err = deserializeJson(doc, f);
	f.close();
	if (err)                                       { wlog("WifiSettings.json unreadable"); return false; }
	if ((int)(doc["ver"] | 0) != WIFI_FILE_VERSION) { wlog("WifiSettings.json has no \"ver\":2 (old format)"); return false; }

	c = WifiCfg();
	c.host = doc["host"] | WIFI_DEFAULT_HOST;
	if (c.host.isEmpty()) c.host = WIFI_DEFAULT_HOST;
	for (JsonObjectConst o : doc["networks"].as<JsonArrayConst>()) {
		if (c.nets.size() >= WIFI_MAX_NETS) break;
		WifiNet n;
		n.ssid = o["ssid"] | "";
		n.pass = o["pass"] | "";
		n.dhcp = o["dhcp"] | true;
		if (n.ssid.isEmpty()) continue;
		if (!n.dhcp) {
			if (!parseIp(o["ip"], n.ip) || !parseIp(o["gw"], n.gw) || !parseIp(o["sn"], n.sn)) {
				wlog("WifiSettings.json: bad static IP for " + n.ssid + " - network skipped");
				continue;
			}
			if (!parseIp(o["dns"], n.dns)) n.dns = n.gw;
		}
		c.nets.push_back(n);
	}
	if (c.nets.empty()) { wlog("WifiSettings.json: no usable network"); return false; }
	return true;
}

static bool SaveCfg(const WifiCfg &c) {
	if (!wfs) return false;
	DynamicJsonDocument doc(2048);
	doc["ver"]  = WIFI_FILE_VERSION;
	doc["host"] = c.host;
	JsonArray arr = doc.createNestedArray("networks");
	for (const WifiNet &n : c.nets) {
		JsonObject o = arr.createNestedObject();
		o["ssid"] = n.ssid;
		o["pass"] = n.pass;
		o["dhcp"] = n.dhcp;
		if (!n.dhcp) {
			o["ip"]  = n.ip.toString();
			o["gw"]  = n.gw.toString();
			o["sn"]  = n.sn.toString();
			o["dns"] = n.dns.toString();
		}
	}
	File f = wfs->open(WIFI_SETTINGS_FILE, "w");
	if (!f) return false;
	bool ok = serializeJsonPretty(doc, f) > 0;
	f.close();
	return ok;
}

static bool WriteDefaults() {
	cfg = DefaultCfg();
	bool ok = SaveCfg(cfg);
	wlog(String(ok ? "Wrote" : "Could NOT write") + " default " WIFI_SETTINGS_FILE " (" +
	     String(cfg.nets.size()) + " networks)");
	return ok;
}

// ── Failed-boot counter (NVS: survives power loss) ──────────────────────────
static uint8_t GetFails() {
	Preferences p;
	uint8_t n = 0;
	if (p.begin("wifi", true)) { n = p.getUChar("fails", 0); p.end(); }
	return n;
}

static void SetFails(uint8_t n) {
	if (GetFails() == n) return;
	Preferences p;
	if (p.begin("wifi", false)) { p.putUChar("fails", n); p.end(); }
}

// =============================================================================
// Station connect
// =============================================================================

// Scan all channels; pick the listed network with the strongest AP in view.
static int PickNetwork(uint8_t bssid[6], uint8_t &channel, int32_t &rssi) {
	int n = WiFi.scanNetworks(false, false);
	int best = -1;
	for (int i = 0; i < n; i++) {
		for (size_t k = 0; k < cfg.nets.size(); k++) {
			if (WiFi.SSID(i) != cfg.nets[k].ssid) continue;
			if (best < 0 || WiFi.RSSI(i) > rssi) {
				best    = (int)k;
				rssi    = WiFi.RSSI(i);
				channel = (uint8_t)WiFi.channel(i);
				memcpy(bssid, WiFi.BSSID(i), 6);
			}
		}
	}
	WiFi.scanDelete();
	return best;
}

static bool Connect(uint32_t timeoutMs) {
	WiFi.mode(WIFI_STA);
	WiFi.setHostname(cfg.host.c_str());
	WiFi.setSleep(false);              // modem sleep adds latency to web / OTA
	WiFi.setAutoReconnect(true);

	uint8_t bssid[6];
	uint8_t channel = 0;
	int32_t rssi    = 0;
	activeIdx = PickNetwork(bssid, channel, rssi);
	if (activeIdx < 0) {
		String names;
		for (auto &n : cfg.nets) names += (names.length() ? ", " : "") + n.ssid;
		apInfo = "none of the WiFi networks (" + names + ") in range";
		wlog(apInfo);
		return false;
	}

	const WifiNet &n = cfg.nets[activeIdx];
	if (!n.dhcp) WiFi.config(n.ip, n.gw, n.sn, n.dns);
	apInfo = n.ssid;   // network name only — no AP MAC, works the same on any router
	// SSID only (no BSSID lock): after a drop the board may rejoin any AP of that WiFi
	WiFi.begin(n.ssid.c_str(), n.pass.length() ? n.pass.c_str() : nullptr);

	Serial.print("[WiFi] Connecting to " + n.ssid);
	uint32_t start = millis();
	while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
		delay(250);
		Serial.print('.');
	}
	Serial.println();
	return WiFi.status() == WL_CONNECTED;
}

// =============================================================================
// Settings form (portal and /wifi page)
// =============================================================================

static const char PAGE_HEAD[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1"><title>WiFi Setup</title>
<style>
body{font-family:sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:16px;font-size:.95em}
h2{color:#0284c7;margin:0 0 4px}h3{color:#38bdf8;margin:14px 0 6px;font-size:1em}p{color:#94a3b8;margin:4px 0 10px}a{color:#38bdf8}
table{width:100%;max-width:520px;border-collapse:collapse;background:#1e293b;border-radius:8px;overflow:hidden;margin-bottom:12px}
td{padding:7px 10px;border-bottom:1px solid #334155}tr:last-child td{border-bottom:none}
input,select{background:#0f172a;color:#e2e8f0;border:1px solid #334155;border-radius:4px;padding:6px;font-size:1em;width:100%;box-sizing:border-box}
input[type=checkbox]{width:18px;height:18px;accent-color:#0284c7}
button{background:#0284c7;color:#fff;border:none;border-radius:4px;padding:9px 16px;font-size:1em;cursor:pointer;margin:4px 6px 4px 0}
button.red{background:#b91c1c}.net{cursor:pointer}.net:hover{background:#334155}
.nav{display:flex;gap:6px;margin-bottom:12px;flex-wrap:wrap}.nav a{color:#e2e8f0;text-decoration:none;background:#1e293b;padding:6px 12px;border-radius:6px}
.nav a.act{background:#0284c7}
</style></head><body>
)HTML";

static const char FORM_JS[] PROGMEM = R"HTML(<script>
var cur=-1;
function first(){for(var i=0;i<4;i++){var e=document.getElementById('s'+i+'_ssid');if(e&&!e.value)return i;}return 0;}
function pick(s){var i=cur>=0?cur:first();document.getElementById('s'+i+'_ssid').value=s;document.getElementById('s'+i+'_pass').focus();}
function dh(i){var d=document.getElementById('s'+i+'_dhcp').checked;document.querySelectorAll('.st'+i).forEach(function(r){r.style.display=d?'none':'';});}
for(var i=0;i<4;i++){if(document.getElementById('s'+i+'_dhcp'))dh(i);
(function(k){var e=document.getElementById('s'+k+'_ssid');if(e)e.addEventListener('focus',function(){cur=k;});})(i);}
</script></body></html>)HTML";
static_assert(WIFI_MAX_NETS == 4, "FORM_JS loops over 4 network slots");

// Scan results for the portal (STA scan done before the AP starts / on rescan)
struct ScanNet { String ssid; int32_t rssi; uint8_t ch; bool open; };
static ScanNet scanNets[20];
static int     scanCount = 0;

static void DoScan() {
	int n = WiFi.scanNetworks(false, true);
	scanCount = 0;
	for (int i = 0; i < n && scanCount < 20; i++) {
		String s = WiFi.SSID(i);
		if (s.isEmpty()) continue;
		bool dup = false;                       // keep the strongest per SSID
		for (int k = 0; k < scanCount; k++) {
			if (scanNets[k].ssid == s) {
				dup = true;
				if (WiFi.RSSI(i) > scanNets[k].rssi) { scanNets[k].rssi = WiFi.RSSI(i); scanNets[k].ch = WiFi.channel(i); }
				break;
			}
		}
		if (dup) continue;
		scanNets[scanCount++] = { s, WiFi.RSSI(i), (uint8_t)WiFi.channel(i),
		                          WiFi.encryptionType(i) == WIFI_AUTH_OPEN };
	}
	WiFi.scanDelete();
	// strongest first
	for (int a = 0; a < scanCount; a++)
		for (int b = a + 1; b < scanCount; b++)
			if (scanNets[b].rssi > scanNets[a].rssi) { ScanNet t = scanNets[a]; scanNets[a] = scanNets[b]; scanNets[b] = t; }
}

static void AddInput(String &h, const String &id, const char *label, const String &value,
                     const char *rowClass, const char *extra) {
	h += F("<tr");
	if (rowClass) { h += F(" class='"); h += rowClass; h += '\''; }
	h += F("><td>");
	h += label;
	h += F("</td><td><input id='");
	h += id;
	h += F("' name='");
	h += id;
	h += F("' value='");
	h += htmlEsc(value);
	h += '\'';
	h += extra;
	h += F("></td></tr>");
}

static void AddNetSlot(String &h, int i, const WifiNet *n) {
	String p   = "s" + String(i) + "_";
	String st  = "st" + String(i);
	bool   dhcp = n ? n->dhcp : true;
	h += F("<h3>Network ");
	h += i + 1;
	if (activeIdx == i && WiFi.status() == WL_CONNECTED) h += F(" &nbsp;(connected)");
	h += F("</h3><table>");
	AddInput(h, p + "ssid", "WiFi name (SSID)", n ? n->ssid : String(), nullptr,
	         i == 0 ? " maxlength='32' placeholder='WiFi name' required" : " maxlength='32' placeholder='empty = not used'");
	AddInput(h, p + "pass", "Password", String(), nullptr,
	         n ? " type='password' maxlength='63' placeholder='unchanged'" : " type='password' maxlength='63' placeholder='WiFi password'");
	h += F("<tr><td>Get IP from router (DHCP)</td><td><input id='");
	h += p;
	h += F("dhcp' name='");
	h += p;
	h += F("dhcp' type='checkbox' value='1' onchange='dh(");
	h += i;
	h += F(")'");
	if (dhcp) h += F(" checked");
	h += F("></td></tr>");
	bool st_ = n && !n->dhcp;
	AddInput(h, p + "ip",  "IP address",       st_ ? n->ip.toString()  : String(), st.c_str(), "");
	AddInput(h, p + "gw",  "Gateway (router)", st_ ? n->gw.toString()  : String(), st.c_str(), "");
	AddInput(h, p + "sn",  "Subnet mask",      st_ ? n->sn.toString()  : String("255.255.255.0"), st.c_str(), "");
	AddInput(h, p + "dns", "DNS",              st_ ? n->dns.toString() : String(), st.c_str(), " placeholder='= gateway'");
	h += F("</table>");
}

// ── Owner password on the settings forms (as TX/RX WifiSetup.cpp) ───────────
static String OwnerFormRows() {
	if (OwnerPassSet())
		return F("<tr><td><b>Owner password</b></td><td><input type='password' name='owner' required "
		         "autocomplete='current-password' placeholder='needed to save'></td></tr>");
	return F("<tr><td colspan='2'><b>Choose an owner password</b> (6 or more characters). You need it to "
	         "change these settings later and to update the firmware. Keep it safe.</td></tr>"
	         "<tr><td>Owner password</td><td><input type='password' name='ownernew' required minlength='6' "
	         "maxlength='63' autocomplete='new-password'></td></tr>"
	         "<tr><td>Repeat it</td><td><input type='password' name='ownerrep' required "
	         "autocomplete='new-password'></td></tr>");
}

static String RawArg(AsyncWebServerRequest *req, const char *name) {   // not trimmed
	return req->hasParam(name, true) ? req->getParam(name, true)->value() : String();
}

// "" = ok. newPw: the password chosen on a new board (store it after saving).
static String OwnerCheck(AsyncWebServerRequest *req, String &newPw) {
	newPw = "";
	if (OwnerPassSet()) {
		if (OwnerPassOk(RawArg(req, "owner"))) return "";
		wlog("WiFi settings change REFUSED: wrong owner password");
		return "Owner password is wrong - nothing was changed";
	}
	String a = RawArg(req, "ownernew"), b = RawArg(req, "ownerrep");
	if (a.length() < OWNER_PW_MIN || a.length() > OWNER_PW_MAX) return "Choose an owner password of 6 to 63 characters";
	if (a != b) return "The two owner passwords are not the same";
	newPw = a;
	return "";
}

static String BuildForm(bool portal) {
	String h;
	h.reserve(9000);
	h += FPSTR(PAGE_HEAD);
	if (portal) {
		h += F("<h2>TV IR IN ONE - WiFi Setup</h2><p>None of the saved WiFi networks could be joined. "
		       "Check or add a network; on boot the board joins whichever listed network it can see.</p>");
		h += F("<table><tr><td><b>Networks found</b> (tap to put it in the selected slot)</td><td style='text-align:right'><a href='/rescan'>Rescan</a></td></tr>");
		if (scanCount == 0) h += F("<tr><td colspan=2>No networks found - press Rescan</td></tr>");
		for (int i = 0; i < scanCount; i++) {
			h += F("<tr class='net' onclick=\"pick('");
			String js = scanNets[i].ssid;
			js.replace("\\", "\\\\");
			js.replace("'", "\\'");
			h += htmlEsc(js);
			h += F("')\"><td>");
			h += htmlEsc(scanNets[i].ssid);
			if (scanNets[i].open) h += F(" (open)");
			h += F("</td><td style='text-align:right'>ch ");
			h += scanNets[i].ch;
			h += F(" &nbsp; ");
			h += scanNets[i].rssi;
			h += F(" dBm</td></tr>");
		}
		h += F("</table><form method='POST' action='/save'>");
	} else {
		h += F("<div class='nav'><a href='/'>STATUS</a><a href='/sequences'>IR SEQUENCES</a>"
		       "<a href='/settings'>SETTINGS</a><a class='act'>WIFI</a><a href='/files'>FILES</a><a href='/about'>ABOUT</a></div>");
		h += F("<h2>WiFi Settings</h2><p>Connected to <b>");
		h += htmlEsc(WiFi.SSID());
		h += F("</b>, IP ");
		h += WiFi.localIP().toString();
		h += F(", channel ");
		h += WiFi.channel();
		h += F(", ");
		h += WiFi.RSSI();
		h += F(" dBm.</p><p>On boot the board joins whichever network below it can see (both in range: the stronger one). "
		       "Saving reboots the board. If no network can be joined the board opens its setup WiFi (IR-FORWARD-");
		h += role;
		h += '-';
		h += macSuffix();
		h += F(") after 3 failed starts.</p><form method='POST' action='/wifi/save'>");
	}

	h += F("<table>");
	AddInput(h, "host", "Host name (both networks)", cfg.host, nullptr, " maxlength='31'");
	h += F("</table>");
	for (int i = 0; i < WIFI_MAX_NETS; i++) {
		AddNetSlot(h, i, i < (int)cfg.nets.size() ? &cfg.nets[i] : nullptr);
	}
	h += F("<table>");
	h += OwnerFormRows();
	h += F("</table><button type='submit'>SAVE &amp; REBOOT</button></form>");

	if (!portal && HaveDefaults()) {
		h += F("<form method='POST' action='/wifi/defaults' onsubmit=\"return confirm('Replace the WiFi list with the defaults (");
		h += htmlEsc(DefaultNames());
		h += F(") and reboot?')\">");
		if (OwnerPassSet()) h += F("<input type='password' name='owner' placeholder='Owner password' required style='max-width:220px'> ");
		h += F("<button class='red' type='submit'>RESTORE DEFAULT NETWORKS</button></form>");
	}
	h += FPSTR(FORM_JS);
	return h;
}

static String Arg(AsyncWebServerRequest *req, const String &name) {
	if (!req->hasParam(name, true)) return String();
	String v = req->getParam(name, true)->value();
	v.trim();
	return v;
}

static bool ValidHost(const String &h) {
	if (h.isEmpty() || h.length() > 31 || h[0] == '-' || h[h.length() - 1] == '-') return false;
	for (char ch : h) if (!isalnum((unsigned char)ch) && ch != '-') return false;
	return true;
}

// Builds a new config from the form. Empty password = keep the saved one of that SSID.
static bool FormToCfg(AsyncWebServerRequest *req, WifiCfg &n, String &err) {
	n = WifiCfg();
	n.host = Arg(req, "host");
	if (!ValidHost(n.host)) { err = "Host name: letters, digits and '-' only (max 31)"; return false; }

	for (int i = 0; i < WIFI_MAX_NETS; i++) {
		String p = "s" + String(i) + "_";
		WifiNet w;
		w.ssid = Arg(req, p + "ssid");
		if (w.ssid.isEmpty()) continue;                       // slot not used
		String who = "Network " + String(i + 1) + " (" + w.ssid + "): ";
		if (w.ssid.length() > 32) { err = who + "SSID too long"; return false; }
		for (auto &o : n.nets) if (o.ssid == w.ssid) { err = who + "listed twice"; return false; }

		w.pass = req->hasParam(p + "pass", true) ? req->getParam(p + "pass", true)->value() : String();
		if (w.pass.isEmpty())
			for (auto &old : cfg.nets) if (old.ssid == w.ssid) w.pass = old.pass;
		if (w.pass.length() > 0 && w.pass.length() < 8) { err = who + "password must be at least 8 characters (or empty for an open WiFi)"; return false; }

		w.dhcp = req->hasParam(p + "dhcp", true);
		if (!w.dhcp) {
			if (!w.ip.fromString(Arg(req, p + "ip")))  { err = who + "bad IP address"; return false; }
			if (!w.gw.fromString(Arg(req, p + "gw")))  { err = who + "bad gateway"; return false; }
			if (!w.sn.fromString(Arg(req, p + "sn")))  { err = who + "bad subnet mask"; return false; }
			if (!w.dns.fromString(Arg(req, p + "dns"))) w.dns = w.gw;
		}
		n.nets.push_back(w);
	}
	if (n.nets.empty()) { err = "Enter at least one WiFi network"; return false; }
	return true;
}

static String ResultPage(bool ok, const String &msg, const WifiCfg *n) {
	String h = FPSTR(PAGE_HEAD);
	if (!ok) {
		h += F("<h2>Not saved</h2><p>");
		h += htmlEsc(msg);
		h += F("</p><button onclick='history.back()'>BACK</button></body></html>");
		return h;
	}
	h += F("<h2>Saved - rebooting</h2><p>The board now joins whichever of these it can see:</p><table>");
	for (auto &w : n->nets) {
		h += F("<tr><td><b>");
		h += htmlEsc(w.ssid);
		h += F("</b></td><td>");
		if (w.dhcp) { h += F("DHCP - http://"); h += htmlEsc(n->host); h += F(".local"); }
		else        { h += F("http://"); h += w.ip.toString(); }
		h += F("</td></tr>");
	}
	h += F("</table><p>If no network can be joined the board opens its setup WiFi again after 3 tries (about 2 minutes).</p></body></html>");
	return h;
}

static void ScheduleReboot() {
	rebootAtMs    = millis() + 1500;
	rebootPending = true;
}

// =============================================================================
// Setup portal (never returns)
// =============================================================================

static void RunPortal(uint32_t timeoutMs) {
	static volatile bool rescanReq = false;

	WiFi.mode(WIFI_STA);
	WiFi.disconnect();
	delay(100);
	DoScan();

	String apName = "IR-FORWARD-" + role + "-" + macSuffix();
	WiFi.mode(WIFI_AP_STA);
	WiFi.softAP(apName.c_str());   // open: the owner password protects saving, not the WiFi
	delay(200);
	IPAddress apIp = WiFi.softAPIP();

	DNSServer dns;
	dns.start(53, "*", apIp);

	AsyncWebServer *portal = new AsyncWebServer(80);
	portal->on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
		req->send(200, "text/html", BuildForm(true));
	});
	portal->on("/rescan", HTTP_GET, [](AsyncWebServerRequest *req) {
		rescanReq = true;   // scan in the portal loop, not in the web task
		req->send(200, "text/html", String(FPSTR(PAGE_HEAD)) +
		          F("<h2>Scanning...</h2><p>Please wait.</p><script>setTimeout(function(){location.href='/';},6000);</script></body></html>"));
	});
	portal->on("/save", HTTP_POST, [](AsyncWebServerRequest *req) {
		WifiCfg n;
		String  err, newPw;
		if (!FormToCfg(req, n, err)) { req->send(200, "text/html", ResultPage(false, err, nullptr)); return; }
		if ((err = OwnerCheck(req, newPw)).length()) { req->send(200, "text/html", ResultPage(false, err, nullptr)); return; }
		if (!SaveCfg(n))             { req->send(200, "text/html", ResultPage(false, "Could not write the settings file", nullptr)); return; }
		if (newPw.length()) { OwnerPassStore(newPw); wlog("Owner password set"); }
		SetFails(0);
		wlog("WiFi settings saved from setup portal (" + String(n.nets.size()) + " networks)");
		req->send(200, "text/html", ResultPage(true, "", &n));
		ScheduleReboot();
	});
	portal->onNotFound([apIp](AsyncWebServerRequest *req) {   // captive portal: everything → setup page
		req->redirect("http://" + apIp.toString() + "/");
	});
	portal->begin();

	wlog("Setup portal: join WiFi '" + apName + "' (open), open http://" + apIp.toString() +
	     (timeoutMs ? "  - retry saved WiFi in 5 min" : ""));

	uint32_t start = millis(), lastLed = 0;
	bool     red   = false;
	for (;;) {
		dns.processNextRequest();
		if (rescanReq) { rescanReq = false; DoScan(); }
		if (millis() - lastLed >= 500) {        // alternate red/green = setup mode
			lastLed = millis();
			red = !red;
			if (red) BoardFlashRed(); else BoardFlashGreen();
		}
		if (rebootPending && (int32_t)(millis() - rebootAtMs) >= 0) { delay(100); ESP.restart(); }
		if (timeoutMs && WiFi.softAPgetStationNum() == 0 && millis() - start >= timeoutMs) {
			wlog("Setup portal timeout - retrying the saved WiFi");
			delay(200);
			ESP.restart();
		}
		delay(10);
	}
}

// =============================================================================
// Public
// =============================================================================

void WifiSetupInit(fs::FS &fs, const char *r, void (*log)(const String &)) {
	wfs   = &fs;
	role  = r;
	logFn = log;
	pinMode(WIFI_RESET_PIN, INPUT_PULLUP);
}

bool WifiStart(uint32_t connectTimeoutMs) {
	if (OwnerPassSeed()) wlog("Owner password set from Private.h");
	if (!LoadCfg(cfg)) {
		if (HaveDefaults()) {
			WriteDefaults();
		} else {                              // new board: owner enters the WiFi
			wlog("No WiFi settings yet - starting setup portal");
			SetFails(0);
			RunPortal(0);                     // until saved (then reboot)
		}
	}

	uint8_t fails = GetFails();
	if (fails >= WIFI_MAX_FAILED_BOOTS) {
		wlog("WiFi failed " + String(fails) + " boots in a row - setup portal for 5 min");
		SetFails(0);                          // after the timeout: 3 fresh tries
		RunPortal(WIFI_PORTAL_TIMEOUT_MS);
	}

	if (Connect(connectTimeoutMs)) {
		SetFails(0);
		if (MDNS.begin(cfg.host.c_str())) MDNS.addService("http", "tcp", 80);
		return true;
	}
	SetFails(fails + 1);
	wlog("WiFi connect failed (" + String(fails + 1) + "/" + String(WIFI_MAX_FAILED_BOOTS) + ")" +
	     (activeIdx >= 0 ? " - " + cfg.nets[activeIdx].ssid : String()));
	return false;
}

const WifiCfg &WifiConfig()    { return cfg; }
const WifiNet *WifiActiveNet() { return activeIdx >= 0 ? &cfg.nets[activeIdx] : nullptr; }
String         WifiApInfo()    { return apInfo; }

void WifiSetupLoop() {
	if (rebootPending && (int32_t)(millis() - rebootAtMs) >= 0) {
		delay(100);
		ESP.restart();
	}

	if (digitalRead(WIFI_RESET_PIN) == LOW) {
		if (resetPressMs == 0) resetPressMs = millis() | 1;
		else if (millis() - resetPressMs >= WIFI_RESET_HOLD_MS) {
			if (HaveDefaults()) {
				wlog("WiFi reset (button 0 held 5 s) - default networks restored, rebooting");
				WriteDefaults();
			} else {
				wlog("WiFi reset (button 0 held 5 s) - settings deleted, rebooting into setup portal");
				if (wfs) wfs->remove(WIFI_SETTINGS_FILE);
			}
			SetFails(0);
			delay(300);
			ESP.restart();
		}
	} else {
		resetPressMs = 0;
	}
}

void WifiAddRoutes(AsyncWebServer &server) {
	server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *req) {
		req->send(200, "text/html", BuildForm(false));
	});
	server.on("/wifi/save", HTTP_POST, [](AsyncWebServerRequest *req) {
		WifiCfg n;
		String  err, newPw;
		if (!FormToCfg(req, n, err)) { req->send(200, "text/html", ResultPage(false, err, nullptr)); return; }
		if ((err = OwnerCheck(req, newPw)).length()) { req->send(200, "text/html", ResultPage(false, err, nullptr)); return; }
		if (!SaveCfg(n))             { req->send(200, "text/html", ResultPage(false, "Could not write the settings file", nullptr)); return; }
		if (newPw.length()) { OwnerPassStore(newPw); wlog("Owner password set"); }
		SetFails(0);
		wlog("WiFi settings changed on /wifi (" + String(n.nets.size()) + " networks) - rebooting");
		req->send(200, "text/html", ResultPage(true, "", &n));
		ScheduleReboot();
	});
	server.on("/wifi/defaults", HTTP_POST, [](AsyncWebServerRequest *req) {
		if (!HaveDefaults()) { req->send(200, "text/html", ResultPage(false, "This firmware has no default networks", nullptr)); return; }
		if (OwnerPassSet() && !OwnerPassOk(RawArg(req, "owner"))) {
			wlog("Restore default networks REFUSED: wrong owner password");
			req->send(200, "text/html", ResultPage(false, "Owner password is wrong - nothing was changed", nullptr));
			return;
		}
		wlog("WiFi list reset to the defaults on /wifi - rebooting");
		WriteDefaults();
		SetFails(0);
		req->send(200, "text/html", ResultPage(true, "", &cfg));
		ScheduleReboot();
	});
}
