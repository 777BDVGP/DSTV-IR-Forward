// =============================================================================
// IrReceive.cpp — TSOP31238 receiver: live forwarding (DSTV or learnt remote) + record mode
// =============================================================================
// Forwarding and the LED rules come from TV_IR_Sending (09_IR.cpp, including
// the tolerant XMP decode RecoverXmp); record mode comes from TV_IR_Receiving
// (IrRecord.cpp). Both share one IRrecv on GPIO 1.
// =============================================================================

#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>
#include "Config.h"
#include "IrReceive.h"
#include "IrRecord.h"
#include "IrTransmit.h"
#include "Status.h"
#include "WebUtil.h"      // FwdEnabled(), FwdEchoGuard(), FwdLearning()
#include "RemoteFilter.h"

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

static portMUX_TYPE lastMux = portMUX_INITIALIZER_UNLOCKED;
static uint64_t     lastDstvValue = 0;
static bool         haveLastDstv  = false;
static char         lastOtherText[48] = "";   // last forwarded non-DSTV code
static bool         lastWasDstv   = true;

// "Forward remote" (Settings) — RemoteFilter.h; default XMP (DSTV)
static RemoteFilter fwdFilter;
static portMUX_TYPE fwdMux     = portMUX_INITIALIZER_UNLOCKED;
static uint32_t     fwdMatchMs = 0;            // last match (NEC repeat frames)

bool IrSetForwardRemote(const String &txt) {
    RemoteFilter f;
    if (!RemoteFilterParse(txt, f)) return false;
    portENTER_CRITICAL(&fwdMux);
    fwdFilter = f;
    portEXIT_CRITICAL(&fwdMux);
    return true;
}

static RemoteFilter CurrentFilter() {
    portENTER_CRITICAL(&fwdMux);
    RemoteFilter f = fwdFilter;
    portEXIT_CRITICAL(&fwdMux);
    return f;
}

String IrForwardRemoteText()  { return RemoteFilterText(CurrentFilter()); }
String IrForwardRemoteLabel() { return RemoteFilterLabel(CurrentFilter()); }

struct RecLock {
    RecLock()  { xSemaphoreTake(recMtx, portMAX_DELAY); }
    ~RecLock() { xSemaphoreGive(recMtx); }
};

// =============================================================================
// RecoverXmp — second-chance XMP decode for frames the library calls UNKNOWN
// (identical logic to TV_IR_Sending 09_IR.cpp)
// =============================================================================
// The library matches each XMP mark/space separately with only ±67 µs on the
// space. The TSOP stretches the very short 210 µs marks (and shortens the
// following space by the same amount), so many genuine DSTV frames fail and
// come out as UNKNOWN. Here each nibble is read from the full mark+space
// PERIOD (970 + 135·value µs), which that stretch does not change, and the
// result is only accepted if both XMP section checksums are correct.
// Layout: 8 × (mark, space)  mark  ~13 ms gap  8 × (mark, space)  mark
// =============================================================================
static const uint16_t kXmpPeriodBase = 970;    // 210 µs mark + 760 µs base space
static const uint16_t kXmpPeriodStep = 135;
static const uint16_t kXmpPeriodTol  = 55;     // < half a step (67 µs)

static bool XmpSectionOk(uint32_t section) {
    uint8_t sum = 0;
    for (int i = 0; i < 8; i++) sum += (section >> (4 * i)) & 0xF;
    uint8_t stored = (section >> 24) & 0xF;            // 2nd most significant nibble
    return stored == (uint8_t)(0xF & ~(0xF + sum - stored));
}

