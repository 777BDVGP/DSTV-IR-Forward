// =============================================================================
// IrTransmit.cpp — IR LED output via the ESP32-S2 RMT peripheral
// =============================================================================

#include <Arduino.h>
#include "driver/rmt.h"
#include "Config.h"
#include "IrTransmit.h"
#include "Status.h"

// ── XMP timing — identical to IRremoteESP8266 ir_Xmp.cpp (sendXmp) ───────────
static const uint16_t kXmpMark        = 210;
static const uint16_t kXmpBaseSpace   = 760;
static const uint16_t kXmpSpaceStep   = 135;
static const uint16_t kXmpFooterSpace = 13000;
static const uint8_t  kXmpSections    = 2;

static const rmt_channel_t kRmtChannel = RMT_CHANNEL_0;
static const uint16_t      kRmtMaxTicks = 32767;              // 15-bit duration field
static const size_t        kMaxItems    = IR_CODE_MAX_RAW / 2 + 2;

// One queued transmission. raw is heap memory owned by the job (freed by
// IrTxTask); done (optional) is given once the job has been sent.
struct IrTxJob {
    uint8_t           format;
    uint16_t          bits;
    uint64_t          value;
    uint16_t          rawCount;
    uint16_t         *raw;
    SemaphoreHandle_t done;
};

static QueueHandle_t  irQueue = NULL;
static volatile uint32_t lastActiveMs = 0;
static volatile bool     txBusy       = false;
static uint64_t       lastValue     = 0;
static uint16_t       lastBits      = 0;
static bool           haveLast      = false;
static portMUX_TYPE   lastMux       = portMUX_INITIALIZER_UNLOCKED;
static uint32_t       lastDropLogMs = 0;

// -----------------------------------------------------------------------------
// Item helpers — one RMT item = mark (carrier on) followed by space (off)
// -----------------------------------------------------------------------------
static inline uint16_t clampTicks(uint32_t us) {
    return us > kRmtMaxTicks ? kRmtMaxTicks : (uint16_t)us;
}

static inline rmt_item32_t makeItem(uint32_t markUs, uint32_t spaceUs) {
    rmt_item32_t it;
    it.level0    = 1;
    it.duration0 = clampTicks(markUs);
    it.level1    = 0;
    it.duration1 = clampTicks(spaceUs);
    return it;
}

// -----------------------------------------------------------------------------
// BuildXmpItems — port of IRsend::sendXmp (single message, no repeat).
// 16 nibbles as mark + (base + nibble*step) space, with a mark + footer space
// after each 32-bit section. Returns the number of items written.
// -----------------------------------------------------------------------------
static size_t BuildXmpItems(uint64_t data, uint16_t nbits, rmt_item32_t *items) {
    if (nbits < 8 || nbits > 64 || (nbits % 4) != 0) return 0;
    const uint16_t sectionBits = nbits / kXmpSections;
    size_t n = 0;
    for (int16_t shift = nbits - 4; shift >= 0; shift -= 4) {
        uint8_t nibble = (data >> shift) & 0xF;
        items[n++] = makeItem(kXmpMark, kXmpBaseSpace + nibble * kXmpSpaceStep);
        uint16_t bitsDone = nbits - shift;
        if (bitsDone % sectionBits == 0) {
            items[n++] = makeItem(kXmpMark, kXmpFooterSpace);
        }
    }
    return n;
}

// -----------------------------------------------------------------------------
// BuildRawItems — raw[] holds mark, space, mark, space ... (µs), starts on mark
// -----------------------------------------------------------------------------
static size_t BuildRawItems(const uint16_t *raw, uint16_t count, rmt_item32_t *items) {
    if (count > IR_CODE_MAX_RAW) count = IR_CODE_MAX_RAW;
    size_t n = 0;
    for (uint16_t i = 0; i < count; i += 2) {
        uint16_t spaceUs = (i + 1 < count) ? raw[i + 1] : 0;
        items[n++] = makeItem(raw[i], spaceUs);
    }
    return n;
}

// -----------------------------------------------------------------------------
// IrTxTask — waits for queued jobs and plays them out on the IR LED
// -----------------------------------------------------------------------------
static void IrTxTask(void *pvParameters) {
    static rmt_item32_t items[kMaxItems + 1];
    IrTxJob job;

    for (;;) {
        if (xQueueReceive(irQueue, &job, portMAX_DELAY) != pdTRUE) continue;

        size_t   n     = 0;
        uint32_t gapMs = IR_RAW_GAP_MS;
        if (job.format == FMT_XMP) {
            n     = BuildXmpItems(job.value, job.bits, items);
            gapMs = IR_XMP_GAP_MS;
        } else if (job.format == FMT_RAW && job.raw) {
            n = BuildRawItems(job.raw, job.rawCount, items);
        }
        free(job.raw);

        if (n == 0) {
            StatusLog("IR send: code could not be built - skipped");
        } else {
            items[n] = {};   // zero-duration terminator
            txBusy = true;
            // Longer than the 256-item RMT memory → driver refills from its ISR
            esp_err_t err = rmt_write_items(kRmtChannel, items, n + 1, true);  // blocks until sent
            lastActiveMs = millis();
            txBusy = false;

            if (err != ESP_OK) StatusLog("IR send FAILED (RMT error " + String((int)err) + ")");

            vTaskDelay(pdMS_TO_TICKS(gapMs));   // inter-frame silence the decoder expects
        }
        if (job.done) xSemaphoreGive(job.done);
    }
}

