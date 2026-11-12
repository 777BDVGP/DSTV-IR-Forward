#pragma once

// =============================================================================
// WebUtil.h — small helpers shared by the web page modules
// =============================================================================

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Collects a POST body into request->_tempObject (malloc'd, freed by the
// request destructor). Use as the onBody handler of server.on(...).
inline void CollectBody(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                        size_t index, size_t total) {
    if (total > 32768) return;
    if (index == 0) {
        req->_tempObject = malloc(total + 1);
        if (req->_tempObject) ((char *)req->_tempObject)[0] = '\0';
    }
    char *buf = (char *)req->_tempObject;
    if (!buf || index + len > total) return;
    memcpy(buf + index, data, len);
    buf[index + len] = '\0';
}

inline const char *RequestBody(AsyncWebServerRequest *req) {
    return req->_tempObject ? (const char *)req->_tempObject : "";
}

inline String JsonEscape(const String &s) {
    String out;
    out.reserve(s.length() + 4);
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if ((uint8_t)c < 0x20) out += ' ';
        else out += c;
    }
    return out;
}

inline String ArgStr(AsyncWebServerRequest *req, const char *name) {
    return req->hasArg(name) ? req->arg(name) : String();
}

inline long ArgInt(AsyncWebServerRequest *req, const char *name, long def) {
    return req->hasArg(name) ? req->arg(name).toInt() : def;
}

void AddSequenceRoutes(AsyncWebServer &server);   // WebSequences.cpp
void AddFileRoutes(AsyncWebServer &server);       // WebFiles.cpp
void AddSettingsRoutes(AsyncWebServer &server);   // WebSettings.cpp
void SetupSettings();                             // WebSettings.cpp — load /AllParams.json
String SysName();                                 // WebSettings.cpp — shown in page headers
void SaveParams();                                // WebSettings.cpp — rewrite /AllParams.json
bool FwdEnabled();                                // WebSettings.cpp — DSTV forwarding on
bool FwdEchoGuard();                              // WebSettings.cpp — ignore frames during own IR send
void AddAboutRoutes(AsyncWebServer &server);      // WebAbout.cpp
void AddUpdateRoutes(AsyncWebServer &server);     // WebUpdate.cpp — /update firmware upload
extern const char WEB_STYLE_CSS[];                // WebPage.cpp
