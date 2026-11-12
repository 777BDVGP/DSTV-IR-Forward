#pragma once

// =============================================================================
// WebPage.h — Status web page (http://<board ip>/)
// =============================================================================
//   /             status page (refreshes every 2 s)
//   /sequences    record / run / edit IR sequences   (WebSequences.cpp)
//   /schedule     weekly schedule of sequences       (WebSchedule.cpp)
//   /settings     System Name shown in the headers   (WebSettings.cpp)
//   /files        LittleFS file manager / backup     (WebFiles.cpp)
//   /about        OTA info, reboot, build info       (WebAbout.cpp)
//   /common.js    shared header (name, time, tabs) on every page
//   /api/status   JSON status
//   /api/resend   POST — replay last received IR frame
//   /api/reboot   POST — restart the board
// =============================================================================

void SetupWebPage();
extern volatile bool RebootRequested;

// OTA progress shown on /about (set by the ArduinoOTA callbacks in main.cpp)
//   OtaPercent  -1 = idle ("Ready to Update"), 0..100 = uploading
//   OtaError    -1 = none, else ota_error_t code of the last failed update
extern volatile int OtaPercent;
extern volatile int OtaError;
