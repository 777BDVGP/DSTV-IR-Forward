#pragma once

// =============================================================================
// IrLink.h — ESP-NOW packet format shared by TV IR TX nodes and the TV IR RX
// =============================================================================
// SHARED FILE: copy this file unchanged into TV_IR_Sending/include (TX).
// Both sides must use the same IRLINK_VERSION.
//
// Flow:
//   RX → broadcast   PKT_BEACON     every 2 s  ("I am the receiver", lets TX
//                                               learn the RX MAC automatically)
//   TX → RX unicast  PKT_IR_FRAME   per DSTV frame received by the TSOP
//   TX → RX unicast  PKT_HEARTBEAT  every 30 s (room name, TX link stats)
//
// Recording a sequence from the TX web page (v1.6.0). The remote is pointed at
// the TX; the RX keeps the recording and saves the sequence in its own list:
//   TX → RX unicast  PKT_REC_CMD     start / next / finish / retry / cancel /
//                                    test, and a status poll every 1 s while
//                                    the TX page is open
//   TX → RX unicast  PKT_REC_STROKE  the captured IR stroke, 1..11 parts
//   RX → broadcast   PKT_REC_STATUS  answer to every PKT_REC_CMD and when a
//                                    stroke was taken
// New types were added without changing IRLINK_VERSION: older boards ignore
// them, so normal forwarding keeps working between old and new firmware.
// With several TX rooms the recording belongs to the TX that started it:
// only its strokes and Next / Accept / Re-record / Test are taken (any room
// may Cancel), and only that TX forwards every remote while it lasts.
// =============================================================================

#include <stdint.h>

#define IRLINK_MAGIC        0x4B4C   // "LK"
#define IRLINK_VERSION      1
#define IRLINK_NAME_LEN     16       // room / device name incl. terminating 0
#define IRLINK_MAX_RAW      100      // max raw durations in one IR frame packet

#define IRLINK_BEACON_MS     2000UL
#define IRLINK_HEARTBEAT_MS  30000UL
#define IRLINK_OFFLINE_MS    (3 * IRLINK_HEARTBEAT_MS + 5000UL)

enum IrLinkType : uint8_t {
    PKT_BEACON    = 1,
    PKT_IR_FRAME  = 2,
    PKT_HEARTBEAT = 3,
    PKT_REC_CMD    = 4,
    PKT_REC_STROKE = 5,
    PKT_REC_STATUS = 6,
};

// How the RX must regenerate the IR signal
enum IrLinkFormat : uint8_t {
    FMT_XMP = 1,   // DSTV XMP: rebuilt from 'value'/'bits' — clean, exact timing
    FMT_RAW = 2,   // any other protocol: replay 'raw[]' mark/space durations (µs)
};

struct __attribute__((packed)) IrLinkHeader {
    uint16_t magic;     // IRLINK_MAGIC
    uint8_t  version;   // IRLINK_VERSION
    uint8_t  type;      // IrLinkType
};

struct __attribute__((packed)) IrLinkBeacon {
    IrLinkHeader hdr;
    char     name[IRLINK_NAME_LEN];
    uint32_t irSent;        // IR codes the RX has played out on its IR LED
};                          // (added later: TX also accepts the shorter old beacon)

// Size of the original beacon without irSent (older RX firmware)
#define IRLINK_BEACON_V1_SIZE  (sizeof(IrLinkHeader) + IRLINK_NAME_LEN)

struct __attribute__((packed)) IrLinkHeartbeat {
    IrLinkHeader hdr;
    char     room[IRLINK_NAME_LEN];
    uint32_t uptimeSec;
    int8_t   wifiRssi;      // TX's RSSI to the access point (dBm)
    uint32_t sentOk;        // ESP-NOW frames acknowledged by RX
    uint32_t sentFail;      // ESP-NOW frames not acknowledged
};

struct __attribute__((packed)) IrLinkIrFrame {
    IrLinkHeader hdr;
    char     room[IRLINK_NAME_LEN];
    uint16_t seq;           // increments per frame — RX drops duplicates
    uint8_t  format;        // IrLinkFormat
    uint16_t protocol;      // IRremoteESP8266 decode_type_t (for logging)
    uint16_t bits;
    uint64_t value;
    uint16_t rawCount;      // valid entries in raw[] (FMT_RAW only)
    uint16_t raw[IRLINK_MAX_RAW];   // mark, space, mark, space ... in µs
};