// -----------------------------------------------------------------------------
// EnqueueJob — copies raw[] to the heap; false (and nothing leaked) if full
// -----------------------------------------------------------------------------
static bool EnqueueJob(uint8_t format, uint16_t bits, uint64_t value,
                       const uint16_t *raw, uint16_t rawCount, SemaphoreHandle_t done) {
    if (!irQueue) return false;
    IrTxJob job = { format, bits, value, 0, nullptr, done };
    if (format == FMT_RAW) {
        if (!raw || rawCount == 0) return false;
        job.raw = (uint16_t *)malloc(rawCount * sizeof(uint16_t));
        if (!job.raw) return false;
        memcpy(job.raw, raw, rawCount * sizeof(uint16_t));
        job.rawCount = rawCount;
    }
    if (xQueueSend(irQueue, &job, 0) != pdTRUE) {
        free(job.raw);
        if (millis() - lastDropLogMs > 5000) {   // a held button can overrun it — log once
            lastDropLogMs = millis();
            StatusLog("IR send queue full - code dropped");
        }
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// SetupIrTransmit
// -----------------------------------------------------------------------------
void SetupIrTransmit() {
    rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX((gpio_num_t)PIN_IR_LED, kRmtChannel);
    cfg.clk_div                         = 80;   // 80 MHz APB / 80 = 1 µs per tick
    cfg.mem_block_num                   = 4;    // all 4×64 items; longer frames are refilled by the driver
    cfg.tx_config.carrier_en            = true;
    cfg.tx_config.carrier_freq_hz       = IR_CARRIER_HZ;
    cfg.tx_config.carrier_duty_percent  = IR_CARRIER_DUTY;
    cfg.tx_config.carrier_level         = RMT_CARRIER_LEVEL_HIGH;
    cfg.tx_config.idle_output_en        = true;
    cfg.tx_config.idle_level            = RMT_IDLE_LEVEL_LOW;   // LED off when idle
    cfg.tx_config.loop_en               = false;

    ESP_ERROR_CHECK(rmt_config(&cfg));
    ESP_ERROR_CHECK(rmt_driver_install(kRmtChannel, 0, 0));

    irQueue = xQueueCreate(IR_QUEUE_DEPTH, sizeof(IrTxJob));
    xTaskCreate(IrTxTask, "IrTxTask", 4096, NULL, 5, NULL);

    Serial.println("[IR] RMT transmitter ready on GPIO " + String(PIN_IR_LED));
}

// -----------------------------------------------------------------------------
// QueueDstvCode — live DSTV frame from the receiver task
// -----------------------------------------------------------------------------
bool QueueDstvCode(uint64_t value, uint16_t bits) {
    if (!EnqueueJob(FMT_XMP, bits, value, NULL, 0, NULL)) return false;
    portENTER_CRITICAL(&lastMux);
    lastValue = value;
    lastBits  = bits;
    haveLast  = true;
    portEXIT_CRITICAL(&lastMux);
    return true;
}

// -----------------------------------------------------------------------------
// ResendLastDstvCode
// -----------------------------------------------------------------------------
bool ResendLastDstvCode() {
    portENTER_CRITICAL(&lastMux);
    bool     have  = haveLast;
    uint64_t value = lastValue;
    uint16_t bits  = lastBits;
    portEXIT_CRITICAL(&lastMux);
    if (!have) return false;
    return EnqueueJob(FMT_XMP, bits, value, NULL, 0, NULL);
}

// -----------------------------------------------------------------------------
// SendIrCode — recorded stroke (sequence step, record-mode test)
// -----------------------------------------------------------------------------
bool SendIrCode(const IrCode &code, uint32_t waitMs) {
    if (!code.valid()) return false;
    SemaphoreHandle_t done = waitMs ? xSemaphoreCreateBinary() : NULL;
    bool ok = EnqueueJob(code.format, code.bits, code.value,
                         code.raw.data(), (uint16_t)code.raw.size(), done);
    if (done) {
        // On timeout the job may still hold the semaphore — leak it rather
        // than let IrTxTask give a deleted handle (only if RMT hangs).
        if (ok && xSemaphoreTake(done, pdMS_TO_TICKS(waitMs)) != pdTRUE) return false;
        vSemaphoreDelete(done);
    }
    return ok;
}

uint32_t IrTxLastActiveMs() { return lastActiveMs; }
bool     IrTxBusy()         { return txBusy; }
