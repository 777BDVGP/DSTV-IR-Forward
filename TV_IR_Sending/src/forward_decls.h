// =============================================================================
// forward_decls.h — Cross-File Function Forward Declarations
// =============================================================================
#pragma once

// ─── 01_Main.cpp ─────────────────────────────────────────────────────────────
void InitHardware();
void WatchdogSetup();
void ISRwatchdog();
void LEDToggle();

// ─── 02_Time.cpp ─────────────────────────────────────────────────────────────
void SetupTime();
void MaintainTimers();
void MaintainScheduledReboot();
void UpdateWebPageTime();
void CompileDateTime();
void reduce_file(String filename, int Entries);
void activateTimer(int index);
void deactivateTimer(int index);
bool TimerExpired(int index);
void SetValueAndAtivate(String Units, int TimerUnits, int index);
void SetTimerValue(int Units, int index);
int  RemainingSec(int index);
int  RemainingMin(int index);

String GetCurrentDateAndTime();
String GetCurrentDate(int daysOffset);
String GetCurrentTime();
String IncrementDateWithOneDay(String date);
String AddTimeToCurrentDateTime(String baseDateTime, String period);

// ─── 03_Files.cpp ─────────────────────────────────────────────────────────────
extern String FSInitLog;
void   InitFileSystem();
void   FilesPage(AsyncWebServerRequest *request);
void   FilePageSel(int Index);
void   handleApiFiles(AsyncWebServerRequest *request);
void   handleFileView(AsyncWebServerRequest *request);
void   handleFileDeleteJson(AsyncWebServerRequest *request);
void   File_Download(AsyncWebServerRequest *request);
void   CompilePage_Download(AsyncWebServerRequest *request);
void   append_Form_Download();
void   DownloadFile(AsyncWebServerRequest *request, String filename);
void   File_Upload(AsyncWebServerRequest *request);
void   handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void   File_Delete(AsyncWebServerRequest *request);
void   Compile_PageDelete(AsyncWebServerRequest *request);
void   append_Form_Delete();
void   SPIFFS_file_delete(AsyncWebServerRequest *request, String filename);
void   CreateDFileList(const char * dir);

// ─── 04_WebPages.cpp ──────────────────────────────────────────────────────────
bool   beginHtmlStream(AsyncWebServerRequest *request);
void   SetupWebPage();
void   MaintainWebPage();
void   webSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void   webParamUpdate(const String& Name, const String& ID, const String& ATT, const char* ATTName = "style");
void   UnpackJsonPayload(uint32_t clientId, uint8_t * payload, size_t length);
void   WebMessUpdate(uint32_t clientId, const String& Mess, const String& Att, bool Reset);
void   UpdateSelectMessDelay();
void   append_FileAction(int ReqMess);
void   WT_StartTable(const char* Hdr1, const char* Hdr2, const char* Hdr3);
void   WT_Column(int WT_Type, int WT_Begin, int WT_Finish, const char* Type, const char* Labval, const char* ID, const char* Style, PageVar *ListObj, const char* ExtraAttrs = "");
void   WT_AutoColumn(int WT_Type, int WT_Begin, int WT_Finish, PageVar *pv, const char* IDPrefix = "", const char* ExtraAttrs = "");
void   WT_DisplayRow(PageVar *pv);
void   WT_EditRow(PageVar *pv, const char* typeOverride = "", const char* ExtraAttrs = "");
void   WT_CheckEditRow(PageVar *cbPv, PageVar *valPv, const char* typeOverride = "");
void   WT_RightCheckRow(PageVar *pv);
void   WT_StartPage();
void   WT_EndPage();
void   WT_EndPageClass(const char* cls);
void   WT_Header(const char* Header);
void   WT_SectionHeader(const char* Header);
void   WT_StartLine();
void   WT_EndLine();
void   WT_StartLabel();
void   WT_StartLabelEmpty();
void   WT_StartInput();
void   WT_StartInputEmpty();
void   WT_NextColumn();
void   WT_EndCell();
void   WT_TypeCell(const char* Type);
void   WT_TextLabel(const char* Txt);
void   WT_ValueInput(const char* Val);
void   WT_IDCell(const char* ID);
void   WT_AttrsStart();
void   WT_Attr(const char* n, const char* v);
void   WT_AttrsEnd();
void   WT_OptionsStart();
void   WT_OptionsValues(const char* AVal);
void   WT_OptionsEnd();

// ─── 09_IR.cpp ────────────────────────────────────────────────────────────────
void SetupIR();
void IRLoop();

// ─── 05_ThisSystem.cpp ────────────────────────────────────────────────────────
void SetupSystemIOs();
void PinDebounce();
void MaintainBoard();
void MaintainRouter();
void MaintainWebPageValues();
void AddRoutes();

// ─── 08_Home.cpp ──────────────────────────────────────────────────────────────
void   handleRoot(AsyncWebServerRequest *request);
void   appendMainPageSel(int Index);
void   WT_HomePage();
void   MapHomePage();
void   SendHomePageValues();
void   MaintainHomePage();
void   webRebootButton();

// ─── 12_Settings.cpp ──────────────────────────────────────────────────────────
void   CreateAllParams();
void   MapSettingsPage();
void   handleSettings(AsyncWebServerRequest *request);
void   WT_SETUP_Settings();
void   SendSettingsPageValues();
void   UpdateSettingsPageValues(uint32_t clientId, JsonObject *root);
void   handleDisableSystem(AsyncWebServerRequest *request);
void   handleEnableSystem(AsyncWebServerRequest *request);
void   appendSettingsActionButtons();


// ─── 05_WiFi.cpp ──────────────────────────────────────────────────────────────
bool   SetupWiFi();

// ─── 15_About.cpp ─────────────────────────────────────────────────────────────
void   SetupAbout();
void   SetupOTA();
void   handleAbout(AsyncWebServerRequest *request);
void   handleApiOta(AsyncWebServerRequest *request);

// ─── main.cpp ─────────────────────────────────────────────────────────────────
void PrintTestMsg(String Message, uint32_t seconds);