// Bytes actually transmitted for an IR frame (raw[] is trimmed to rawCount)
static inline uint16_t IrLinkFrameSize(uint16_t rawCount) {
    return sizeof(IrLinkIrFrame) - sizeof(uint16_t) * (IRLINK_MAX_RAW - rawCount);
}

static_assert(sizeof(IrLinkIrFrame) <= 250, "ESP-NOW payload limit is 250 bytes");

// =============================================================================
// Recording a sequence from the TX web page
// =============================================================================
enum IrLinkRecCmdCode : uint8_t {
    RECCMD_STATUS = 0,   // only asks for a PKT_REC_STATUS
    RECCMD_START  = 1,   // text = sequence name
    RECCMD_NEXT   = 2,   // keep the stroke (text = label, delayMs), wait for the next
    RECCMD_FINISH = 3,   // keep the stroke if any (text = label, delayMs) and save
    RECCMD_RETRY  = 4,   // drop the stroke, wait for it again
    RECCMD_CANCEL = 5,
    RECCMD_TEST   = 6,   // RX sends the received stroke from its IR LEDs
};

enum IrLinkRecState : uint8_t {   // same values as the RX RecState
    RECST_IDLE    = 0,
    RECST_WAIT_IR = 1,
    RECST_GOT_IR  = 2,
};

#define IRLINK_REC_TEXT_LEN   33    // name / label: 32 characters + 0
#define IRLINK_REC_PART_RAW   100   // raw durations per stroke part
#define IRLINK_REC_MAX_PARTS  11    // 11 x 100 >= 1024 durations (long aircon frames)

struct __attribute__((packed)) IrLinkRecCmd {
    IrLinkHeader hdr;
    uint16_t cmdSeq;     // never 0; RX answers with it in ackSeq. A repeat of
                         // the last cmdSeq is answered but not executed again.
    uint8_t  cmd;        // IrLinkRecCmdCode
    uint32_t delayMs;    // NEXT / FINISH: wait after this stroke
    char     text[IRLINK_REC_TEXT_LEN];
};

struct __attribute__((packed)) IrLinkRecStroke {
    IrLinkHeader hdr;
    uint16_t strokeId;   // same for every part of one stroke
    uint8_t  part;       // 0 .. parts-1
    uint8_t  parts;
    uint8_t  format;     // IrLinkFormat
    uint16_t protocol;
    uint16_t bits;
    uint64_t value;
    uint16_t rawTotal;   // durations in the whole stroke (FMT_RAW)
    uint16_t rawCount;   // durations in this part
    uint16_t raw[IRLINK_REC_PART_RAW];
};

static inline uint16_t IrLinkRecStrokeSize(uint16_t rawCount) {
    return sizeof(IrLinkRecStroke) - sizeof(uint16_t) * (IRLINK_REC_PART_RAW - rawCount);
}

struct __attribute__((packed)) IrLinkRecStatus {
    IrLinkHeader hdr;
    uint16_t ackSeq;     // cmdSeq of the last command handled (0 = none yet)
    uint8_t  ok;         // that command succeeded
    uint8_t  state;      // IrLinkRecState
    uint8_t  steps;      // strokes kept so far
    uint32_t rxIp;       // RX web page address (IPv4, as uint32_t from IPAddress)
    char     name[IRLINK_REC_TEXT_LEN];       // sequence being recorded
    char     pending[48];                     // received stroke, e.g. "NEC 0x20DF10EF"
    char     lastStep[IRLINK_REC_TEXT_LEN];   // label of the last kept stroke
    char     msg[64];                         // result / hint text for the TX page
    uint8_t  owner[6];   // MAC of the TX that started the recording; all 0 =
                         // started on the RX page. Other TX rooms stay normal.
};

static_assert(sizeof(IrLinkRecCmd)    <= 250, "ESP-NOW payload limit is 250 bytes");
static_assert(sizeof(IrLinkRecStroke) <= 250, "ESP-NOW payload limit is 250 bytes");
static_assert(sizeof(IrLinkRecStatus) <= 250, "ESP-NOW payload limit is 250 bytes");
