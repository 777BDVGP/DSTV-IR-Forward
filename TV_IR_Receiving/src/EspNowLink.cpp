// =============================================================================
// EspNowLink.cpp — ESP-NOW receiver side of the IR link
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "Config.h"
#include "IrLink.h"
#include "EspNowLink.h"
#include "IrTransmit.h"
#include "Status.h"
#include "RemoteRec.h"

String RxSysName();   // WebSettings.cpp

static const uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint32_t lastBeaconMs = 0;

// Record packets from the TX page are handled in EspNowLoop (loop task):
// they touch the recorder and LittleFS, which must not run in the WiFi task.
struct RecPacket {
    uint8_t mac[6];
    uint8_t len;
    uint8_t data[250];
};
static QueueHandle_t recQueue = NULL;

// -----------------------------------------------------------------------------
// OnDataRecv — runs in the WiFi task; keep it short (queue work, no IR here)
// -----------------------------------------------------------------------------
static void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len < (int)sizeof(IrLinkHeader)) return;
    IrLinkHeader hdr;
    memcpy(&hdr, data, sizeof(hdr));
    if (hdr.magic != IRLINK_MAGIC || hdr.version != IRLINK_VERSION) return;

    if (hdr.type == PKT_IR_FRAME) {
        if (len < (int)IrLinkFrameSize(0)) return;
        IrLinkIrFrame frame = {};
        memcpy(&frame, data, min((size_t)len, sizeof(frame)));
        // Trust only what actually arrived
        uint16_t maxRaw = (len - IrLinkFrameSize(0)) / sizeof(uint16_t);
        if (frame.rawCount > maxRaw) frame.rawCount = maxRaw;

        if (StatusRoomIrFrame(mac, frame)) QueueIrFrame(frame);
    }
    else if (hdr.type == PKT_HEARTBEAT) {
        if (len < (int)sizeof(IrLinkHeartbeat)) return;
        IrLinkHeartbeat hb;
        memcpy(&hb, data, sizeof(hb));
        StatusRoomHeartbeat(mac, hb);
    }
    else if (hdr.type == PKT_REC_CMD || hdr.type == PKT_REC_STROKE) {
        if (!recQueue || len > (int)sizeof(RecPacket::data)) return;
        RecPacket p;
        memcpy(p.mac, mac, 6);
        p.len = (uint8_t)len;
        memcpy(p.data, data, len);
        xQueueSend(recQueue, &p, 0);
    }
}

void EspNowBroadcast(const void *data, size_t len) {
    esp_now_send(kBroadcastMac, (const uint8_t *)data, len);
}

// -----------------------------------------------------------------------------
// SetupEspNow
// -----------------------------------------------------------------------------
bool SetupEspNow() {
    if (esp_now_init() != ESP_OK) {
        StatusLog("ESP-NOW init FAILED");
        return false;
    }
    recQueue = xQueueCreate(16, sizeof(RecPacket));   // a long stroke is up to 11 parts
    esp_now_register_recv_cb(OnDataRecv);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, kBroadcastMac, 6);
    peer.channel = 0;              // 0 = current channel (the AP's channel)
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        StatusLog("ESP-NOW broadcast peer FAILED");
        return false;
    }

    StatusLog("ESP-NOW ready, MAC " + WiFi.macAddress() + ", channel " + String(WiFi.channel()));
    return true;
}

// -----------------------------------------------------------------------------
// EspNowLoop — beacon so TX nodes can find us (and re-find us after a swap)
// -----------------------------------------------------------------------------
void EspNowLoop() {
    RecPacket p;
    while (recQueue && xQueueReceive(recQueue, &p, 0) == pdTRUE)
        RemoteRecPacket(p.mac, p.data, p.len);

    if (millis() - lastBeaconMs < IRLINK_BEACON_MS) return;
    lastBeaconMs = millis();

    IrLinkBeacon b = {};
    b.hdr.magic   = IRLINK_MAGIC;
    b.hdr.version = IRLINK_VERSION;
    b.hdr.type    = PKT_BEACON;
    strlcpy(b.name, RxSysName().c_str(), IRLINK_NAME_LEN);   // TX shows it as "RX Room"
    b.irSent = StatusIrSentCount();                          // TX shows "IR codes sent out to IR"
    esp_now_send(kBroadcastMac, (const uint8_t *)&b, sizeof(b));
}
