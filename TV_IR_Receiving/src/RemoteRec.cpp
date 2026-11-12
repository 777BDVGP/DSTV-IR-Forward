// =============================================================================
// RemoteRec.cpp — record a sequence from the TX web page
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include "IrLink.h"
#include "IrRecord.h"
#include "EspNowLink.h"
#include "RemoteRec.h"
#include "Status.h"

static uint16_t lastCmdSeq = 0;    // last command executed (repeats are only answered)
static bool     lastOk     = true;
static String   lastMsg;

// Stroke being put together from its parts
static bool     asmBusy  = false;
static uint16_t asmId    = 0;
static bool     asmDone  = false;  // asmId already complete: late repeats are ignored
static uint16_t asmMask  = 0;
static uint8_t  asmParts = 0;
static IrCode   asmCode;

static void copyText(char *dst, size_t size, const String &s) {
    strlcpy(dst, s.c_str(), size);
}

static String macText(const uint8_t *mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

// -----------------------------------------------------------------------------
static void sendStatus() {
    RecInfo ri = RecGetInfo();
    IrLinkRecStatus st = {};
    st.hdr.magic   = IRLINK_MAGIC;
    st.hdr.version = IRLINK_VERSION;
    st.hdr.type    = PKT_REC_STATUS;
    st.ackSeq      = lastCmdSeq;
    st.ok          = lastOk ? 1 : 0;
    st.state       = (uint8_t)ri.state;
    st.steps       = ri.steps > 255 ? 255 : ri.steps;
    st.rxIp        = (uint32_t)WiFi.localIP();
    copyText(st.name,     sizeof(st.name),     ri.name);
    copyText(st.pending,  sizeof(st.pending),  ri.pending);
    copyText(st.lastStep, sizeof(st.lastStep), ri.lastStep);
    copyText(st.msg,      sizeof(st.msg),      lastMsg);
    if (ri.hasOwner) memcpy(st.owner, ri.owner, 6);
    EspNowBroadcast(&st, sizeof(st));
}

// -----------------------------------------------------------------------------
static void handleCmd(const uint8_t *mac, const IrLinkRecCmd &c) {
    if (c.cmd == RECCMD_STATUS || c.cmdSeq == lastCmdSeq) {   // poll, or a repeat
        sendStatus();
        return;
    }
    char text[IRLINK_REC_TEXT_LEN];
    memcpy(text, c.text, sizeof(text));
    text[sizeof(text) - 1] = 0;
    uint32_t delayMs = c.delayMs > 60000 ? 60000 : c.delayMs;

    RecInfo before = RecGetInfo();
    String  err, okMsg;
    bool    otherRoom = before.hasOwner && memcmp(mac, before.owner, 6) != 0;
    if (otherRoom && c.cmd != RECCMD_START && c.cmd != RECCMD_CANCEL) {
        lastCmdSeq = c.cmdSeq;
        lastOk     = false;
        lastMsg    = "'" + before.name + "' is being recorded from another TX room";
        sendStatus();
        return;
    }
    switch (c.cmd) {
        case RECCMD_START:
            if (before.state != REC_IDLE) {
                err = "'" + before.name + "' is still being recorded - cancel it first";
                break;
            }
            err = RecStart(text);
            if (err.isEmpty()) {
                RecSetOwner(mac);
                StatusLog("Record '" + String(text) + "' controlled from TX " + macText(mac));
                okMsg = "Recording - press button 1 on the remote";
            }
            break;
        case RECCMD_NEXT:
            err   = RecNext(text, delayMs);
            okMsg = "Kept stroke " + String(before.steps + 1) + " - press button " +
                    String(before.steps + 2);
            break;
        case RECCMD_FINISH: {
            uint16_t id = 0;
            err = RecFinish(text, delayMs, id);
            uint16_t n = before.steps + (before.state == REC_GOT_IR ? 1 : 0);
            okMsg = "Saved '" + before.name + "' (" + String(n) + " strokes) on the receiver";
            break;
        }
        case RECCMD_RETRY:
            err   = RecRetry();
            okMsg = "Press the button again";
            break;
        case RECCMD_CANCEL:
            RecCancel();
            okMsg = "Recording cancelled";
            break;
        case RECCMD_TEST:
            err   = RecTestPending() ? "" : "Nothing to test - no stroke received";
            okMsg = "Sent from the receiver IR LEDs";
            break;
        default:
            err = "Unknown command - update the receiver firmware";
    }
    lastCmdSeq = c.cmdSeq;
    lastOk     = err.isEmpty();
    lastMsg    = lastOk ? okMsg : err;
    sendStatus();
}

// -----------------------------------------------------------------------------
static void handleStroke(const uint8_t *mac, const uint8_t *data, int len) {
    RecInfo ri = RecGetInfo();
    if (!ri.hasOwner || memcmp(mac, ri.owner, 6) != 0) return;   // only the recording TX
    if (len < (int)IrLinkRecStrokeSize(0)) return;
    IrLinkRecStroke p;
    memset(&p, 0, sizeof(p));
    memcpy(&p, data, min((size_t)len, sizeof(p)));
    if (p.parts == 0 || p.parts > IRLINK_REC_MAX_PARTS || p.part >= p.parts) return;
    if ((len - IrLinkRecStrokeSize(0)) / sizeof(uint16_t) < p.rawCount) return;   // cut short

    if (!asmBusy || p.strokeId != asmId) {          // first part of a new stroke
        asmBusy  = true;
        asmId    = p.strokeId;
        asmDone  = false;
        asmMask  = 0;
        asmParts = p.parts;
        asmCode  = IrCode();
        asmCode.format   = p.format;
        asmCode.protocol = p.protocol;
        asmCode.bits     = p.bits;
        asmCode.value    = p.value;
        if (p.format == FMT_RAW) {
            if (p.rawTotal == 0 || p.rawTotal > IR_CODE_MAX_RAW) { asmDone = true; return; }
            asmCode.raw.assign(p.rawTotal, 0);
        }
    }
    if (asmDone || p.parts != asmParts || (asmMask & (1 << p.part))) return;

    if (asmCode.format == FMT_RAW) {
        size_t off = (size_t)p.part * IRLINK_REC_PART_RAW;
        if (off + p.rawCount > asmCode.raw.size()) { asmDone = true; return; }
        memcpy(&asmCode.raw[off], p.raw, p.rawCount * sizeof(uint16_t));
    }
    asmMask |= 1 << p.part;
    if (asmMask != (uint16_t)((1u << asmParts) - 1)) return;

    asmDone = true;                                  // complete
    if (RecOfferStroke(asmCode)) {
        lastOk  = true;
        lastMsg = "Received - give it a label, then Next IR or Accept";
        sendStatus();
    }
}

// -----------------------------------------------------------------------------
void RemoteRecPacket(const uint8_t *mac, const uint8_t *data, int len) {
    if (len < (int)sizeof(IrLinkHeader)) return;
    IrLinkHeader hdr;
    memcpy(&hdr, data, sizeof(hdr));
    if (hdr.type == PKT_REC_CMD) {
        if (len < (int)sizeof(IrLinkRecCmd)) return;
        IrLinkRecCmd c;
        memcpy(&c, data, sizeof(c));
        handleCmd(mac, c);
    } else if (hdr.type == PKT_REC_STROKE) {
        handleStroke(mac, data, len);
    }
}
