#pragma once
// =============================================================================
// RemoteFilter.h — "Forward remote" setting: WHICH remote is passed on
// =============================================================================
// SAME FILE in TV_IR_Sending/src and TV_IR_IN_ONE/src — keep in sync.
//
// The setting is a short text:
//   "XMP"          DSTV (default) — every XMP frame, as before
//   "NEC"          every NEC frame, any address
//   "NEC 0x20DF"   only NEC frames with that address (one box's remote)
// Names are the IRremoteESP8266 protocol names (NEC, SAMSUNG, RC5, RC6, SONY,
// PANASONIC, SHARP, JVC, ...). "Learn remote" fills it in from the next button
// press (RemoteFilterLearn). An old DSTV address ("0x...") or blank = "XMP".
//
// NEC sends a short "repeat" frame while a button is held: it has no address,
// so it is accepted when it follows a matching frame (volume up held down).
// =============================================================================

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

struct RemoteFilter {
	decode_type_t proto   = XMP;
	bool          anyAddr = true;
	uint32_t      addr    = 0;
};

static const uint32_t kRemoteRepeatMs = 300;   // NEC repeats come every ~108 ms

// Text → filter. false = not understood (unknown protocol / bad address).
inline bool RemoteFilterParse(String s, RemoteFilter &out) {
	s.trim();
	s.toUpperCase();
	if (s.isEmpty() || s.startsWith("0X")) { out = RemoteFilter(); return true; }
	int sp = s.indexOf(' ');
	String name = sp < 0 ? s : s.substring(0, sp);
	if (name.length() && isDigit(name[0])) return false;   // no protocol numbers
	decode_type_t t = strToDecodeType(name.c_str());
	if (t == UNKNOWN || t == UNUSED) return false;
	RemoteFilter f;
	f.proto = t;
	if (sp >= 0 && t != XMP) {
		String a = s.substring(sp + 1);
		a.trim();
		if (a.length()) {
			char *end = nullptr;
			unsigned long v = strtoul(a.c_str(), &end, 0);
			if (!end || *end) return false;
			f.anyAddr = false;
			f.addr    = (uint32_t)v;
		}
	}
	out = f;
	return true;
}

// Filter → text, as stored in the settings ("NEC 0x20DF")
inline String RemoteFilterText(const RemoteFilter &f) {
	String s = typeToString(f.proto);
	if (!f.anyAddr) {
		char buf[16];
		snprintf(buf, sizeof(buf), " 0x%lX", (unsigned long)f.addr);
		s += buf;
	}
	return s;
}

// Text for the web pages: "XMP (DSTV)", "NEC 0x20DF"
inline String RemoteFilterLabel(const RemoteFilter &f) {
	return f.proto == XMP ? String("XMP (DSTV)") : RemoteFilterText(f);
}

// A button press → setting text; "" = this remote cannot be used
// (not recognised, or only a repeat frame — press the button shortly)
inline String RemoteFilterLearn(const decode_results &r) {
	if (r.decode_type == UNKNOWN || r.decode_type == UNUSED) return "";
	if (r.decode_type == XMP) return "XMP";
	if (r.repeat) return "";
	RemoteFilter f;
	f.proto   = r.decode_type;
	f.anyAddr = r.address == 0;    // protocols without an address: any
	f.addr    = r.address;
	return RemoteFilterText(f);
}

// Is this frame from the remote to forward? lastMs: time of the last match
// (one variable per receiver), used for NEC "button held" repeat frames.
inline bool RemoteFilterMatch(const RemoteFilter &f, const decode_results &r, uint32_t &lastMs) {
	if (r.decode_type != f.proto) return false;
	if (f.proto == XMP) return true;                 // DSTV: as before
	uint32_t now = millis();
	if (r.repeat && r.address == 0) {
		if (now - lastMs > kRemoteRepeatMs) return false;
	} else if (!f.anyAddr && r.address != f.addr) {
		return false;
	}
	lastMs = now;
	return true;
}
