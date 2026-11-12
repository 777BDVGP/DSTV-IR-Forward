// =============================================================================
// 15_About.cpp — OTA Update Service & About Page
// =============================================================================
// Implements ArduinoOTA setup (with watchdog guard), the About web page,
// and About.json persistence (ip, gateway, subnet, dns).
// The About page is read-only: a 2-column OTA table, a REBOOT button,
// and a centred project-info block.  System Name is edited on the Settings page.
// =============================================================================

#include "OwnerPass.h"
#include <Arduino.h>
#include "all_includes.h"

// ─── SPIFFS path for About.json ───────────────────────────────────────────────
static constexpr const char* ABOUT_FILE_PATH = "/About.json";

// ─── OTA State ────────────────────────────────────────────────────────────────
bool OTAInProgress      = false;
int  OTAPercentComplete = 0;
int  OTAError           = -1;

// =============================================================================
// About.json — IP Persistence
// =============================================================================


// -----------------------------------------------------------------------------
// SetupAbout
// Compares the current build time (__DATE__ " " __TIME__) with the stored
// buildStamp in /About.json.  If they differ, a new build has been uploaded
// (likely via USB cable), so we update buildStamp.
// Called once from setup() after WiFi is connected (to ensure we have NTP time).
// -----------------------------------------------------------------------------
void SetupAbout() {
    String currentBuild = String(__DATE__) + " " + String(__TIME__);
    DynamicJsonDocument doc(512);
    bool changed = false;

    File fr = SPIFFS.open(ABOUT_FILE_PATH, "r");
    if (fr) {
        deserializeJson(doc, fr);
        fr.close();
        if (!doc.containsKey("buildStamp") || doc["buildStamp"].as<String>() != currentBuild) {
            changed = true;
        }
    } else {
        changed = true;
    }

    if (changed) {
        doc["buildStamp"]   = currentBuild;
        File fw = SPIFFS.open(ABOUT_FILE_PATH, "w");
        if (fw) {
            serializeJson(doc, fw);
            fw.close();
            DEBUG_PRINTLN("About.json updated with new buildStamp");
        }
    }
}

// =============================================================================
// OTA Setup
// =============================================================================

// -----------------------------------------------------------------------------
// SetupOTA
// Configures ArduinoOTA with the system name as hostname and registers callbacks.
// The watchdog Ticker is detached during flash writes and re-attached on end/error.
// Called once from setup() after WiFi connects.
// -----------------------------------------------------------------------------
void SetupOTA() {
    String deviceName = WifiConfig().host;   // valid host name (SysName may contain spaces)

    ArduinoOTA.setPort(3232);
    ArduinoOTA.setHostname(deviceName.c_str());

    ArduinoOTA
        .onStart([]() {
            OTAError           = -1;
            OTAPercentComplete = 0;
            OTAInProgress      = true;
            secondTick.detach();   // Disable watchdog — flash writes block > 5 s
            ws.textAll("{\"type\":\"ota_start\"}");
            DEBUG_PRINTLN("OTA Start");
        })
        .onEnd([]() {
            DEBUG_PRINTLN("\nOTA End");
            ws.textAll("{\"type\":\"ota_end\"}");
            OTAPercentComplete = 100;   // /api/ota reports "done - rebooting"
            secondTick.attach(1, ISRwatchdog);
        })
        .onProgress([](unsigned int progress, unsigned int total) {
            int pct = total ? (int)((uint64_t)progress * 100 / total) : 0;
            if (pct != OTAPercentComplete && pct % 5 == 0) {
                String msg = "{\"type\":\"ota_progress\",\"percent\":" + String(pct) + "}";
                ws.textAll(msg);
            }
            OTAPercentComplete = pct;
            DEBUG_PRINTF("OTA Progress: %u%%\r", OTAPercentComplete);
        })
        .onError([](ota_error_t error) {
            DEBUG_PRINTF("OTA Error[%u]\n", error);
            ws.textAll("{\"type\":\"ota_error\"}");
            OTAError      = (int)error;
            OTAInProgress = false;
            secondTick.attach(1, ISRwatchdog);
        });

    // Network upload needs the owner password (OwnerPass.h). PlatformIO sends it
    // from the environment variable IR_OTA_PASS (upload_flags in platformio.ini).
    if (OwnerPassMd5().length() == 32) ArduinoOTA.setPasswordHash(OwnerPassMd5().c_str());
    ArduinoOTA.begin();
    DEBUG_PRINTLN("OTA Ready — " + WiFi.localIP().toString());
    DEBUG_PRINTLN("PIO upload: pio run -t upload --upload-port " + WiFi.localIP().toString());
}

// =============================================================================
// About Page — HTML Builders
// =============================================================================

