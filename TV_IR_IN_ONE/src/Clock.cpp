// =============================================================================
// Clock.cpp — NTP time for the page header and the event log
// =============================================================================

#include <Arduino.h>
#include <time.h>
#include "Clock.h"
#include "Status.h"

static bool wasSynced = false;

static bool localNow(struct tm &t) {
    time_t now = time(nullptr);
    localtime_r(&now, &t);
    return t.tm_year + 1900 >= 2024;   // before the first NTP sync the clock says 1970
}

void ClockSetTimeZone() {
    setenv("TZ", CLOCK_TZ, 1);
    tzset();
}

void SetupClock() {
    configTzTime(CLOCK_TZ, CLOCK_NTP1, CLOCK_NTP2);
}

void ClockLoop() {
    static uint32_t lastMs = 0;
    if (wasSynced || millis() - lastMs < 1000) return;
    lastMs = millis();
    struct tm t;
    if (!localNow(t)) return;
    wasSynced = true;
    StatusLog("Time synced: " + ClockText());
}

bool ClockSynced() {
    struct tm t;
    return localNow(t);
}

String ClockText() {
    struct tm t;
    if (!localNow(t)) return "OFFLINE  NO  TIME";
    char buf[32];
    strftime(buf, sizeof(buf), "%d %b %Y %H:%M:%S %a", &t);   // same as the TX header
    return String(buf);
}
