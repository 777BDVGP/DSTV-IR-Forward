// =============================================================================
// Sequencer.cpp — plays recorded sequences on the IR LED
// =============================================================================

#include <Arduino.h>
#include "Sequencer.h"
#include "SeqStore.h"
#include "IrTransmit.h"
#include "Status.h"

#define SEQ_RUN_QUEUE_DEPTH  4
#define SEQ_SEND_WAIT_MS     3000   // max wait for one stroke to leave the IR LED

struct RunRequest {
    uint16_t ids[SEQ_MAX_PER_RUN];
    uint8_t  count;
    uint32_t pauseMs[SEQ_MAX_PER_RUN];   // after ids[k]
    uint8_t  times[SEQ_MAX_PER_RUN];      // runs of ids[k] in a row
    uint32_t everyMs[SEQ_MAX_PER_RUN];    // ms between those runs (0 = last step delay)
    char     source[48];
};

static QueueHandle_t runQueue = NULL;
static portMUX_TYPE  textMux  = portMUX_INITIALIZER_UNLOCKED;
static char          runningText[96] = "";

static void setRunning(const String &text) {
    portENTER_CRITICAL(&textMux);
    strlcpy(runningText, text.c_str(), sizeof(runningText));
    portEXIT_CRITICAL(&textMux);
}

// -----------------------------------------------------------------------------
// PlaySequence — true if every stroke was sent
// -----------------------------------------------------------------------------
static bool PlaySequence(uint16_t id, uint8_t times, uint32_t everyMs) {
    Sequence seq;
    if (!SeqLoad(id, seq) || seq.steps.empty()) {
        StatusLog("Sequence #" + String(id) + " not found - skipped");
        return false;
    }
    if (times < 1) times = 1;
    // Between repeats: everyMs from the schedule, else the delay of the last
    // step (unused otherwise); min. 100 ms
    uint32_t repeatGapMs = max(everyMs ? everyMs : seq.steps.back().delayMs, (uint32_t)100);
    bool ok = true;
    for (uint8_t r = 0; r < times; r++) {
        if (r) vTaskDelay(pdMS_TO_TICKS(repeatGapMs));
        String rep = times > 1 ? "  (" + String(r + 1) + "/" + String(times) + ")" : String("");
        for (size_t i = 0; i < seq.steps.size(); i++) {
            const SeqStep &st = seq.steps[i];
            setRunning(seq.name + rep + "  step " + String(i + 1) + "/" + String(seq.steps.size()) +
                       "  " + st.label);
            if (!SendIrCode(st.code, SEQ_SEND_WAIT_MS)) {
                StatusLog("Sequence '" + seq.name + "' step " + String(i + 1) + " FAILED to send");
                ok = false;
            }
            if (i + 1 < seq.steps.size()) vTaskDelay(pdMS_TO_TICKS(st.delayMs));
        }
    }
    return ok;
}

static void SeqTask(void *pvParameters) {
    RunRequest req;
    for (;;) {
        if (xQueueReceive(runQueue, &req, portMAX_DELAY) != pdTRUE) continue;

        String names;
        for (uint8_t i = 0; i < req.count; i++) {
            if (i) names += " + ";
            String n = SeqName(req.ids[i]);
            names += n.length() ? n : ("#" + String(req.ids[i]));
            if (req.times[i] > 1) names += " x" + String(req.times[i]) +
                                           (req.everyMs[i] ? " every " + String(req.everyMs[i]) + " ms" : String(""));
        }
        StatusLog("Run " + names + "  (" + req.source + ")");

        bool ok = true;
        for (uint8_t i = 0; i < req.count; i++) {
            if (i && req.pauseMs[i - 1]) {
                setRunning("pause " + String(req.pauseMs[i - 1]) + " ms before next sequence");
                vTaskDelay(pdMS_TO_TICKS(req.pauseMs[i - 1]));
            }
            ok &= PlaySequence(req.ids[i], req.times[i], req.everyMs[i]);
        }
        setRunning("");
        StatusLog("Run " + names + (ok ? " done" : " finished WITH ERRORS"));
    }
}

void SetupSequencer() {
    runQueue = xQueueCreate(SEQ_RUN_QUEUE_DEPTH, sizeof(RunRequest));
    xTaskCreate(SeqTask, "SeqTask", 8192, NULL, 3, NULL);
}

bool SeqRun(const std::vector<uint16_t> &ids, const std::vector<uint32_t> &pausesMs,
            const std::vector<uint8_t> &times, const std::vector<uint32_t> &everyMs,
            const String &source) {
    if (!runQueue || ids.empty()) return false;
    RunRequest req = {};
    for (uint16_t id : ids) {
        if (req.count >= SEQ_MAX_PER_RUN) break;
        req.pauseMs[req.count] = req.count < pausesMs.size() ? pausesMs[req.count] : 0;
        req.times[req.count]   = req.count < times.size() && times[req.count] ? times[req.count] : 1;
        req.everyMs[req.count] = req.count < everyMs.size() ? everyMs[req.count] : 0;
        req.ids[req.count++]   = id;
    }
    strlcpy(req.source, source.c_str(), sizeof(req.source));
    return xQueueSend(runQueue, &req, 0) == pdTRUE;
}

bool SeqTestStep(uint16_t id, uint16_t step) {
    Sequence seq;
    if (!SeqLoad(id, seq) || step >= seq.steps.size()) return false;
    return SendIrCode(seq.steps[step].code);
}

String SeqRunningText() {
    char copy[sizeof(runningText)];
    portENTER_CRITICAL(&textMux);
    memcpy(copy, runningText, sizeof(copy));
    portEXIT_CRITICAL(&textMux);
    return String(copy);
}