// -----------------------------------------------------------------------------
// buildAboutTable
// Writes a 2-column read-only OTA info table into PageBuf using the WT_* system.
// Row 1: "OTA (Over-The-Air) Updates" | "Ready"  (green)
// Row 2: "OTA Terminal Command"        | "pio run -t upload --upload-port <board ip>"
// ListObj is nullptr — safe because Type is "text" (never a <select>).
// -----------------------------------------------------------------------------
static void buildAboutTable() {
    String otaCmd = "pio run -t upload --upload-port " + WiFi.localIP().toString();

    resetPageBuf();
    WT_StartPage();
    WT_StartTable("PARAMETER", "VALUE", "");

    // Row 1 — OTA status (live: JS setOtaStatus() updates value + colour via WebSocket)
    WT_StartLine();
    WT_Column(WT_LABEL, WT_FIRST, WT_LAST, "text", "OTA (Over-The-Air) Updates", "OTA_L1",     "",          nullptr);
    WT_Column(WT_INPUT, WT_FIRST, WT_LAST, "text", "Ready to Update",            "ota_status", COLOR_GREEN, nullptr, "readonly:");
    WT_EndLine();

    // Row 2 — PlatformIO upload command
    WT_StartLine();
    WT_Column(WT_LABEL, WT_FIRST, WT_MORE, "text", "OTA Terminal Command", "OTA_L2", "", nullptr);
    WT_Column(WT_LABEL, WT_NEXT,  WT_LAST, "text", otaCmd.c_str(),        "OTA_V2", "font-size:11px", nullptr);
    WT_Column(WT_INPUT, WT_FIRST, WT_LAST, "", "", "", "", nullptr);
    WT_EndLine();

    WT_EndPage();
}

// -----------------------------------------------------------------------------
// buildProjectInfoDiv
// Returns a centred HTML block showing project name, build date, and PCB type.
// -----------------------------------------------------------------------------
static void buildProjectInfoDiv() {
    const char* bd = __DATE__;
    char bDay[3] = { (bd[4]==' ' ? '0' : bd[4]), bd[5], '\0' };
    char bMon[4] = { bd[0], bd[1], bd[2], '\0' };
    char bYr[5]  = { bd[7], bd[8], bd[9], bd[10], '\0' };
    char buildDate[32];
    snprintf(buildDate, sizeof(buildDate), "%s %s %s %s", bDay, bMon, bYr, __TIME__);

    // Same card + lines as the RX About page
    appendHtmlStr("<div style='background:#1e293b;border-radius:8px;padding:12px;margin-bottom:12px;"
                  "text-align:center;line-height:1.7'>");
    appendHtmlStr("<b>Project:</b> TV IR Forward - Transmitter");
    appendHtmlStr("<br><b>Firmware:</b> " FW_VERSION);
    appendHtmlStr("<br><b>Build Date:</b> ");
    appendHtmlStr(buildDate);
    appendHtmlStr("<br><b>PCB:</b> IR_FORWARD_V1 (WEMOS LOLIN S2 Mini)");
    appendHtmlStr("<br><b>IP:</b> ");
    appendHtmlStr(WiFi.localIP().toString().c_str());
    appendHtmlStr(" &nbsp; <b>MAC:</b> ");
    appendHtmlStr(WiFi.macAddress().c_str());
    appendHtmlStr("</div>");
}

// -----------------------------------------------------------------------------
// handleApiOta  (GET /api/ota)  → {"pct":N,"err":E}
//   pct -1 = idle, 0..99 = uploading, 100 = done ("Busy Rebooting"); err -1 = none
// Polled every second by the About page — works even if the WebSocket drops.
// -----------------------------------------------------------------------------
void handleApiOta(AsyncWebServerRequest *request) {
    int pct = (OTAInProgress || OTAPercentComplete >= 100) ? OTAPercentComplete : -1;
    request->send(200, "application/json",
                  "{\"pct\":" + String(pct) + ",\"err\":" + String(OTAError) + "}");
}

// OTA status poller for the About page (same behaviour as the RX About page)
static const char OTA_POLL_SCRIPT[] PROGMEM = R"JS(<script>
var otaBusy=false;
function otaPoll(){fetch('/api/ota',{cache:'no-store'}).then(function(r){return r.json();}).then(function(o){
  if(o.pct>=100){setOtaStatus('Busy Rebooting','red');otaBusy=true;}
  else if(o.pct>=0){setOtaStatus('Uploading '+o.pct+' %','orange');otaBusy=true;}
  else if(o.err>=0){setOtaStatus('OTA error '+o.err,'red');otaBusy=false;}
  else if(otaBusy){otaBusy=false;location.reload();}
  else setOtaStatus('Ready to Update','#22c55e');
}).catch(function(){if(otaBusy)setOtaStatus('Busy Rebooting','red');})
  .finally(function(){setTimeout(otaPoll,1000);});}
otaPoll();
</script>)JS";

// =============================================================================
// About Page — Route Handler
// =============================================================================

// -----------------------------------------------------------------------------
// handleAbout  (GET /about)
// Builds and serves the About page:
//   1. OTA info table (read-only, 2 columns via WT_* system)
//   2. REBOOT button
//   3. Centred project info card (Project, Firmware, Build Date, PCB, IP, MAC)
// -----------------------------------------------------------------------------
void handleAbout(AsyncWebServerRequest *request) {
    if (!beginHtmlStream(request)) return;
    buildAboutTable();                            
    resetHtmlBuf();
    appendHtmlStr(webHeadPage);
    append_FileAction(FA_MESS_06);
    appendMainPageSel(PS_ABOUT);                  
    appendHtmlStr(webSocketScript);               
    appendHtmlBuf(PageBuf, getPageBufLen());      
    // REBOOT in its own card, like the RX About page
    appendHtmlStr("<div style='background:#1e293b;border-radius:8px;padding:12px;margin-bottom:12px'>"
                  "<input type=\"button\" id=\"REBOOT\" onclick=\"if(confirm('Reboot transmitter?'))ButtonReboot()\" "
                  "style=\"background:#b91c1c\" value=\"REBOOT\"></div>");
    buildProjectInfoDiv();                        
    appendHtmlStr(OTA_POLL_SCRIPT);
    appendHtmlStr(webHTMLEnd);
    sendHtmlPage(request);
}
