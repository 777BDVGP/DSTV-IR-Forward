// =============================================================================
// 12_Settings.cpp — System Parameters, Settings Page & System Control Handlers
// =============================================================================

#include <Arduino.h>
#include "all_includes.h"

void appendSettingsPageSel(int Index);

// =============================================================================
// AllParams — Object Factory & Persistence
// =============================================================================

void CreateAllParams(){
	MovMuteChk = BoardMovMuted();      // NVS copy; AllParams.json wins if it has the key
	MovFittedChk = BoardMovFitted();
	Params = new AllParams;
	Params->ReadData();
	BoardMovSetMuted(MovMuteChk);
	Params->WriteData();
	MapSettingsPage();
}

AllParams::AllParams()  {}
AllParams::~AllParams(){}
void AllParams::Maintain(){}

void AllParams::WriteData()
{
	String StrFileName = "/AllParams.json";
	File ChannelDataFile = SPIFFS.open(StrFileName, "w");
	DynamicJsonDocument doc(1024);

	doc["SysName"]          = SysName;
	doc["LogGeneralEvents"] = LogGeneralEvents;
	doc["BootupWait"]       = BootupWait;
	doc["LearnMode"]        = LearnMode;
	doc["LearnRemote"]      = LearnRemote;
	doc["ForwardRemote"]    = ForwardRemote;
	doc["MovBuzzerMute"]    = MovMuteChk;       // copy of the Board.cpp NVS flag (visible on FILES)
	doc["RebootEnable"]     = RebootEnable;
	doc["RebootTime"]       = RebootTime;

	if (ChannelDataFile)
	{
		serializeJsonPretty(doc, ChannelDataFile);
		ChannelDataFile.close();
	}
}

void AllParams::ReadData()
{
	String StrFileName = "/AllParams.json";
	File ChannelDataFile = SPIFFS.open(StrFileName, "r");
	if (ChannelDataFile)
	{
		size_t size = ChannelDataFile.size();
		if (size <= 1024)
		{
			std::unique_ptr<char[]> buf(new char[size]);
			ChannelDataFile.readBytes(buf.get(), size);
			DynamicJsonDocument doc(1024);
			auto error = deserializeJson(doc, buf.get());
			if (!error)
			{
				if ( doc["SysName"].as<String>()          != "null" ){ SysName          = doc["SysName"].as<String>(); }
				if ( doc["LogGeneralEvents"].as<String>() != "null" ){ LogGeneralEvents = (doc["LogGeneralEvents"].as<String>() == "true"); }
				if ( doc["BootupWait"].as<String>()       != "null" ){ BootupWait       = doc["BootupWait"]; }
				if ( doc["LearnMode"].as<String>()        != "null" ){ LearnMode        = (doc["LearnMode"].as<String>() == "true"); }
				// RecordDSTVCode / DSTVAddress (up to 1.7.1) are dropped: the old
				// DSTV address was never used as a filter, so "XMP" is the same
				if ( doc["LearnRemote"].as<String>()      != "null" ){ LearnRemote      = (doc["LearnRemote"].as<String>() == "true"); }
				if ( doc["ForwardRemote"].as<String>()    != "null" ){ ForwardRemote    = doc["ForwardRemote"].as<String>(); }
				if ( doc["MovBuzzerMute"].as<String>()    != "null" ){ MovMuteChk       = (doc["MovBuzzerMute"].as<String>() == "true"); }
				// One reboot slot now; older files had three — keep what was slot 3
				if ( doc["RebootEnable3"].as<String>()    != "null" ){ RebootEnable     = (doc["RebootEnable3"].as<String>() == "true"); }
				if ( doc["RebootTime3"].as<String>()      != "null" ){ RebootTime       = doc["RebootTime3"].as<String>(); }
				if ( doc["RebootEnable"].as<String>()     != "null" ){ RebootEnable     = (doc["RebootEnable"].as<String>() == "true"); }
				if ( doc["RebootTime"].as<String>()       != "null" ){ RebootTime       = doc["RebootTime"].as<String>(); }
			}
			else
			{
				SaveGenReport F(" File Error open File AllParams.json");
			}
		}
		ChannelDataFile.close();
	}
}

// =============================================================================
// Settings Page — PageVar Mapping
// =============================================================================

void MapSettingsPage(){
	SettingsList = new PageVar[PAGE_VAR_SETTINGS];
	for (int i=0; i < PAGE_VAR_SETTINGS; i++)
	{
		switch(i)
		{
			case SET_SYSNAME:
				SettingsList[i].Var = new PageElement(&Params->SysName, F("System Name (room name on the RX, max 15 chars)"), "", &SetStyle, i+ID_SETTINGS);
				break;
			case SET_LOG_GENERAL:
				SettingsList[i].Var = new PageElement(&Params->LogGeneralEvents, F("Log General Events"), "", &SetStyle, i+ID_SETTINGS);
				break;
			case SET_BOOTUP_DELAY:
				SettingsList[i].Var = new PageElement(&Params->BootupWait, F("Bootup Wait (ms)"), "", &SetStyle, i+ID_SETTINGS);
				break;
			case SET_LEARN_MODE:
				SettingsList[i].Var = new PageElement(&Params->LearnMode, F("Diagnostic tool (log all IR to IRLog.txt)"), "", &SetStyle, i+ID_SETTINGS);
				break;
			case SET_LEARN_REMOTE:
				SettingsList[i].Var = new PageElement(&Params->LearnRemote, F("Learn remote: tick, UPDATE, then press a button on the TV box remote"), "", &SetStyle, i+ID_SETTINGS);
				break;
			case SET_FWD_REMOTE:
				SettingsList[i].Var = new PageElement(&Params->ForwardRemote, F("Forward remote (XMP = DSTV)"), "", &SetStyle, i+ID_SETTINGS);
				SettingsList[i].Var->SetPlaceholder("XMP");
				break;
			case SET_MOV_MUTE:
				SettingsList[i].Var = new PageElement(&MovMuteChk, F("Mute buzzer - MOV blown (until replaced)"), "", &SetStyle, i+ID_SETTINGS);
				break;
			case SET_MOV_FITTED:
				SettingsList[i].Var = new PageElement(&MovFittedChk, F("Surge protection parts fitted (TMOV, 4N35)"), "", &SetStyle, i+ID_SETTINGS);
				break;
			case SET_REBOOT_EN:
				SettingsList[i].Var = new PageElement(&Params->RebootEnable, F("Enable Scheduled Reboot"), "", &SetStyle, i+ID_SETTINGS);
				break;
			case SET_REBOOT_TIME:
				SettingsList[i].Var = new PageElement(&Params->RebootTime, F("Scheduled Reboot @ Time"), "", &SetStyle, i+ID_SETTINGS);
				break;
		}
	}
}

