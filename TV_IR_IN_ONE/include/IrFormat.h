#pragma once

// =============================================================================
// IrFormat.h — how a stored / queued IR code is regenerated on the IR LED
// =============================================================================
// (TV_IR_Sending / TV_IR_Receiving had these in the ESP-NOW IrLink.h; IN_ONE
//  has no radio link, only the format enum is kept.)
// =============================================================================

#include <stdint.h>

enum IrFormat : uint8_t {
    FMT_XMP = 1,   // DSTV XMP: rebuilt from value/bits — clean, exact timing
    FMT_RAW = 2,   // any other protocol: replay raw mark/space durations (µs)
};