static bool RecoverXmp(decode_results *r) {
    const uint16_t kNeeded = 2 * 17 + 1;   // two sections of 17 + the gap space
    if (r->decode_type != UNKNOWN || r->rawlen < 1 + kNeeded) return false;

    uint64_t value = 0;
    uint16_t i = 1;                         // rawbuf[0] = gap before the frame
    for (int section = 0; section < 2; section++) {
        for (int nib = 0; nib < 8; nib++) {
            uint32_t period = (r->rawbuf[i] + r->rawbuf[i + 1]) * kRawTick;
            i += 2;
            if (period + kXmpPeriodTol < kXmpPeriodBase) return false;
            uint32_t v = (period - kXmpPeriodBase + kXmpPeriodStep / 2) / kXmpPeriodStep;
            if (v > 15) return false;
            int32_t err = (int32_t)period - (int32_t)(kXmpPeriodBase + v * kXmpPeriodStep);
            if (err > kXmpPeriodTol || err < -(int32_t)kXmpPeriodTol) return false;
            value = (value << 4) | v;
        }
        uint32_t footerMark = r->rawbuf[i++] * kRawTick;
        if (footerMark < 100 || footerMark > 600) return false;
        if (section == 0) {
            uint32_t gap = r->rawbuf[i++] * kRawTick;    // ~13 ms between sections
            if (gap < 10000 || gap > 17000) return false;
        }
    }
    if (!XmpSectionOk((uint32_t)(value >> 32)) || !XmpSectionOk((uint32_t)value)) return false;

    r->decode_type = XMP;
    r->value       = value;
    r->bits        = 64;
    return true;
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
// HandleFrame — one decoded frame from the TSOP
// -----------------------------------------------------------------------------
static void HandleFrame() {
    RecoverXmp(&results);   // UNKNOWN that is really a (stretched) DSTV frame → XMP
    bool learning = FwdLearning();
    bool isOurs   = !learning && RemoteFilterMatch(CurrentFilter(), results, fwdMatchMs);

    // Own IR LED: next door, so normally not seen. Echo guard (Settings) is
    // only for an installation where the TSOP can see the IR LED.
    bool ownTx = IrTxBusy() || millis() - IrTxLastActiveMs() < kOwnTxIgnoreMs;

    bool recording;
    {
        RecLock lock;
        recording = state != REC_IDLE;
        if (state == REC_WAIT_IR && !ownTx) {
            if (results.overflow) {
                StatusLog("Record: IR frame too long (> " + String(IR_CODE_MAX_RAW) + " durations) - ignored");
            } else {
                IrCode code;
                if (CodeFromResults(results, code)) {
                    pending      = code;
                    state        = REC_GOT_IR;
                    lastActionMs = millis();
                }
            }
        }
    }

    // LEDs show only the code type (as TV_IR_Sending): forward remote → GREEN, other → RED
    if (isOurs) BoardFlashGreen();
    else        BoardFlashRed();

    // Settings "Learn remote": the next button press becomes the forward remote
    if (learning && !recording && !ownTx) {
        String txt = RemoteFilterLearn(results);
        if (txt.length()) {
            IrSetForwardRemote(txt);
            FwdLearned(IrForwardRemoteText());
        } else if (!results.repeat) {
            StatusLog("Learn remote: this IR code is not recognised - try another button or remote");
        }
        return;
    }

    if (!isOurs || recording || !FwdEnabled()) return;
    if (ownTx && FwdEchoGuard()) return;

    if (results.decode_type == XMP) {          // DSTV: rebuilt as clean XMP
        QueueDstvCode(results.value, results.bits);
        portENTER_CRITICAL(&lastMux);
        lastDstvValue = results.value;
        haveLastDstv  = true;
        lastWasDstv   = true;
        portEXIT_CRITICAL(&lastMux);
        return;
    }
    IrCode code;                               // learnt remote: sent as raw timing
    if (results.overflow || !CodeFromResults(results, code)) return;
    SendIrCode(code);
    char buf[sizeof(lastOtherText)];
    snprintf(buf, sizeof(buf), "%s", code.describe().c_str());
    portENTER_CRITICAL(&lastMux);
    memcpy(lastOtherText, buf, sizeof(buf));
    haveLastDstv = true;
    lastWasDstv  = false;
    portEXIT_CRITICAL(&lastMux);
}

// -----------------------------------------------------------------------------
// IrReceiveTask — keeps the receiver drained
// -----------------------------------------------------------------------------
static void IrReceiveTask(void *pvParameters) {
    irrecv.setTolerance(25);
    irrecv.setUnknownThreshold(kMinUnknownSize);
    irrecv.enableIRIn(true);   // internal pull-up — TSOP31238 output is open-collector

    for (;;) {
        if (irrecv.decode(&results)) {
            HandleFrame();
            irrecv.resume();
        }

        {
            RecLock lock;
            if (state != REC_IDLE && millis() - lastActionMs > REC_TIMEOUT_MS) {
                state = REC_IDLE;
                StatusLog("Record '" + recSeq.name + "' abandoned (no action for 5 min) - DSTV forwarding resumed");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void SetupIrReceive() {
    recMtx = xSemaphoreCreateMutex();
    // Above the web server, below IrTxTask (5): forwarding must not wait on page loads
    xTaskCreate(IrReceiveTask, "IrReceiveTask", 8192, NULL, 4, NULL);
}

bool RecActive() {
    RecLock lock;
    return state != REC_IDLE;
}

String IrForwardStateText() {
    if (!FwdEnabled()) return "OFF (Settings)";
    {
        RecLock lock;
        if (state != REC_IDLE) return "PAUSED - recording a sequence";
    }
    return "Forwarding";
}

String IrLastDstvText() {
    char other[sizeof(lastOtherText)];
    portENTER_CRITICAL(&lastMux);
    bool     have  = haveLastDstv;
    bool     dstv  = lastWasDstv;
    uint64_t value = lastDstvValue;
    memcpy(other, lastOtherText, sizeof(other));
    portEXIT_CRITICAL(&lastMux);
    if (!have) return "";
    if (!dstv) return String(other);
    char buf[32];
    snprintf(buf, sizeof(buf), "XMP 0x%016llX", (unsigned long long)value);
    return String(buf);
}

// =============================================================================
// Record mode — web actions (IrRecord.h)
// =============================================================================
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
    pending      = IrCode();
    state        = REC_WAIT_IR;
    lastActionMs = millis();
    StatusLog("Record '" + n + "' started - DSTV forwarding paused");
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
    StatusLog("Sequence '" + seq.name + "' saved, " + String(seq.steps.size()) + " step(s) - DSTV forwarding resumed");
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
    if (state != REC_IDLE) StatusLog("Record '" + recSeq.name + "' cancelled - DSTV forwarding resumed");
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

String RecStatusJson() {
    RecLock lock;
    String j = "{\"state\":" + String((int)state);
    j += ",\"name\":\"" + JsonEscape(recSeq.name) + "\"";
    j += ",\"pending\":\"" + (state == REC_GOT_IR ? JsonEscape(pending.describe()) : String("")) + "\"";
    j += ",\"steps\":[";
    for (size_t i = 0; i < recSeq.steps.size(); i++) {
        const SeqStep &s = recSeq.steps[i];
        if (i) j += ",";
        j += "{\"label\":\"" + JsonEscape(s.label) + "\",\"delay\":" + String(s.delayMs) +
             ",\"code\":\"" + JsonEscape(s.code.describe()) + "\"}";
    }
    j += "]}";
    return j;
}
