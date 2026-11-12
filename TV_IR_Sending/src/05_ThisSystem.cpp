// =============================================================================
// 05_ThisSystem.cpp — I/O Setup, Input Debounce, System Loop & HTTP Routes
// =============================================================================

#include <Arduino.h>
#include "all_includes.h"

void SetupSystemIOs()
{
	// IR receiver (TSOP31238) is open-collector — pull HIGH so idle reads HIGH.
	// All other PCB pins are set up by BoardSetup() (Board.cpp).
	pinMode(IPIN_IR_RECEIVE, INPUT_PULLUP);
}

void PinDebounce() {
    unsigned long now = millis();
    for (int i = 0; i < InputPorts; i++) {
        bool reading = digitalRead(InPort[i].PinIO);

        if (reading && !InPort[i].Input) {
            InPort[i].MemoryL = false;
            if (!InPort[i].MemoryH) {
                InPort[i].Time = now;
                InPort[i].MemoryH = true;
            } else if ((now - InPort[i].Time) >= DEBOUNCE_STEADY_MS) {
                InPort[i].Input = true;
            }
        } else if (!reading && InPort[i].Input) {
            InPort[i].MemoryH = false;
            if (!InPort[i].MemoryL) {
                InPort[i].Time = now;
                InPort[i].MemoryL = true;
            } else if ((now - InPort[i].Time) >= DEBOUNCE_STEADY_MS) {
                InPort[i].Input = false;
            }
        } else {
            InPort[i].MemoryH = InPort[i].MemoryL = false;
        }
    }
}

// -----------------------------------------------------------------------------
// MaintainWebPageValues  (called every 10 ms from MaintainWebPage)
// -----------------------------------------------------------------------------
void MaintainWebPageValues(){
	SendSettingsPageValues();
	if (MainPageObject)  SendHomePageValues();
}

// -----------------------------------------------------------------------------
// MaintainBoard  (called every 10 ms from SystemLoop)
// Logs TMOV state changes and keeps the Settings "mute" checkbox
// in step with Board.cpp (the mute clears itself when the TMOV is replaced).
// -----------------------------------------------------------------------------
void MaintainBoard(){
	static BoardMovState lastState = MOV_UNKNOWN;
	static bool          lastMuted = BoardMovMuted();

	BoardMovState state = BoardMovGetState();
	if (state != lastState) {
		if (state == MOV_BLOWN) {
			SaveGenReport F("SURGE PROTECTION (TMOV) BLOWN - replace the MOV");
		} else if (state == MOV_OK && lastState == MOV_BLOWN) {
			SaveGenReport F("Surge protection (TMOV) OK again");
		}
		lastState = state;
	}

	bool muted = BoardMovMuted();
	if (muted != lastMuted) {          // ticked / unticked / auto-cleared after MOV replaced
		lastMuted  = muted;
		MovMuteChk = muted;
		Params->WriteData();           // keep "MovBuzzerMute" in AllParams.json in step
	}
}

// -----------------------------------------------------------------------------
// MaintainRouter  (called every 10 ms from SystemLoop)
// Gateway ping once a minute (RouterCheck.cpp) — catches "WiFi connected but
// no traffic", which the WiFi-disconnect watchdog in LEDToggle() cannot see.
// -----------------------------------------------------------------------------
void MaintainRouter(){
	switch (RouterCheckLoop()) {
		case RC_RECONNECT:
			SaveGenReport("Router " + WiFi.gatewayIP().toString() + " unreachable for 3 min - reconnecting WiFi");
			WiFi.reconnect();
			break;
		case RC_REBOOT:
			SaveGenReport F("REBOOT - router unreachable for 6 min");
			delay(100);
			ESP.restart();
			break;
		case RC_RECOVERED:
			SaveGenReport F("Router reachable again");
			break;
		default:
			break;
	}
}

// -----------------------------------------------------------------------------
// SystemLoop  (called every 10 ms from loop())
// -----------------------------------------------------------------------------
void SystemLoop() {
	if (!OTAInProgress) {
		MaintainBoard();
		MaintainRouter();
		EspNowMaintain();
	}
}

// -----------------------------------------------------------------------------
// AddRoutes
// Registers all HTTP GET/POST route handlers with the AsyncWebServer.
// -----------------------------------------------------------------------------
void AddRoutes(){

	// ── Main Pages ────────────────────────────────────────────────────────────
	server.on("/",         handleRoot);      // Home page        (08_Home.cpp)
	server.on("/settings", handleSettings);  // System settings  (12_Settings.cpp)
	server.on("/about",    handleAbout);     // About / OTA      (15_About.cpp)
	RecSeqAddRoutes();                       // /recseq record for the RX (18_RecSeq.cpp)
	WifiAddRoutes(server);                   // /wifi settings   (WifiSetup.cpp)
	server.on("/api/ota",  handleApiOta);    // OTA progress JSON, polled by /about

	// ── System Control ────────────────────────────────────────────────────────
	server.on("/disablesystem", handleDisableSystem);
	server.on("/enablesystem",  handleEnableSystem);


	// ── File Manager ──────────────────────────────────────────────────────────
	server.on("/files/view",   handleFileView);
	server.on("/files/delete", handleFileDeleteJson);
	server.on("/api/files",    handleApiFiles);
	server.on("/files",        FilesPage);
	server.on("/download",     File_Download);
	server.on("/upload",       File_Upload);
	server.on("/delete",       File_Delete);
	server.on("/fupload", HTTP_POST,
		[](AsyncWebServerRequest *request){ request->redirect("/files"); },
		handleFileUpload
	);
}
