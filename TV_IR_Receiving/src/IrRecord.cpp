// =============================================================================
// IrRecord.cpp — record mode: capture IR strokes from any remote on the RX TSOP
// =============================================================================

#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>
#include "Config.h"
#include "IrRecord.h"
#include "IrTransmit.h"
#include "Status.h"

// 50 ms gap ends a capture: XMP frames (80 ms apart) stay separate, while the
// multi-part frames of aircon remotes (~30 ms gaps) are captured as one stroke.
static const uint8_t  kCaptureTimeoutMs = 50;
static const uint32_t kOwnTxIgnoreMs    = 150;   // after our IR LED went quiet
static const uint16_t kMinUnknownSize   = 12;    // shorter unknown bursts = noise

static IRrecv          irrecv(BRD_PIN_IR_IN, IR_CODE_MAX_RAW, kCaptureTimeoutMs, true);
static decode_results  results;

static SemaphoreHandle_t recMtx = NULL;
static RecState  state = REC_IDLE;
static Sequence  recSeq;           // steps accepted so far
static IrCode    pending;          // stroke waiting for Next / Finish
static uint32_t  lastActionMs = 0;
static bool      ownerSet = false;   // recording started from a TX page
static uint8_t   ownerMac[6];

struct RecLock {
    RecLock()  { xSemaphoreTake(recMtx, portMAX_DELAY); }
    ~RecLock() { xSemaphoreGive(recMtx); }
};

static String jsonEscape(const String &s) {
    String out;
    out.reserve(s.length() + 4);
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if ((uint8_t)c < 0x20) out += ' ';
        else out += c;
    }
    return out;
}

// -----------------------------------------------------------------------------
// CodeFromResults — XMP keeps value/bits, everything else is kept raw
// -----------------------------------------------------------------------------
static bool CodeFromResults(const decode_results &r, IrCode &code) {
    code = IrCode();
    code.protocol = (uint16_t)r.decode_type;
    code.bits     = r.bits;
    code.value    = r.value;
    if (r.decode_type == XMP) {
        code.format = FMT_XMP;
        return true;
    }
    uint16_t n = getCorrectedRawLength(&r);
    if (n == 0 || n > IR_CODE_MAX_RAW) return false;
    uint16_t *raw = resultToRawArray(&r);   // µs durations, caller frees
    if (!raw) return false;
    code.raw.assign(raw, raw + n);
    delete[] raw;
    code.format = FMT_RAW;
    return true;
}

