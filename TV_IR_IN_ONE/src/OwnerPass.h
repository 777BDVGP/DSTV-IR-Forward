#pragma once

// =============================================================================
// OwnerPass.h — one owner password per board (no password in the code)
// =============================================================================
// SHARED FILE: identical copy in TV_IR_Sending/src, TV_IR_Receiving/src and
// TV_IR_IN_ONE/src — keep in sync.
//
// The owner chooses it on the setup page of a new board (or on /update of the
// All-in-One). It is needed to:
//   * change the WiFi settings (setup WiFi page and /wifi page)
//   * upload firmware over the network (pio ... -t upload: ArduinoOTA, and the
//     All-in-One /update page)
// Stored in NVS (namespace "upd"): "pwh" = SHA-256 (web pages), "pwm" = MD5
// (the hash ArduinoOTA uses). Never in a file, so not visible on the Files page.
//
// Own boards: an optional src/Private.h (never committed, see
// Private.example.h) may define OWNER_PASSWORD_DEFAULT; a board without a
// password gets it at boot (OwnerPassSeed, called from WifiStart).
// Forgotten: erase the flash over USB (pio run -e usb -t erase) and flash again.
// =============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include <MD5Builder.h>
#include <mbedtls/sha256.h>

#if __has_include("Private.h")
#include "Private.h"
#endif

#define OWNER_PW_MIN 6
#define OWNER_PW_MAX 63

inline String OwnerSha256Hex(const String &s) {
	uint8_t out[32];
	mbedtls_sha256((const unsigned char *)s.c_str(), s.length(), out, 0);
	char hex[65];
	for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", out[i]);
	return String(hex);
}

inline String OwnerMd5Hex(const String &s) {
	MD5Builder md5;
	md5.begin();
	md5.add(s);
	md5.calculate();
	return md5.toString();
}

inline String OwnerPrefGet(const char *key) {
	Preferences p;
	String v;
	if (p.begin("upd", true)) { v = p.getString(key, ""); p.end(); }
	return v;
}

inline bool   OwnerPassSet()                  { return OwnerPrefGet("pwh").length() == 64; }
inline bool   OwnerPassOk(const String &pw)   { String h = OwnerPrefGet("pwh"); return h.length() == 64 && OwnerSha256Hex(pw) == h; }
inline String OwnerPassMd5()                  { return OwnerPrefGet("pwm"); }   // "" = none (OTA open)

inline bool OwnerPassStore(const String &pw) {
	if (pw.length() < OWNER_PW_MIN || pw.length() > OWNER_PW_MAX) return false;
	Preferences p;
	if (!p.begin("upd", false)) return false;
	String h = OwnerSha256Hex(pw), m = OwnerMd5Hex(pw);
	bool ok = p.putString("pwh", h) == h.length() && p.putString("pwm", m) == m.length();
	p.end();
	return ok;
}

// Own boards: password from Private.h when the board has none yet.
// true = it was seeded now (caller logs it).
inline bool OwnerPassSeed() {
#ifdef OWNER_PASSWORD_DEFAULT
	if (!OwnerPassSet()) return OwnerPassStore(OWNER_PASSWORD_DEFAULT);
#endif
	return false;
}
