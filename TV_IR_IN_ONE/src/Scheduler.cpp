// =============================================================================
// Scheduler.cpp — runs recorded sequences at set times on set weekdays
// =============================================================================

#include <Arduino.h>
#include <time.h>
#include <algorithm>
#include "Scheduler.h"
#include "Sequencer.h"
#include "IrRecord.h"
#include "Clock.h"
#include "Status.h"

static SemaphoreHandle_t       schedMtx = NULL;
static std::vector<SchedEntry> entries;
static int                     lastCheckedMinute = -1;

struct SchedLock {
    SchedLock()  { xSemaphoreTake(schedMtx, portMAX_DELAY); }
    ~SchedLock() { xSemaphoreGive(schedMtx); }
};

// runEntry — queue the lines of an entry that are switched on. A line that is
// off is skipped together with its pause. Nothing on = nothing to do (true).
static bool runEntry(const SchedEntry &e, const String &source) {
    std::vector<uint16_t> ids;
    std::vector<uint32_t> pauses, every;
    std::vector<uint8_t>  times;
    for (size_t k = 0; k < e.seqs.size(); k++) {
        if (k < e.on.size() && !e.on[k]) continue;
        ids.push_back(e.seqs[k]);
        pauses.push_back(k < e.pauses.size() ? e.pauses[k] : 0);
        times.push_back(k < e.times.size() ? e.times[k] : 1);
        every.push_back(k < e.every.size() ? e.every[k] : 0);
    }
    if (ids.empty()) {
        StatusLog("Schedule '" + e.name + "': every line is switched off - nothing sent");
        return true;
    }
    return SeqRun(ids, pauses, times, every, source);
}

static bool validTime(const String &hhmm) {
    if (hhmm.length() != 5 || hhmm[2] != ':') return false;
    int h = hhmm.substring(0, 2).toInt(), m = hhmm.substring(3).toInt();
    return isDigit(hhmm[0]) && isDigit(hhmm[1]) && isDigit(hhmm[3]) && isDigit(hhmm[4]) &&
           h < 24 && m < 60;
}

static String entriesText(size_t n) {
    return String(n) + " entr" + (n == 1 ? "y" : "ies");
}

// -----------------------------------------------------------------------------
void SetupScheduler() {
    schedMtx = xSemaphoreCreateMutex();
    SchedLock lock;
    entries = SchedLoad();
    StatusLog("Schedule: " + entriesText(entries.size()));
}

void SchedulerLoop() {
    static uint32_t lastMs = 0;
    if (millis() - lastMs < 1000) return;
    lastMs = millis();

    if (!ClockSynced()) return;          // no time yet → nothing can be due
    struct tm t;
    time_t now = time(nullptr);
    localtime_r(&now, &t);

    int minuteOfDay = t.tm_hour * 60 + t.tm_min;
    if (minuteOfDay == lastCheckedMinute) return;
    lastCheckedMinute = minuteOfDay;

    char hhmm[6], stamp[20];
    strftime(hhmm,  sizeof(hhmm),  "%H:%M", &t);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", &t);

    SchedLock lock;
    bool changed = false;
    for (auto &e : entries) {
        if (!e.enabled || e.seqs.empty() || e.time != hhmm || !(e.days & (1 << t.tm_wday))) continue;
        if (e.lastRun == stamp) continue;   // already ran this minute (e.g. after a reboot)
        e.lastRun = stamp;                  // due now: run or skip, never retried
        changed   = true;
        if (RecActive()) {
            StatusLog("Schedule '" + e.name + "' " + hhmm + " SKIPPED - a sequence recording is open");
            continue;
        }
        if (!runEntry(e, "schedule '" + e.name + "' " + hhmm))
            StatusLog("Schedule '" + e.name + "' could not start (run queue full)");
    }
    if (changed) SchedSave(entries);
}

std::vector<SchedEntry> SchedGet() {
    SchedLock lock;
    return entries;
}