// -----------------------------------------------------------------------------
// IrRecordTask — keeps the receiver drained; captures only in WAIT_IR
// -----------------------------------------------------------------------------
static void IrRecordTask(void *pvParameters) {
    irrecv.setTolerance(25);
    irrecv.setUnknownThreshold(kMinUnknownSize);
    irrecv.enableIRIn(true);   // internal pull-up — TSOP31238 output is open-collector

    for (;;) {
        if (irrecv.decode(&results)) {
            bool ownTx = IrTxBusy() || millis() - IrTxLastActiveMs() < kOwnTxIgnoreMs;
            if (!ownTx && results.overflow) {
                StatusLog("Record: IR frame too long (> " + String(IR_CODE_MAX_RAW) + " durations) - ignored");
            } else if (!ownTx) {
                RecLock lock;
                if (state == REC_WAIT_IR) {
                    IrCode code;
                    if (CodeFromResults(results, code)) {
                        pending      = code;
                        state        = REC_GOT_IR;
                        lastActionMs = millis();
                    }
                }
            }
            irrecv.resume();
        }

        {
            RecLock lock;
            if (state != REC_IDLE && millis() - lastActionMs > REC_TIMEOUT_MS) {
                state = REC_IDLE;
                StatusLog("Record '" + recSeq.name + "' abandoned (no action for 5 min)");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void SetupIrRecord() {
    recMtx = xSemaphoreCreateMutex();
    xTaskCreate(IrRecordTask, "IrRecordTask", 8192, NULL, 2, NULL);
}

// -----------------------------------------------------------------------------
// Web actions
// -----------------------------------------------------------------------------
static void AcceptPending(const String &label, uint32_t delayMs) {
    SeqStep step;
    step.label   = label.length() ? label : ("Step " + String(recSeq.steps.size() + 1));
    step.delayMs = delayMs;
    step.code    = pending;
    recSeq.steps.push_back(step);
    pending = IrCode();
}

String RecStart(const String &name) {
    String n = name;
    n.trim();
    if (n.isEmpty())                   return "Enter a sequence name";
    if (n.length() > SEQ_NAME_LEN)     return "Name too long (max " + String(SEQ_NAME_LEN) + ")";
    if (SeqNameUsed(n))                return "A sequence called '" + n + "' already exists";
    if (SeqList().size() >= SEQ_MAX_SEQUENCES) return "Maximum number of sequences reached";

    RecLock lock;
    recSeq       = Sequence();
    recSeq.name  = n;
    ownerSet     = false;
    pending      = IrCode();
    state        = REC_WAIT_IR;
    lastActionMs = millis();
    StatusLog("Record '" + n + "' started");
    return "";
}

String RecNext(const String &label, uint32_t delayMs) {
    RecLock lock;
    if (state != REC_GOT_IR)                  return "No IR stroke received yet";
    if (recSeq.steps.size() >= SEQ_MAX_STEPS - 1) return "Maximum steps reached - Accept the sequence";
    AcceptPending(label, delayMs);
    state        = REC_WAIT_IR;
    lastActionMs = millis();
    return "";
}

String RecFinish(const String &label, uint32_t delayMs, uint16_t &savedId) {
    Sequence seq;
    {
        RecLock lock;
        if (state == REC_IDLE)                              return "Not recording";
        if (state == REC_GOT_IR) {
            AcceptPending(label, delayMs);
            state = REC_WAIT_IR;   // if saving fails the stroke is not added twice
        }
        if (recSeq.steps.empty())                           return "No IR stroke recorded yet";
        seq = recSeq;
    }
    savedId = SeqSave(seq);
    if (!savedId) return "Saving failed (storage full?)";

    RecLock lock;
    state = REC_IDLE;
    StatusLog("Sequence '" + seq.name + "' saved, " + String(seq.steps.size()) + " step(s)");
    return "";
}

String RecRetry() {
    RecLock lock;
    if (state == REC_IDLE) return "Not recording";
    pending      = IrCode();
    state        = REC_WAIT_IR;
    lastActionMs = millis();
    return "";
}

void RecCancel() {
    RecLock lock;
    if (state != REC_IDLE) StatusLog("Record '" + recSeq.name + "' cancelled");
    state = REC_IDLE;
}

bool RecTestPending() {
    IrCode code;
    {
        RecLock lock;
        if (state != REC_GOT_IR) return false;
        code         = pending;
        lastActionMs = millis();
    }
    return SendIrCode(code);
}

bool RecOfferStroke(const IrCode &code) {
    if (!code.valid()) return false;
    RecLock lock;
    if (state != REC_WAIT_IR) return false;
    pending      = code;
    state        = REC_GOT_IR;
    lastActionMs = millis();
    return true;
}

void RecSetOwner(const uint8_t *mac) {
    RecLock lock;
    memcpy(ownerMac, mac, 6);
    ownerSet = true;
}

RecInfo RecGetInfo() {
    RecLock lock;
    RecInfo ri;
    ri.hasOwner = state != REC_IDLE && ownerSet;
    memcpy(ri.owner, ownerMac, 6);
    ri.state    = state;
    ri.name     = state == REC_IDLE ? String("") : recSeq.name;
    ri.steps    = state == REC_IDLE ? 0 : recSeq.steps.size();
    ri.pending  = state == REC_GOT_IR ? pending.describe() : String("");
    ri.lastStep = (state != REC_IDLE && !recSeq.steps.empty()) ? recSeq.steps.back().label : String("");
    return ri;
}

String RecStatusJson() {
    RecLock lock;
    String j = "{\"state\":" + String((int)state);
    j += ",\"name\":\"" + jsonEscape(recSeq.name) + "\"";
    j += ",\"pending\":\"" + (state == REC_GOT_IR ? jsonEscape(pending.describe()) : String("")) + "\"";
    j += ",\"steps\":[";
    for (size_t i = 0; i < recSeq.steps.size(); i++) {
        const SeqStep &s = recSeq.steps[i];
        if (i) j += ",";
        j += "{\"label\":\"" + jsonEscape(s.label) + "\",\"delay\":" + String(s.delayMs) +
             ",\"code\":\"" + jsonEscape(s.code.describe()) + "\"}";
    }
    j += "]}";
    return j;
}
