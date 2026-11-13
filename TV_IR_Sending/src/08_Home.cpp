// =============================================================================
// 08_Home.cpp — Home Page Route Handler & Real-Time Status Maintenance
// =============================================================================

#include <Arduino.h>
#include "all_includes.h"
#include "OmadaAps.h"     // Omada AP name from the connected BSSID

// -----------------------------------------------------------------------------
// handleRoot  (GET /)
// -----------------------------------------------------------------------------
void handleRoot(AsyncWebServerRequest *request){
	if (!beginHtmlStream(request)) return;

	if (!MainPageObject) MapHomePage();

	WT_HomePage();
	resetHtmlBuf();
	appendHtmlStr(webHeadPage);
	append_FileAction(FA_MESS_06);
	appendMainPageSel(PS_HOME);
	appendHtmlStr(webSocketScript);
	appendHtmlBuf(PageBuf, getPageBufLen());
	appendHtmlStr(webHTMLEnd);

	sendHtmlPage(request);
}

// -----------------------------------------------------------------------------
// appendMainPageSel
// Builds the top navigation tab strip: STATUS, IR SEQUENCES, SETTINGS, WIFI, FILES, ABOUT.
// -----------------------------------------------------------------------------
void appendMainPageSel(int Index){
	appendHtmlStr("<div class='nav'>");
	appendHtmlStr(Index == PS_HOME     ? "<a class='act'>STATUS</a>"     : "<a href='/'>STATUS</a>");
	appendHtmlStr("<a href='/recseq'>IR SEQUENCES</a>");
	appendHtmlStr(Index == PS_SETTINGS ? "<a class='act'>SETTINGS</a>" : "<a href='/settings'>SETTINGS</a>");
	appendHtmlStr("<a href='/wifi'>WIFI</a>");
	appendHtmlStr("<a href='/files'>FILES</a>");
	appendHtmlStr(Index == PS_ABOUT    ? "<a class='act'>ABOUT</a>"    : "<a href='/about'>ABOUT</a>");
	appendHtmlStr("</div>");
}

// ─── Home page display values (written by MaintainHomePage) ──────────────────
static String HomeValStyle  = "";
static String HomeLinkStyle = ATT_ORANGE;
static String HomeRxLink    = "Searching...";
static String HomeRxRoom    = "-";
static String HomeRxMac     = "-";
static String HomeRxBeacon  = "-";
static int    HomeForwarded = 0;
static String HomeIrSent    = "-";
static int    HomeDelivered = 0;
static int    HomeFailed    = 0;
static String HomeLastCode  = "-";
static String HomeWifi      = "-";
static String HomeAp        = "-";
static String HomeMyMac     = "-";
static String HomeRouter      = "checking...";
static String HomeRouterStyle = "";
static String HomeMovStyle  = ATT_ORANGE;
static String HomeMov       = "-";

// -----------------------------------------------------------------------------
// WT_HomePage
// Writes the home-page WT parameter table into PageBuf.
// -----------------------------------------------------------------------------
void WT_HomePage(){
	resetPageBuf();
	WT_StartPage();
	WT_StartTable("IR LINK TO RECEIVER", "INSTANT VALUES", "");

	for (int i = 0; i < PAGE_VAR_HOME; i++) {
		WT_DisplayRow(&HomeList[i]);
	}

	WT_EndPage();
}

// -----------------------------------------------------------------------------
// MapHomePage
// Allocates the HomeList[] PageVar array.
// -----------------------------------------------------------------------------
void MapHomePage(){
	MainPageObject = true;
	HomeList = new PageVar[PAGE_VAR_HOME];
	for (int i = 0; i < PAGE_VAR_HOME; i++) {
		switch (i) {
			case HOME_RX_LINK:
				HomeList[i].Var = new PageElement(&HomeRxLink,    F("Receiver link"),         "", &HomeLinkStyle, i+ID_HOME);
				break;
			case HOME_RX_ROOM:
				HomeList[i].Var = new PageElement(&HomeRxRoom,    F("Receiver Room"),               "", &HomeValStyle,  i+ID_HOME);
				break;
			case HOME_RX_MAC:
				HomeList[i].Var = new PageElement(&HomeRxMac,     F("Receiver MAC"),          "", &HomeValStyle,  i+ID_HOME);
				break;
			case HOME_RX_BEACON:
				HomeList[i].Var = new PageElement(&HomeRxBeacon,  F("Last receiver beacon"),  "", &HomeValStyle,  i+ID_HOME);
				break;
			case HOME_FORWARDED:
				HomeList[i].Var = new PageElement(&HomeForwarded, F("DSTV frames forwarded"), "", &HomeValStyle,  i+ID_HOME);
				break;
			case HOME_IR_SENT:
				HomeList[i].Var = new PageElement(&HomeIrSent,    F("IR codes sent out to IR"), "", &HomeValStyle, i+ID_HOME);
				break;
			case HOME_DELIVERED:
				HomeList[i].Var = new PageElement(&HomeDelivered, F("Delivered (ACK)"),       "", &HomeValStyle,  i+ID_HOME);
				break;
			case HOME_FAILED:
				HomeList[i].Var = new PageElement(&HomeFailed,    F("Not delivered"),         "", &HomeValStyle,  i+ID_HOME);
				break;
			case HOME_LAST_CODE:
				HomeList[i].Var = new PageElement(&HomeLastCode,  F("Last forwarded code"),   "", &HomeValStyle,  i+ID_HOME);
				break;
			case HOME_WIFI:
				HomeList[i].Var = new PageElement(&HomeWifi,      F("WiFi channel / RSSI"),   "", &HomeValStyle,  i+ID_HOME);
				break;
			case HOME_AP:
				HomeList[i].Var = new PageElement(&HomeAp,        F("Omada AP"),              "", &HomeValStyle,  i+ID_HOME);
				break;
			case HOME_MY_MAC:
				HomeList[i].Var = new PageElement(&HomeMyMac,     F("This TX MAC"),           "", &HomeValStyle,  i+ID_HOME);
				break;
			case HOME_ROUTER:
				HomeList[i].Var = new PageElement(&HomeRouter,    F("Router reachable"),      "", &HomeRouterStyle, i+ID_HOME);
				break;
			case HOME_MOV:
				HomeList[i].Var = new PageElement(&HomeMov,       F("Surge protection (MOV)"), "", &HomeMovStyle, i+ID_HOME);
				break;
		}
	}
}

