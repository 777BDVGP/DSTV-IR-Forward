// =============================================================================
// RouterCheck.cpp — gateway ping every minute (see RouterCheck.h)
// =============================================================================
// IDENTICAL copy in TV_IR_Sending/src and TV_IR_Receiving/src — keep in sync.
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include "ping/ping_sock.h"
#include "RouterCheck.h"

static esp_ping_handle_t session  = NULL;
static volatile bool     done     = false;   // set by the ping task
static volatile bool     answered = false;
static volatile uint32_t rttMs    = 0;

static uint32_t lastStartMs = 0;
static bool     started     = false;          // at least one check finished
static bool     reachable   = true;
static uint32_t lastRttMs   = 0;
static uint8_t  fails       = 0;

// ── ping task callbacks (keep short) ─────────────────────────────────────────
static void onSuccess(esp_ping_handle_t hdl, void *) {
    uint32_t t = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &t, sizeof(t));
    rttMs    = t;
    answered = true;
}
static void onTimeout(esp_ping_handle_t, void *) {}
static void onEnd(esp_ping_handle_t, void *)     { done = true; }

static bool startPing() {
    IPAddress gw = WiFi.gatewayIP();
    if (gw == IPAddress(0, 0, 0, 0)) return false;

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr.type             = IPADDR_TYPE_V4;
    cfg.target_addr.u_addr.ip4.addr  = static_cast<uint32_t>(gw);
    cfg.count           = 1;
    cfg.timeout_ms      = RC_TIMEOUT_MS;
    cfg.task_stack_size = 3072;

    esp_ping_callbacks_t cbs = {};
    cbs.on_ping_success = onSuccess;
    cbs.on_ping_timeout = onTimeout;
    cbs.on_ping_end     = onEnd;

    done = answered = false;
    if (esp_ping_new_session(&cfg, &cbs, &session) != ESP_OK) { session = NULL; return false; }
    if (esp_ping_start(session) != ESP_OK) {
        esp_ping_delete_session(session);
        session = NULL;
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
RouterAction RouterCheckLoop() {
    // A ping is running: wait for its end, then judge it
    if (session) {
        if (!done) return RC_NONE;
        esp_ping_delete_session(session);
        session = NULL;
        started = true;

        if (answered) {
            lastRttMs = rttMs;
            bool wasDown = !reachable;
            reachable = true;
            fails     = 0;
            return wasDown ? RC_RECOVERED : RC_NONE;
        }
        reachable = false;
        if (fails < 255) fails++;
        if (fails == RC_FAILS_RECONNECT) return RC_RECONNECT;
        if (fails >= RC_FAILS_REBOOT)    return RC_REBOOT;
        return RC_NONE;
    }

    if (WiFi.status() != WL_CONNECTED) return RC_NONE;   // WiFi watchdogs handle this
    if (lastStartMs && millis() - lastStartMs < RC_INTERVAL_MS) return RC_NONE;
    lastStartMs = millis() | 1;
    startPing();
    return RC_NONE;
}

String RouterCheckText() {
    if (!started)  return "checking...";
    if (reachable) return "yes (" + String(lastRttMs) + " ms)";
    return "NO (" + String(fails) + " failed)";
}

bool    RouterReachable() { return reachable; }
uint8_t RouterFailCount() { return fails; }