// =============================================================================
// Settings Web Page — Route Handlers
// =============================================================================

void handleSettings(AsyncWebServerRequest *request){
	if (!beginHtmlStream(request)) return;
	WT_SETUP_Settings();
	resetHtmlBuf();
	appendHtmlStr(webHeadPage);
	append_FileAction(FA_MESS_06);
	appendSettingsPageSel(PS_SETTINGS);
	appendHtmlStr(webSocketScript);
	appendHtmlBuf(PageBuf, getPageBufLen());
	appendHtmlStr(webUpateButton);
	webRebootButton();
	appendHtmlStr(webHTMLEnd);
	sendHtmlPage(request);
}

void appendSettingsPageSel(int Index){
	appendMainPageSel(Index);
}

void appendSettingsActionButtons(){
	appendHtmlStr("<div class='nav'>");
	if (!NORMAL_OPERATION) appendHtmlStr("<a class='warn' href='/enablesystem'>ENABLE SYSTEM</a>");
	else                   appendHtmlStr("<a class='warn' href='/disablesystem'>DISABLE SYSTEM</a>");
	appendHtmlStr("</div>");
}

// =============================================================================
// Settings Page — WT_* Table Builder & WebSocket Handlers
// =============================================================================

void WT_SETUP_Settings(){
	resetPageBuf();
	WT_StartPage();
	WT_StartTable("Parameter Description", "Value", "");

	for (int i=0; i < PAGE_VAR_SETTINGS_BASE; i++)
	{
		WT_EditRow(&SettingsList[i]);
	}
	// Two 2-column rows (a checkbox + time in one row added a 3rd column)
	WT_EditRow(&SettingsList[SET_REBOOT_EN]);
	WT_EditRow(&SettingsList[SET_REBOOT_TIME], "time");
	WT_EndPage();
}

void SendSettingsPageValues(){
	for (int i=0; i < PAGE_VAR_SETTINGS; i++)
	{
		if(SettingsList[i].Var->HasChanged())
		{
			webParamUpdate(SettingsList[i].Var->GetName(),
			               SettingsList[i].Var->GetID(""),
			               SettingsList[i].Var->GetVarStyle());
			SettingsList[i].Var->UpdateLast();
		}
	}
}

void UpdateSettingsPageValues(uint32_t clientId, JsonObject *root){

	for (JsonPair kv : *root){
		for (int i=0; i < PAGE_VAR_SETTINGS; i++)
		{
			if(SettingsList[i].Var->GetID("") == kv.key().c_str())
			{
				SettingsList[i].Var->SetName(String(kv.value().as<const char*>()));
			}
		}
	}

	String msg = "Parameters Updated!";
	if (!IrSetForwardRemote(Params->ForwardRemote)) {        // typo → keep the remote that works
		msg = "Forward remote '" + Params->ForwardRemote + "' not understood - kept " + IrForwardRemoteText();
		Params->ForwardRemote = IrForwardRemoteText();
	} else if (Params->ForwardRemote != IrForwardRemoteText()) {
		Params->ForwardRemote = IrForwardRemoteText();       // tidy: "nec 0x20df" → "NEC 0x20DF"
	}
	if (Params->LearnRemote) msg += " Now press a button on the TV box remote.";
	WebMessUpdate(clientId, msg, "color: red", true);
	Params->WriteData();
	EspNowSetRoomName(Params->SysName);   // RX shows the new room name at once
	BoardMovSetMuted(MovMuteChk);   // Board.cpp ignores/clears it while the TMOV is OK
	if (MovFittedChk != BoardMovFitted()) {
		BoardMovSetFitted(MovFittedChk);
		SaveGenReport(MovFittedChk ? F("Surge protection set to FITTED - checking again")
		                           : F("Surge protection set to NOT FITTED - no check, no buzzer"));
	}
}

// =============================================================================
// System Control Action Handlers
// =============================================================================

void handleDisableSystem(AsyncWebServerRequest *request) {
	SetMess    = FA_MESS_GRN;
	SpesialStr = "Normal Operation Off";
	NORMAL_OPERATION = false;
	handleSettings(request);
}

void handleEnableSystem(AsyncWebServerRequest *request) {
	SetMess    = FA_MESS_RED;
	SpesialStr = "Normal Operation On";
	NORMAL_OPERATION = true;
	handleSettings(request);
}
