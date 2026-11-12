// =============================================================================
// WifiSetup.cpp — WiFi from /WifiSettings.json + setup portal
// =============================================================================
// IDENTICAL copy in TV_IR_Sending/src and TV_IR_Receiving/src — keep in sync.
// See WifiSetup.h for the file format and the boot / portal / reset rules.
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "Board.h"
#include "WifiSetup.h"
#include "OwnerPass.h"        // owner password: setup / WiFi changes / OTA
#include "OmadaAps.h"      // AP names for /wifiscan
#include <vector>
#include <algorithm>

static fs::FS  *wfs   = nullptr;
static String   role  = "TX";
static void   (*logFn)(const String &) = nullptr;

static WifiCfg  cfg;
static bool     cfgValid = false;
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

static String defaultHost() {
	String h = "ir-" + role + "-" + macSuffix();
	h.toLowerCase();
	return h;
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

static bool LoadCfg(WifiCfg &c) {
	if (!wfs) return false;
	File f = wfs->open(WIFI_SETTINGS_FILE, "r");
	if (!f) return false;
	StaticJsonDocument<768> doc;
	DeserializationError err = deserializeJson(doc, f);
	f.close();
	if (err)                      { wlog("WifiSettings.json unreadable - ignored"); return false; }
	if ((int)(doc["ver"] | 0) != 1) { wlog("WifiSettings.json has no \"ver\":1 (old format) - ignored"); return false; }

	c.ssid    = doc["ssid"] | "";
	c.pass    = doc["pass"] | "";
	c.host    = doc["host"] | "";
	c.channel = (uint8_t)(doc["channel"] | 0);
	c.dhcp    = doc["dhcp"] | true;
	if (c.ssid.isEmpty() || (c.channel > 13 && c.channel != WIFI_CH_FOLLOW_RX)) { wlog("WifiSettings.json: no SSID / bad channel - ignored"); return false; }
	if (c.host.isEmpty()) c.host = defaultHost();
	if (!c.dhcp) {
		if (!parseIp(doc["ip"], c.ip) || !parseIp(doc["gw"], c.gw) || !parseIp(doc["sn"], c.sn)) {
			wlog("WifiSettings.json: bad static IP - ignored");
			return false;
		}
		if (!parseIp(doc["dns"], c.dns)) c.dns = c.gw;
	}
	return true;
}

static bool SaveCfg(const WifiCfg &c) {
	if (!wfs) return false;
	StaticJsonDocument<768> doc;
	doc["ver"]     = 1;
	doc["ssid"]    = c.ssid;
	doc["pass"]    = c.pass;
	doc["channel"] = c.channel;
	doc["dhcp"]    = c.dhcp;
	if (!c.dhcp) {
		doc["ip"]  = c.ip.toString();
		doc["gw"]  = c.gw.toString();
		doc["sn"]  = c.sn.toString();
		doc["dns"] = c.dns.toString();
	}
	doc["host"]    = c.host;
	File f = wfs->open(WIFI_SETTINGS_FILE, "w");
	if (!f) return false;
	bool ok = serializeJson(doc, f) > 0;
	f.close();
	return ok;
}

static void ForgetCfg() {
	if (wfs) wfs->remove(WIFI_SETTINGS_FILE);
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

// ── "Follow receiver": remembered RX channel (NVS, survives restarts) ────────
static bool searchMode = false;   // TX is hopping channels looking for the RX

static uint8_t FollowChGet() {
	Preferences p;
	uint8_t ch = 0;
	if (p.begin("wifi", true)) { ch = p.getUChar("rxch", 0); p.end(); }
	return ch <= 13 ? ch : 0;
}

static void FollowChSet(uint8_t ch) {
	if (ch > 13 || FollowChGet() == ch) return;
	Preferences p;
	if (p.begin("wifi", false)) { p.putUChar("rxch", ch); p.end(); }
}

// The channel actually locked to: 0 (any), 1-13, or the remembered RX channel
static uint8_t LockChannel(const WifiCfg &c) {
	return c.channel == WIFI_CH_FOLLOW_RX ? FollowChGet() : c.channel;
}

static String LockText(const WifiCfg &c) {
	if (c.channel == WIFI_CH_FOLLOW_RX) {
		uint8_t ch = FollowChGet();
		return ch ? "follow receiver (ch " + String(ch) + ")" : String("follow receiver (not found yet)");
	}
	return c.channel ? String(c.channel) : String("any");
}

// Strongest AP of the SSID on the locked channel → its BSSID.
// Up to 3 scans of 500 ms: one short scan can miss a weak AP (a TX at -67 dBm
// once missed it and joined the SSID on another channel, cutting ESP-NOW).
static bool FindApOnChannel(const WifiCfg &cfgIn, uint8_t bssid[6], int32_t &rssi) {
	WifiCfg c = cfgIn;
	c.channel = LockChannel(cfgIn);
	if (!c.channel) return false;
	bool found = false;
	for (int attempt = 0; attempt < 3 && !found; attempt++) {
		int n = WiFi.scanNetworks(false, false, false, 500, c.channel);
		for (int i = 0; i < n; i++) {
			if (WiFi.SSID(i) != c.ssid || WiFi.channel(i) != c.channel) continue;
			if (!found || WiFi.RSSI(i) > rssi) {
				memcpy(bssid, WiFi.BSSID(i), 6);
				rssi  = WiFi.RSSI(i);
				found = true;
			}
		}
		WiFi.scanDelete();
	}
	return found;
}

static bool Connect(const WifiCfg &c, uint32_t timeoutMs) {
	WiFi.setHostname(c.host.c_str());
	WiFi.mode(WIFI_STA);
	WiFi.setSleep(false);              // modem sleep would drop ESP-NOW packets
	WiFi.setAutoReconnect(true);
	if (!c.dhcp) WiFi.config(c.ip, c.gw, c.sn, c.dns);

	const char *pass = c.pass.length() ? c.pass.c_str() : nullptr;
	uint8_t lockCh = LockChannel(c);
	if (lockCh) {
		uint8_t bssid[6];
		int32_t rssi = 0;
		if (FindApOnChannel(c, bssid, rssi)) {
			char buf[48];
			snprintf(buf, sizeof(buf), "AP %02X:%02X:%02X:%02X:%02X:%02X (scan %ld dBm)",
			         bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], (long)rssi);
			apInfo = buf;
			WiFi.begin(c.ssid.c_str(), pass, lockCh, bssid);
		} else {
			apInfo = "WARNING: no " + c.ssid + " AP on channel " + String(lockCh) +
			         " - ESP-NOW may not reach the other boards";
			WiFi.begin(c.ssid.c_str(), pass);   // keep web/OTA reachable
		}
	} else {
		apInfo = c.channel == WIFI_CH_FOLLOW_RX ? "(any channel - receiver channel not known yet)" : "(any channel)";
		WiFi.begin(c.ssid.c_str(), pass);
	}

	Serial.print("[WiFi] Connecting to " + c.ssid);
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
h2{color:#0284c7;margin:0 0 4px}p{color:#94a3b8;margin:4px 0 10px}a{color:#38bdf8}
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
function pick(s,c){document.getElementById('ssid').value=s;document.getElementById('channel').value=c;document.getElementById('pass').focus();}
function dh(){var d=document.getElementById('dhcp').checked;document.querySelectorAll('.st').forEach(function(r){r.style.display=d?'none':'';});}
dh();
</script></body></html>)HTML";

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
		bool dup = false;                       // keep the strongest per SSID+channel
		for (int k = 0; k < scanCount; k++) {
			if (scanNets[k].ssid == s && scanNets[k].ch == WiFi.channel(i)) { dup = true; break; }
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

static String ipVal(bool have, const IPAddress &ip, const char *def) {
	return have ? ip.toString() : String(def);
}

// ── Owner password on the settings forms ─────────────────────────────────────
// New board: the owner chooses it here. Afterwards it is asked before any
// WiFi change, so someone who joins the (open) setup WiFi cannot change it.
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
	const WifiCfg &c = cfg;
	bool have = cfgValid;
	String h;
	h.reserve(5000);
	h += FPSTR(PAGE_HEAD);
	if (portal) {
		h += F("<h2>IR Forward ");
		h += role;
		h += F(" - WiFi Setup</h2><p>Choose the home WiFi this board must join. All boards of one system (RX and every TX) must join the same WiFi on the same channel.</p>");
		h += F("<table><tr><td><b>Networks found</b> (tap to select)</td><td style='text-align:right'><a href='/rescan'>Rescan</a></td></tr>");
		if (scanCount == 0) h += F("<tr><td colspan=2>No networks found - press Rescan</td></tr>");
		for (int i = 0; i < scanCount; i++) {
			h += F("<tr class='net' onclick=\"pick('");
			String js = scanNets[i].ssid;
			js.replace("\\", "\\\\");
			js.replace("'", "\\'");
			h += htmlEsc(js);
			h += F("',");
			h += scanNets[i].ch;
			h += F(")\"><td>");
			h += htmlEsc(scanNets[i].ssid);
			if (scanNets[i].open) h += F(" (open)");
			h += F("</td><td style='text-align:right'>ch ");
			h += scanNets[i].ch;
			h += F(" &nbsp; ");
			h += scanNets[i].rssi;
			h += F(" dBm</td></tr>");
		}
		h += F("</table>");
		h += F("<form method='POST' action='/save'>");
	} else {
		h += F("<div class='nav'><a href='/'>STATUS</a>");
		if (role == "RX") h += F("<a href='/sequences'>IR SEQUENCES</a><a href='/schedule'>SCHEDULE</a>");
		else              h += F("<a href='/recseq'>IR SEQUENCES</a>");
		h += F("<a href='/settings'>SETTINGS</a><a class='act'>WIFI</a><a href='/files'>FILES</a><a href='/about'>ABOUT</a></div>");
		h += F("<h2>WiFi Settings</h2><p>Connected to <b>");
		h += htmlEsc(WiFi.SSID());
		h += F("</b>, IP ");
		h += WiFi.localIP().toString();
		h += F(", channel ");
		h += WiFi.channel();
		h += F(" (lock: ");
		h += LockText(cfg);
		h += ')';
		h += F(", ");
		h += WiFi.RSSI();
		h += F(" dBm. Saving reboots the board. A wrong setting makes the board open its setup WiFi (IR-FORWARD-");
		h += role;
		h += '-';
		h += macSuffix();
		h += F(") after 3 failed starts.</p>");
		h += F("<p><a href='/wifiscan'>Which access points does this board hear?</a></p>");
		h += F("<form method='POST' action='/wifi/save'>");
	}

	h += F("<table>");
	h += F("<tr><td>WiFi name (SSID)</td><td><input id='ssid' name='ssid' maxlength='32' required value='");
	h += htmlEsc(have ? c.ssid : String());
	h += F("'></td></tr><tr><td>Password</td><td><input id='pass' name='pass' type='password' maxlength='63' placeholder='");
	h += have ? F("unchanged") : F("WiFi password");
	h += F("'></td></tr><tr><td>Channel lock</td><td><select id='channel' name='channel'><option value='0'>Any channel</option>");
	if (role == "TX") {
		h += F("<option value='99'");
		if (!have || c.channel == WIFI_CH_FOLLOW_RX) h += F(" selected");
		h += F(">Follow receiver</option>");
	}
	for (int ch = 1; ch <= 13; ch++) {
		h += F("<option value='");
		h += ch;
		h += '\'';
		if (have && c.channel == ch) h += F(" selected");
		h += '>';
		h += ch;
		h += F("</option>");
	}
	h += F("</select>");
	h += role == "TX"
	   ? F("<br><small>Follow receiver: this sender finds the receiver's channel by itself (recommended). "
	       "Any: one router. A number: the same on every board.</small>")
	   : F("<br><small>Any: one router or a mesh on one channel. Several access points on different "
	       "channels: pick one (see Which access points does this board hear?).</small>");
	h += F("</td></tr><tr><td>Get IP from router (DHCP)</td><td><input id='dhcp' name='dhcp' type='checkbox' value='1' onchange='dh()'");
	if (!have || c.dhcp) h += F(" checked");
	h += F("></td></tr>");
	h += F("<tr class='st'><td>IP address</td><td><input name='ip' value='");
	h += ipVal(have && !c.dhcp, c.ip, "192.168.1.100");
	h += F("'></td></tr><tr class='st'><td>Gateway (router)</td><td><input name='gw' value='");
	h += ipVal(have && !c.dhcp, c.gw, "192.168.1.1");
	h += F("'></td></tr><tr class='st'><td>Subnet mask</td><td><input name='sn' value='");
	h += ipVal(have && !c.dhcp, c.sn, "255.255.255.0");
	h += F("'></td></tr><tr class='st'><td>DNS</td><td><input name='dns' value='");
	h += ipVal(have && !c.dhcp, c.dns, "192.168.1.1");
	h += F("'></td></tr><tr><td>Host name</td><td><input name='host' maxlength='31' value='");
	h += htmlEsc(have ? c.host : defaultHost());
	h += F("'></td></tr>");
	h += OwnerFormRows();
	h += F("</table><button type='submit'>SAVE &amp; REBOOT</button></form>");

	if (!portal) {
		h += F("<form method='POST' action='/wifi/forget' onsubmit=\"return confirm('Delete the WiFi settings and reboot into the setup WiFi?')\">");
		if (OwnerPassSet()) h += F("<input type='password' name='owner' placeholder='Owner password' required style='max-width:220px'> ");
		h += F("<button class='red' type='submit'>FORGET WIFI</button></form>");
	}
	h += FPSTR(FORM_JS);
	return h;
}

static String Arg(AsyncWebServerRequest *req, const char *name) {
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

// Builds a new config from the form. keepPass: empty password = keep the old one.
static bool FormToCfg(AsyncWebServerRequest *req, bool keepPass, WifiCfg &n, String &err) {
	n.ssid = Arg(req, "ssid");
	n.pass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : String();
	if (keepPass && n.pass.isEmpty() && cfgValid && n.ssid == cfg.ssid) n.pass = cfg.pass;
	n.channel = (uint8_t)Arg(req, "channel").toInt();
	n.dhcp    = req->hasParam("dhcp", true);
	n.host    = Arg(req, "host");

	if (n.ssid.isEmpty() || n.ssid.length() > 32) { err = "WiFi name (SSID) is required"; return false; }
	if (n.pass.length() > 0 && n.pass.length() < 8) { err = "Password must be at least 8 characters (or empty for an open WiFi)"; return false; }
	if (n.channel > 13 && !(n.channel == WIFI_CH_FOLLOW_RX && role == "TX")) { err = "Bad channel"; return false; }
	if (!ValidHost(n.host))                         { err = "Host name: letters, digits and '-' only (max 31)"; return false; }
	if (!n.dhcp) {
		if (!n.ip.fromString(Arg(req, "ip")))  { err = "Bad IP address"; return false; }
		if (!n.gw.fromString(Arg(req, "gw")))  { err = "Bad gateway"; return false; }
		if (!n.sn.fromString(Arg(req, "sn")))  { err = "Bad subnet mask"; return false; }
		if (!n.dns.fromString(Arg(req, "dns"))) n.dns = n.gw;
	}
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
	h += F("<h2>Saved - rebooting</h2><p>The board now joins <b>");
	h += htmlEsc(n->ssid);
	h += F("</b>.</p><p>Connect your phone/PC to that WiFi again, then open <b>");
	if (n->dhcp) {
		h += F("http://");
		h += htmlEsc(n->host);
		h += F(".local</b> (or look up the IP in your router).");
	} else {
		h += F("http://");
		h += n->ip.toString();
		h += F("</b>.");
	}
	h += F("</p><p>If the board cannot connect it opens its setup WiFi again after 3 tries (about 2 minutes).</p></body></html>");
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
		if (!FormToCfg(req, true, n, err))  { req->send(200, "text/html", ResultPage(false, err, nullptr)); return; }
		if ((err = OwnerCheck(req, newPw)).length()) { req->send(200, "text/html", ResultPage(false, err, nullptr)); return; }
		if (!SaveCfg(n))                    { req->send(200, "text/html", ResultPage(false, "Could not write the settings file", nullptr)); return; }
		if (newPw.length()) { OwnerPassStore(newPw); wlog("Owner password set"); }
		SetFails(0);
		wlog("WiFi settings saved from setup portal: " + n.ssid);
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
	cfgValid = LoadCfg(cfg);
	if (!cfgValid) {
		wlog("No valid " WIFI_SETTINGS_FILE " - starting setup portal");
		SetFails(0);
		RunPortal(0);                         // until saved
	}

	uint8_t fails = GetFails();
	if (fails >= WIFI_MAX_FAILED_BOOTS) {
		wlog("WiFi failed " + String(fails) + " boots in a row - setup portal for 5 min");
		SetFails(0);                          // after the timeout: 3 fresh tries
		RunPortal(WIFI_PORTAL_TIMEOUT_MS);
	}

	if (Connect(cfg, connectTimeoutMs)) {
		SetFails(0);
		if (MDNS.begin(cfg.host.c_str())) MDNS.addService("http", "tcp", 80);
		return true;
	}
	SetFails(fails + 1);
	wlog("WiFi connect to " + cfg.ssid + " failed (" + String(fails + 1) + "/" + String(WIFI_MAX_FAILED_BOOTS) + ")");
	return false;
}

const WifiCfg &WifiConfig() { return cfg; }
String         WifiApInfo() { return apInfo; }

// -----------------------------------------------------------------------------
// CheckLockedChannel — joined the SSID on another channel because the locked
// channel's AP was not found at boot? Look for it every 2 minutes and move
// back when it is there again (ESP-NOW only reaches boards on one channel).
// -----------------------------------------------------------------------------
#define WIFI_CHANNEL_RECHECK_MS  120000UL

static bool scanBusy = false;   // /wifiscan scan running (below)

static void CheckLockedChannel() {
	static uint32_t lastCheckMs = 0;
	if (scanBusy || searchMode) return;
	uint8_t lockCh = LockChannel(cfg);
	if (!cfgValid || !lockCh || WiFi.status() != WL_CONNECTED) return;
	if (WiFi.channel() == lockCh) return;
	if (millis() - lastCheckMs < WIFI_CHANNEL_RECHECK_MS) return;
	lastCheckMs = millis();

	uint8_t bssid[6];
	int32_t rssi = 0;
	if (!FindApOnChannel(cfg, bssid, rssi)) return;   // still not there: try again later

	wlog("WiFi on channel " + String(WiFi.channel()) + ", " + cfg.ssid + " found again on locked channel " +
	     String(lockCh) + " (" + String(rssi) + " dBm) - moving back");
	WiFi.disconnect();
	delay(200);
	if (Connect(cfg, 15000)) {
		wlog("WiFi back on channel " + String(WiFi.channel()) + " " + apInfo);
	} else {
		WifiCfg any = cfg;   // keep web / OTA reachable
		any.channel = 0;
		Connect(any, 15000);
		wlog("WiFi could not join channel " + String(lockCh) + " - on channel " + String(WiFi.channel()) + " again");
	}
}

// =============================================================================
// /wifiscan — which access points of our SSID this board hears (read-only).
// The scan runs in the background (~4 s, all channels); the board stays
// connected, ESP-NOW packets may be missed during it.
// =============================================================================
struct ScanSeen { String name; String bssid; uint8_t ch; int32_t rssi; bool connected; };
static std::vector<ScanSeen> scanSeen;
static uint32_t              scanAtMs = 0;

static void CollectScan(int n) {
	scanSeen.clear();
	const uint8_t *cur = WiFi.status() == WL_CONNECTED ? WiFi.BSSID() : nullptr;
	for (int i = 0; i < n; i++) {
		if (WiFi.SSID(i) != cfg.ssid) continue;
		const uint8_t *b = WiFi.BSSID(i);
		ScanSeen s;
		s.name = WiFi.BSSIDstr(i);
		for (const OmadaAp &ap : OMADA_APS)
			if (ap.name && b && memcmp(ap.mac, b, 6) == 0) s.name = ap.name;
		s.bssid     = WiFi.BSSIDstr(i);
		s.ch        = WiFi.channel(i);
		s.rssi      = WiFi.RSSI(i);
		s.connected = cur && b && memcmp(cur, b, 6) == 0;
		scanSeen.push_back(s);
	}
	std::sort(scanSeen.begin(), scanSeen.end(),
	          [](const ScanSeen &a, const ScanSeen &b) { return a.rssi > b.rssi; });
	WiFi.scanDelete();
	scanAtMs = millis() | 1;
}

static String ScanJson(bool start) {
	if (scanBusy) {
		int st = WiFi.scanComplete();
		if (st >= 0)                    { CollectScan(st); scanBusy = false; }
		else if (st == WIFI_SCAN_FAILED) scanBusy = false;
	}
	if (start && !scanBusy) {
		WiFi.scanDelete();
		if (WiFi.scanNetworks(true, false, false, 300) == WIFI_SCAN_RUNNING) scanBusy = true;
	}
	String j = "{\"busy\":";
	j += scanBusy ? "true" : "false";
	j += ",\"age\":" + String(scanAtMs ? (long)((millis() - scanAtMs) / 1000) : -1L);
	j += ",\"role\":\"" + role + "\",\"ssid\":\"" + cfg.ssid + "\",\"lock\":" + String(LockChannel(cfg)) +
	     ",\"locktxt\":\"" + LockText(cfg) + "\"";
	j += ",\"ch\":" + String(WiFi.channel()) + ",\"aps\":[";
	for (size_t i = 0; i < scanSeen.size(); i++) {
		const ScanSeen &s = scanSeen[i];
		if (i) j += ",";
		j += "{\"name\":\"" + s.name + "\",\"bssid\":\"" + s.bssid + "\",\"ch\":" + String(s.ch) +
		     ",\"rssi\":" + String(s.rssi) + ",\"conn\":" + (s.connected ? "true" : "false") + "}";
	}
	j += "]}";
	return j;
}

static const char SCAN_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>WiFi scan</title>
<style>
body{font-family:sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:16px;font-size:.95em}
h2{color:#0284c7;margin:0 0 4px}p{color:#94a3b8;margin:4px 0 10px;max-width:640px}a{color:#38bdf8}
table{width:100%;max-width:640px;border-collapse:collapse;background:#1e293b;border-radius:8px;overflow:hidden;margin-bottom:12px}
td,th{padding:7px 10px;border-bottom:1px solid #334155;text-align:left}th{color:#94a3b8;font-weight:normal}
button{background:#0284c7;color:#fff;border:none;border-radius:4px;padding:9px 16px;font-size:1em;cursor:pointer}
.bar{display:inline-block;height:8px;border-radius:4px;vertical-align:middle;margin-right:6px}
.g{color:#4ade80}.o{color:#fbbf24}.r{color:#f87171}b.c{color:#4ade80}
.nav{display:flex;gap:6px;margin-bottom:12px;flex-wrap:wrap}.nav a{color:#e2e8f0;text-decoration:none;background:#1e293b;padding:6px 12px;border-radius:6px}
</style></head><body>
<div class="nav"><a href="/">STATUS</a><a href="/wifi">WIFI</a></div>
<h2>Access points this board hears</h2>
<p id="info">…</p>
<table><thead><tr><th>Access point</th><th>Channel</th><th>Signal</th></tr></thead><tbody id="list"><tr><td colspan="3">Scanning…</td></tr></tbody></table>
<button onclick="scan()">Scan again</button>
<p>Signal: -30 to -55 dBm excellent, -55 to -67 good, -67 to -75 weak, below -75 poor.
ESP-NOW only works between boards on the <b>same channel</b>.</p>
<script>
function q(r){return r>=-55?['g','excellent']:r>=-67?['g','good']:r>=-75?['o','weak']:['r','poor'];}
function show(s){
  document.getElementById('info').innerHTML='Board: <b>'+s.role+'</b>, WiFi <b>'+s.ssid+'</b>, now on channel <b>'+s.ch+
    '</b>, channel lock: <b>'+s.locktxt+'</b>'+(s.busy?' &mdash; scanning…':(s.age>=0?' &mdash; scanned '+s.age+' s ago':''));
  if(s.busy&&!s.aps.length)return;
  document.getElementById('list').innerHTML=s.aps.length?s.aps.map(a=>{const k=q(a.rssi),w=Math.max(4,Math.min(100,(a.rssi+95)*2));
    return '<tr><td>'+(a.conn?'<b class="c">'+a.name+' &#10004; connected</b>':a.name)+'</td><td>'+a.ch+(a.ch==s.lock?' (lock)':'')+
      '</td><td><span class="bar" style="width:'+w+'px;background:currentColor" class="'+k[0]+'"></span><span class="'+k[0]+'">'+a.rssi+' dBm '+k[1]+'</span></td></tr>';}).join('')
    :'<tr><td colspan="3">No access point of this WiFi found</td></tr>';
  if(s.busy)setTimeout(poll,1000);
}
function poll(){fetch('/api/wifiscan').then(r=>r.json()).then(show).catch(()=>setTimeout(poll,2000));}
function scan(){fetch('/api/wifiscan?start=1').then(r=>r.json()).then(s=>{show(s);setTimeout(poll,1000);});}
scan();
</script></body></html>)HTML";

// -----------------------------------------------------------------------------
// CheckWifiLost — the ESP32's own auto-reconnect can give up for good (seen
// 2026-11-12: Omada refused the AP, both TX stayed offline until power-cycled).
// 30 s without WiFi → reconnect once; 2 min → restart (a fresh boot re-scans).
// -----------------------------------------------------------------------------
#define WIFI_LOST_RECONNECT_MS  30000UL
#define WIFI_LOST_RESTART_MS   120000UL

static void CheckWifiLost() {
	static uint32_t downSinceMs = 0;
	static bool     reconnected = false;
	if (!cfgValid || searchMode || WiFi.status() == WL_CONNECTED) {
		downSinceMs = 0;
		reconnected = false;
		return;
	}
	if (!downSinceMs) { downSinceMs = millis() | 1; return; }
	uint32_t down = millis() - downSinceMs;
	if (!reconnected && down >= WIFI_LOST_RECONNECT_MS) {
		reconnected = true;
		wlog("WiFi lost for 30 s - reconnecting");
		WiFi.disconnect();
		delay(100);
		WiFi.reconnect();
	}
	if (down >= WIFI_LOST_RESTART_MS) {
		wlog("WiFi lost for 2 min - restarting");
		delay(300);
		ESP.restart();
	}
}

bool    WifiFollowRx()              { return cfgValid && cfg.channel == WIFI_CH_FOLLOW_RX; }
uint8_t WifiFollowChannel()         { return FollowChGet(); }
void    WifiFollowStore(uint8_t ch) { FollowChSet(ch); }

void WifiSearchMode(bool on) {
	searchMode = on;
	WiFi.setAutoReconnect(!on);   // auto-reconnect would pull the radio back to the AP's channel
}

void WifiSetupLoop() {
	if (rebootPending && (int32_t)(millis() - rebootAtMs) >= 0) {
		delay(100);
		ESP.restart();
	}

	if (digitalRead(WIFI_RESET_PIN) == LOW) {
		if (resetPressMs == 0) resetPressMs = millis() | 1;
		else if (millis() - resetPressMs >= WIFI_RESET_HOLD_MS) {
			wlog("WiFi reset (button 0 held 5 s) - settings deleted, rebooting into setup portal");
			ForgetCfg();
			SetFails(0);
			delay(300);
			ESP.restart();
		}
	} else {
		resetPressMs = 0;
	}

	CheckLockedChannel();
	CheckWifiLost();
}

void WifiAddRoutes(AsyncWebServer &server) {
	server.on("/wifiscan", HTTP_GET, [](AsyncWebServerRequest *req) {
		req->send(200, "text/html", SCAN_HTML);
	});
	server.on("/api/wifiscan", HTTP_GET, [](AsyncWebServerRequest *req) {
		req->send(200, "application/json", ScanJson(req->hasParam("start")));
	});
	server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *req) {
		req->send(200, "text/html", BuildForm(false));
	});
	server.on("/wifi/save", HTTP_POST, [](AsyncWebServerRequest *req) {
		WifiCfg n;
		String  err, newPw;
		if (!FormToCfg(req, true, n, err)) { req->send(200, "text/html", ResultPage(false, err, nullptr)); return; }
		if ((err = OwnerCheck(req, newPw)).length()) { req->send(200, "text/html", ResultPage(false, err, nullptr)); return; }
		if (!SaveCfg(n))                   { req->send(200, "text/html", ResultPage(false, "Could not write the settings file", nullptr)); return; }
		if (newPw.length()) { OwnerPassStore(newPw); wlog("Owner password set"); }
		SetFails(0);
		wlog("WiFi settings changed on /wifi: " + n.ssid + (n.dhcp ? " DHCP" : " " + n.ip.toString()) + " - rebooting");
		req->send(200, "text/html", ResultPage(true, "", &n));
		ScheduleReboot();
	});
	server.on("/wifi/forget", HTTP_POST, [](AsyncWebServerRequest *req) {
		if (OwnerPassSet() && !OwnerPassOk(RawArg(req, "owner"))) {
			wlog("FORGET WIFI REFUSED: wrong owner password");
			req->send(200, "text/html", ResultPage(false, "Owner password is wrong - nothing was changed", nullptr));
			return;
		}
		wlog("WiFi settings deleted on /wifi - rebooting into setup portal");
		ForgetCfg();
		SetFails(0);
		String h = FPSTR(PAGE_HEAD);
		h += F("<h2>WiFi settings deleted</h2><p>The board reboots into its setup WiFi <b>IR-FORWARD-");
		h += role;
		h += '-';
		h += macSuffix();
		h += F("</b> (open WiFi, no password). Join it and open http://192.168.4.1 - the owner password is needed to save.</p></body></html>");
		req->send(200, "text/html", h);
		ScheduleReboot();
	});
}
