// =============================================================================
// IrCode.cpp — IrCode helpers
// =============================================================================

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>
#include "IrCode.h"

String IrCode::describe() const {
    String proto = typeToString((decode_type_t)protocol);
    char buf[64];
    if (format == FMT_XMP) {
        snprintf(buf, sizeof(buf), "DSTV XMP 0x%llX", (unsigned long long)value);
        return String(buf);
    }
    if (protocol != UNKNOWN && bits > 0 && bits <= 64) {
        snprintf(buf, sizeof(buf), " 0x%llX", (unsigned long long)value);
        return proto + buf + " (" + String(raw.size()) + " durations)";
    }
    return proto + " (" + String(raw.size()) + " durations)";
}

String IrCode::rawToText() const {
    String s;
    s.reserve(raw.size() * 5);
    for (size_t i = 0; i < raw.size(); i++) {
        if (i) s += ',';
        s += raw[i];
    }
    return s;
}

bool IrCode::rawFromText(const char *text) {
    raw.clear();
    if (!text) return false;
    const char *p = text;
    while (*p && raw.size() < IR_CODE_MAX_RAW) {
        char *end;
        unsigned long v = strtoul(p, &end, 10);
        if (end == p) break;
        raw.push_back(v > 65535 ? 65535 : (uint16_t)v);
        p = (*end == ',') ? end + 1 : end;
    }
    return !raw.empty();
}

// -----------------------------------------------------------------------------
// IrCodeFromSpec — build a 32-bit SAMSUNG or NEC frame (one frame, no repeat)
// Timings as IRremoteESP8266 sends them: 560 µs tick, bit = mark 560 +
// space 1680 (1) / 560 (0), MSB first, then a 560 µs footer mark.
// -----------------------------------------------------------------------------
bool IrCodeFromSpec(const String &specIn, IrCode &code, String &err) {
    String spec = specIn;
    spec.trim();
    spec.toUpperCase();
    int    colon = spec.indexOf(':');
    String proto = colon < 0 ? String("") : spec.substring(0, colon);
    String hex   = colon < 0 ? String("") : spec.substring(colon + 1);
    hex.trim();
    if (hex.startsWith("0X")) hex = hex.substring(2);
    bool hexOk = hex.length() >= 1 && hex.length() <= 8;
    for (size_t i = 0; hexOk && i < hex.length(); i++) hexOk = isHexadecimalDigit(hex[i]);
    if (!hexOk) { err = "The code must be 1 to 8 hex digits, e.g. E0E09966"; return false; }

    uint16_t hdrMark, hdrSpace, proto_id;
    if      (proto == "SAMSUNG") { hdrMark = 4480; hdrSpace = 4480; proto_id = SAMSUNG; }
    else if (proto == "NEC")     { hdrMark = 8960; hdrSpace = 4480; proto_id = NEC; }
    else { err = "Unknown protocol '" + proto + "' (SAMSUNG or NEC)"; return false; }

    const uint16_t kBitMark = 560, kOneSpace = 1680, kZeroSpace = 560;
    uint32_t value = strtoul(hex.c_str(), NULL, 16);
    code          = IrCode();
    code.format   = FMT_RAW;
    code.protocol = proto_id;
    code.bits     = 32;
    code.value    = value;
    code.raw.reserve(2 + 64 + 1);
    code.raw.push_back(hdrMark);
    code.raw.push_back(hdrSpace);
    for (int b = 31; b >= 0; b--) {
        code.raw.push_back(kBitMark);
        code.raw.push_back((value >> b) & 1 ? kOneSpace : kZeroSpace);
    }
    code.raw.push_back(kBitMark);
    return true;
}