String SchedSet(const std::vector<SchedEntry> &newEntries) {
    if (newEntries.size() > SCHED_MAX_ENTRIES) return "Too many entries (max " + String(SCHED_MAX_ENTRIES) + ")";
    for (auto &e : newEntries) {
        String who = "'" + e.name + "': ";
        if (e.name.isEmpty())                   return "Every entry needs a name";
        if (!validTime(e.time))                 return who + "time must be HH:MM";
        if (e.days == 0)                        return who + "tick at least one day";
        if (e.seqs.empty())                     return who + "select at least one sequence";
        if (e.seqs.size() > SEQ_MAX_PER_RUN)    return who + "max " + String(SEQ_MAX_PER_RUN) + " sequences";
        for (uint16_t id : e.seqs)
            if (SeqName(id).isEmpty())          return who + "unknown sequence #" + String(id);
        if (e.pauses.size() != e.seqs.size())   return who + "a pause is missing";
        if (e.times.size() != e.seqs.size())    return who + "a times value is missing";
        if (e.every.size() != e.seqs.size())    return who + "an every value is missing";
        if (e.on.size() != e.seqs.size())       return who + "a line on/off value is missing";
        if (e.enabled && std::find(e.on.begin(), e.on.end(), 1) == e.on.end())
                                                return who + "tick at least one line (or untick Enabled)";
        for (uint32_t ms : e.every)
            if (ms < SCHED_EVERY_MIN_MS || ms > SCHED_EVERY_MAX_MS)
                return who + "every must be " + String(SCHED_EVERY_MIN_MS) + " to " + String(SCHED_EVERY_MAX_MS) + " ms";
        for (uint8_t n : e.times)
            if (n < 1 || n > SCHED_TIMES_MAX)   return who + "times must be 1 to " + String(SCHED_TIMES_MAX);
        for (uint32_t ms : e.pauses)
            if (ms < SCHED_PAUSE_MIN_MS || ms > SCHED_PAUSE_MAX_MS)
                return who + "pause must be " + String(SCHED_PAUSE_MIN_MS) + " to " + String(SCHED_PAUSE_MAX_MS) + " ms";
    }
    SchedLock lock;
    // keep lastRun of unchanged entries so an edit at 08:00 does not re-run them
    std::vector<SchedEntry> merged = newEntries;
    for (auto &m : merged)
        for (auto &old : entries)
            if (old.name == m.name && old.time == m.time) m.lastRun = old.lastRun;
    if (!SchedSave(merged)) return "Saving failed";
    entries = merged;
    StatusLog("Schedule saved, " + entriesText(entries.size()));
    return "";
}

void SchedReload() {
    SchedLock lock;
    entries = SchedLoad();
}

bool SchedRunNow(size_t index) {
    SchedEntry e;
    {
        SchedLock lock;
        if (index >= entries.size()) return false;
        e = entries[index];
    }
    if (e.seqs.empty()) return false;
    return runEntry(e, "manual run of schedule '" + e.name + "'");
}

// A deleted sequence leaves every entry; an entry left with none is disabled
void SchedForgetSequence(uint16_t id) {
    SchedLock lock;
    bool changed = false;
    for (auto &e : entries) {
        size_t before = e.seqs.size();
        for (size_t k = e.seqs.size(); k-- > 0;)
            if (e.seqs[k] == id) {
                e.seqs.erase(e.seqs.begin() + k);
                if (k < e.pauses.size()) e.pauses.erase(e.pauses.begin() + k);
                if (k < e.times.size())  e.times.erase(e.times.begin() + k);
                if (k < e.every.size())  e.every.erase(e.every.begin() + k);
                if (k < e.on.size())     e.on.erase(e.on.begin() + k);
            }
        if (e.seqs.size() == before) continue;
        changed = true;
        if (e.seqs.empty() && e.enabled) {
            e.enabled = false;
            StatusLog("Schedule '" + e.name + "' DISABLED - its only sequence was deleted");
        } else {
            StatusLog("Schedule '" + e.name + "': deleted sequence removed from it");
        }
    }
    if (changed) SchedSave(entries);
}