// -----------------------------------------------------------------------------
// webRebootButton
// Returns an HTML button that calls ButtonReboot() via WebSocket when clicked.
// -----------------------------------------------------------------------------
void webRebootButton()
{
	appendHtmlStr("<div><br></div><div><b><a>");
	appendHtmlStr("<b><a><input type=\"button\" id=\"REBOOT\" onclick=\"ButtonReboot()\" style=\"font-size:14px;background-color:#dc2626;color:#fff;border:none;padding:6px 14px;border-radius:4px;cursor:pointer\" value=\"REBOOT\"</a></b></div>");
}

// -----------------------------------------------------------------------------
// SendHomePageValues  (called every 10 ms from MaintainWebPageValues)
// -----------------------------------------------------------------------------
void SendHomePageValues(){
	for (int i = 0; i < PAGE_VAR_HOME; i++){
		if (HomeList[i].Var->HasChanged()){
			webParamUpdate(HomeList[i].Var->GetName(), HomeList[i].Var->GetID("V"), HomeList[i].Var->GetVarStyle());
			HomeList[i].Var->UpdateLast();
		}
	}
}

// -----------------------------------------------------------------------------
// MaintainHomePage  (called every 500 ms from WebSocketLoop)
// Refreshes the ESP-NOW link values; SendHomePageValues pushes the changes.
// -----------------------------------------------------------------------------
void MaintainHomePage(){
	if (!HomePageReady) {
		if (!TimerExpired(TIM_STABILIZE_DELAY)) return;
		HomePageReady = true;
	}

	EspNowStats s = EspNowGetStats();

	if (!s.rxKnown) {
		HomeLinkStyle = ATT_ORANGE;
		HomeRxLink    = "Searching...";
	} else if (s.linkUp) {
		HomeLinkStyle = ATT_GREEN;
		HomeRxLink    = "Linked";
	} else {
		HomeLinkStyle = ATT_RED;
		HomeRxLink    = "LOST";
	}

	if (s.rxKnown) {
		char mac[18];
		snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
		         s.rxMac[0], s.rxMac[1], s.rxMac[2], s.rxMac[3], s.rxMac[4], s.rxMac[5]);
		HomeRxMac    = mac;
		HomeRxRoom   = s.rxName[0] ? String(s.rxName) : String("-");
		HomeRxBeacon = String(s.beaconAgeMs / 1000) + " s ago";
	}

	HomeForwarded = (int)s.forwarded;
	HomeIrSent    = s.rxIrSentKnown ? String(s.rxIrSent) : String("-");
	HomeDelivered = (int)s.sentOk;
	HomeFailed    = (int)s.sentFail;

	if (s.lastFormat == FMT_XMP) {
		char buf[24];
		snprintf(buf, sizeof(buf), "XMP 0x%016llX", (unsigned long long)s.lastValue);
		HomeLastCode = buf;
	} else if (s.lastFormat == FMT_RAW) {
		HomeLastCode = "RAW " + String(s.lastRawCount) + " durations";
	} else {
		HomeLastCode = "none yet - forwarding " + IrForwardRemoteLabel();
	}

	HomeWifi = "ch " + String(WiFi.channel()) + " / " + String(WiFi.RSSI()) + " dBm";
	HomeAp    = OmadaApName();
	HomeMyMac = WiFi.macAddress();

	HomeRouter      = RouterCheckText();
	HomeRouterStyle = RouterReachable() ? "" : ATT_RED;

	BoardMovState mov = BoardMovGetState();
	HomeMovStyle = mov == MOV_OK ? ATT_GREEN : mov == MOV_BLOWN ? ATT_RED : mov == MOV_NONE ? ATT_WHITE : ATT_ORANGE;
	HomeMov      = BoardMovText();
}
