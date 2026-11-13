// =============================================================================
// 09_IR.cpp — IR Receive, Forward-remote filter (DSTV by default) & Forwarding
// =============================================================================

#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>
#include "all_includes.h"
#include "RemoteFilter.h"

// 1024 durations: room for long TV / sound bar / aircon frames when recording a
// sequence for the RX (the library default of 100 is enough for DSTV only).
static IRrecv         irrecv(IPIN_IR_RECEIVE, 1024, kTimeoutMs, false);
static decode_results results;

// -----------------------------------------------------------------------------
// Forward remote — which remote goes to the RX (RemoteFilter.h)
// -----------------------------------------------------------------------------
static RemoteFilter fwdFilter;                        // default XMP (DSTV)
static portMUX_TYPE fwdMux     = portMUX_INITIALIZER_UNLOCKED;
static uint32_t     fwdMatchMs = 0;                   // last match (NEC repeat frames)

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

// -----------------------------------------------------------------------------
// LearnRemote — Settings "Learn remote": the next button press sets the remote
// -----------------------------------------------------------------------------
static void LearnRemote(decode_results *r) {
    String txt = RemoteFilterLearn(*r);
    if (txt.isEmpty()) {
        if (!r->repeat) SaveGenReport("Learn remote: this IR code is not recognised - try another button or remote");
        return;
    }
    IrSetForwardRemote(txt);
    Params->ForwardRemote = IrForwardRemoteText();
    Params->LearnRemote   = false;      // done - the Settings page shows it unticked
    Params->WriteData();
    SaveGenReport("Learn remote: now forwarding " + IrForwardRemoteLabel());
}

// -----------------------------------------------------------------------------
// AppendIRLog
// -----------------------------------------------------------------------------
static void AppendIRLog(decode_results *r) {
    File lf = SPIFFS.open(StrIRLog, "a");
    if (!lf) return;
    char buf[120];
    uint32_t valHi = (uint32_t)(r->value >> 32);
    uint32_t valLo = (uint32_t)(r->value & 0xFFFFFFFF);
    snprintf(buf, sizeof(buf), "Proto:%d Val:%08X%08X Addr:%08X Cmd:%08X Bits:%d\n",
             (int)r->decode_type, valHi, valLo, r->address, r->command, r->bits);
    lf.print(buf);
    lf.close();
}

// -----------------------------------------------------------------------------
// RecoverXmp — second-chance XMP decode for frames the library calls UNKNOWN
// -----------------------------------------------------------------------------
// The library matches each XMP mark/space separately with only ±67 µs on the
// space. The TSOP stretches the very short 210 µs marks (and shortens the
// following space by the same amount), so many genuine DSTV frames fail and
// come out as UNKNOWN. Here each nibble is read from the full mark+space
// PERIOD (970 + 135·value µs), which that stretch does not change, and the
// result is only accepted if both XMP section checksums are correct.
// Layout: 8 × (mark, space)  mark  ~13 ms gap  8 × (mark, space)  mark
// -----------------------------------------------------------------------------
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
// SetupIR
// -----------------------------------------------------------------------------
void SetupIR() {
    irrecv.setTolerance(25);
    irrecv.setUnknownThreshold(12);   // shorter unknown bursts = noise (as on the RX)
    irrecv.enableIRIn(true);  // keep internal pull-up — TSOP31238 output is open-collector
    if (!IrSetForwardRemote(Params->ForwardRemote)) {
        SaveGenReport("Forward remote '" + Params->ForwardRemote + "' not understood - forwarding DSTV (XMP)");
        IrSetForwardRemote("XMP");
    }
}

// -----------------------------------------------------------------------------
// IRLoop  (called every 10 ms from loop())
// IRrecv captures in its ISR, so a frame arriving between calls is not lost.
// -----------------------------------------------------------------------------
void IRLoop() {
    if (!irrecv.decode(&results)) return;
    RecoverXmp(&results);   // UNKNOWN that is really a (stretched) DSTV frame → XMP

    // Only the "Forward remote" (DSTV by default) goes to the TV IR RX. No
    // local IR repeat on IR OUT — with TSOP and IR LED on one PCB that would
    // feed back.
    RemoteFilter filter = CurrentFilter();
    bool learning = Params->LearnRemote;
    bool isOurs   = !learning && RemoteFilterMatch(filter, results, fwdMatchMs);
    if (EspNowRecActive()) {
        // Recording a sequence for the RX (page /recseq): every remote (TV and
        // sound bar too, not only DSTV) is forwarded, so the RX plays it on its
        // IR output and you can see it work, AND goes to the recording.
        // Raw frames over IRLINK_MAX_RAW durations are too long to forward.
        if (NORMAL_OPERATION) EspNowForwardIr(&results);
        EspNowRecStroke(&results);
    } else {
        bool forward = NORMAL_OPERATION && isOurs;
        if (forward) EspNowForwardIr(&results);
    }

    // LEDs show only the code type: forward remote → GREEN (GPIO 5), anything else → RED (GPIO 4)
    if (isOurs) BoardFlashGreen();
    else        BoardFlashRed();

    if (Params->LearnMode) {
        AppendIRLog(&results);
    }

    if (learning && !EspNowRecActive()) {
        LearnRemote(&results);
    }
    irrecv.resume();
}
