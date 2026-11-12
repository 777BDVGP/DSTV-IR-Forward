#pragma once

// =============================================================================
// IrCode.h — one recorded IR stroke (any remote: DSTV, TV, sound bar, aircon)
// =============================================================================
// DSTV (XMP) strokes keep value/bits so IrTransmit rebuilds the exact XMP
// timing. Every other remote is kept as raw mark/space durations (µs) and
// replayed on the 38 kHz carrier.
// =============================================================================

#include <Arduino.h>
#include <vector>
#include "IrFormat.h"    // FMT_XMP / FMT_RAW

#define IR_CODE_MAX_RAW   1024   // durations per stroke (long aircon frames)

struct IrCode {
    uint8_t  format   = 0;       // FMT_XMP or FMT_RAW (0 = empty)
    uint16_t protocol = 0;       // IRremoteESP8266 decode_type_t (display only for RAW)
    uint16_t bits     = 0;
    uint64_t value    = 0;
    std::vector<uint16_t> raw;   // FMT_RAW: mark, space, mark ... (µs)

    bool   valid() const { return format == FMT_XMP || (format == FMT_RAW && !raw.empty()); }
    String describe() const;     // e.g. "XMP 0x...", "NEC 0x20DF10EF (68 durations)"

    // Compact text form for the JSON files: raw as "9000,4500,560,..."
    String rawToText() const;
    bool   rawFromText(const char *text);
};

// Known code typed in the sequence editor instead of recorded, e.g.
// "SAMSUNG:E0E09966" (Samsung TV Power ON) or "NEC:20DF10EF". The board builds
// the 32-bit frame itself; it is stored and played like a recorded raw stroke.
// false + err if the text is not a supported protocol / 1-8 hex digits.
bool IrCodeFromSpec(const String &spec, IrCode &code, String &err);
