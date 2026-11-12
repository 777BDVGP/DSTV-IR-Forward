// =============================================================================
// SeqStore.cpp — recorded IR sequences + schedule, stored in LittleFS
// =============================================================================

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "SeqStore.h"
#include "Status.h"

// JSON documents live in PSRAM (a sequence of aircon strokes can be large)
struct SpiRamAllocator {
    void *allocate(size_t n)              { return ps_malloc(n); }
    void  deallocate(void *p)             { free(p); }
    void *reallocate(void *p, size_t n)   { return ps_realloc(p, n); }
};
using PsramJsonDocument = BasicJsonDocument<SpiRamAllocator>;

static SemaphoreHandle_t    fsMtx = NULL;
static std::vector<SeqInfo> index_;          // cached list, kept in step with the files

struct FsLock {
    FsLock()  { xSemaphoreTakeRecursive(fsMtx, portMAX_DELAY); }
    ~FsLock() { xSemaphoreGiveRecursive(fsMtx); }
};

static String seqPath(uint16_t id) { return "/seq/" + String(id) + ".json"; }

// -----------------------------------------------------------------------------
// readFile — whole file into a PSRAM buffer (zero-copy JSON parse source)
// -----------------------------------------------------------------------------
static char *readFile(const String &path) {
    File f = LittleFS.open(path, "r");
    if (!f) return nullptr;
    size_t size = f.size();
    char *buf = (char *)ps_malloc(size + 1);
    if (buf) {
        size_t got = f.readBytes(buf, size);
        buf[got] = '\0';
    }
    f.close();
    return buf;
}

// Caller holds the lock. buf must stay alive while seq is parsed.
static bool parseSequence(char *buf, Sequence &seq, bool withCodes) {
    PsramJsonDocument doc(16384 + strlen(buf) / 4);
    if (deserializeJson(doc, buf)) return false;   // char* → zero-copy strings
    seq.name = doc["name"] | "";
    seq.steps.clear();
    for (JsonObject s : doc["steps"].as<JsonArray>()) {
        SeqStep step;
        step.label   = s["label"] | "";
        step.delayMs = s["delay"] | 500;
        if (withCodes) {
            step.code.format   = s["fmt"]   | 0;
            step.code.protocol = s["proto"] | 0;
            step.code.bits     = s["bits"]  | 0;
            step.code.value    = strtoull(s["value"] | "0", nullptr, 16);
            if (step.code.format == FMT_RAW) step.code.rawFromText(s["raw"] | "");
        }
        seq.steps.push_back(step);
    }
    return true;
}

static void rebuildIndex() {
    index_.clear();
    File dir = LittleFS.open("/seq");
    if (!dir || !dir.isDirectory()) return;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String name = f.name();                       // "<id>.json"
        f.close();
        uint16_t id = name.toInt();
        if (id == 0) continue;
        char *buf = readFile(seqPath(id));
        if (!buf) continue;
        Sequence seq;
        if (parseSequence(buf, seq, false)) index_.push_back({ id, seq.name, (uint16_t)seq.steps.size() });
        free(buf);
    }
    std::sort(index_.begin(), index_.end(),
              [](const SeqInfo &a, const SeqInfo &b) { return a.name.compareTo(b.name) < 0; });
}

// -----------------------------------------------------------------------------
bool SetupSeqStore() {
    fsMtx = xSemaphoreCreateRecursiveMutex();
    FsLock lock;
    // partitions_ota.csv names the data partition "spiffs"
    if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
        StatusLog("LittleFS mount FAILED - sequences unavailable");
        return false;
    }
    if (!LittleFS.exists("/seq")) LittleFS.mkdir("/seq");
    rebuildIndex();
    StatusLog("Storage: " + String(index_.size()) + " sequence(s), " +
              String(LittleFS.usedBytes() / 1024) + " / " + String(LittleFS.totalBytes() / 1024) + " KB used");
    return true;
}

std::vector<SeqInfo> SeqList() {
    FsLock lock;
    return index_;
}

String SeqName(uint16_t id) {
    FsLock lock;
    for (auto &s : index_) if (s.id == id) return s.name;
    return "";
}

bool SeqNameUsed(const String &name, uint16_t exceptId) {
    FsLock lock;
    for (auto &s : index_)
        if (s.id != exceptId && s.name.equalsIgnoreCase(name)) return true;
    return false;
}

bool SeqLoad(uint16_t id, Sequence &seq) {
    FsLock lock;
    char *buf = readFile(seqPath(id));
    if (!buf) return false;
    bool ok = parseSequence(buf, seq, true);
    free(buf);
    seq.id = id;
    return ok;
}

uint16_t SeqSave(Sequence &seq) {
    FsLock lock;
    if (seq.id == 0) {
        uint16_t maxId = 0;
        for (auto &s : index_) if (s.id > maxId) maxId = s.id;
        seq.id = maxId + 1;
    }

    // const char* values are stored by pointer (no copy) — keep texts alive
    std::vector<String> texts;
    texts.reserve(seq.steps.size() * 2);
    size_t capacity = 1024 + seq.steps.size() * 256;
    PsramJsonDocument doc(capacity);
    doc["name"] = seq.name.c_str();
    JsonArray steps = doc.createNestedArray("steps");
    for (auto &st : seq.steps) {
        JsonObject o = steps.createNestedObject();
        o["label"] = st.label.c_str();
        o["delay"] = st.delayMs;
        o["fmt"]   = st.code.format;
        o["proto"] = st.code.protocol;
        o["bits"]  = st.code.bits;
        char hex[20];
        snprintf(hex, sizeof(hex), "%llX", (unsigned long long)st.code.value);
        texts.push_back(hex);
        o["value"] = texts.back().c_str();
        if (st.code.format == FMT_RAW) {
            texts.push_back(st.code.rawToText());
            o["raw"] = texts.back().c_str();
        }
    }
    if (doc.overflowed()) return 0;

    File f = LittleFS.open(seqPath(seq.id), "w");
    if (!f) return 0;
    size_t written = serializeJson(doc, f);
    f.close();
    if (written == 0) return 0;

    rebuildIndex();
    return seq.id;
}

bool SeqDelete(uint16_t id) {
    FsLock lock;
    bool ok = LittleFS.remove(seqPath(id));
    rebuildIndex();
    return ok;
}

// -----------------------------------------------------------------------------
// Schedule (Scheduler.cpp keeps the live copy)
// -----------------------------------------------------------------------------
std::vector<SchedEntry> SchedLoad() {
    FsLock lock;
    std::vector<SchedEntry> out;
    char *buf = readFile("/schedule.json");
    if (!buf) return out;
    PsramJsonDocument doc(16384);
    if (!deserializeJson(doc, buf)) {
        for (JsonObject o : doc["entries"].as<JsonArray>()) {
            SchedEntry e;
            e.name    = o["name"] | "";
            e.enabled = o["en"]   | true;
            e.time    = o["time"] | "08:00";
            e.days    = o["days"] | 0x3E;
            e.lastRun = o["last"] | "";
            SchedReadSeqs(o, e);
            out.push_back(e);
        }
    }
    free(buf);
    return out;
}

bool SchedSave(const std::vector<SchedEntry> &entries) {
    FsLock lock;
    PsramJsonDocument doc(16384);
    JsonArray arr = doc.createNestedArray("entries");
    for (auto &e : entries) {
        JsonObject o = arr.createNestedObject();
        o["name"] = e.name;
        o["en"]   = e.enabled;
        o["time"] = e.time;
        o["days"] = e.days;
        o["last"] = e.lastRun;
        JsonArray s = o.createNestedArray("seqs");
        for (uint16_t id : e.seqs) s.add(id);
        JsonArray p = o.createNestedArray("pauses");
        for (uint32_t ms : e.pauses) p.add(ms);
        JsonArray t = o.createNestedArray("times");
        for (uint8_t n : e.times) t.add(n);
        JsonArray ev = o.createNestedArray("every");
        for (uint32_t ms : e.every) ev.add(ms);
        JsonArray on = o.createNestedArray("on");
        for (uint8_t v : e.on) on.add(v ? 1 : 0);
    }
    if (doc.overflowed()) return false;
    File f = LittleFS.open("/schedule.json", "w");
    if (!f) return false;
    size_t written = serializeJson(doc, f);
    f.close();
    return written > 0;
}

void SchedReadSeqs(JsonObjectConst o, SchedEntry &e) {
    uint32_t oldGap = o["gap"] | SCHED_PAUSE_DEFAULT_MS;   // before per-sequence pauses
    JsonArrayConst pauses = o["pauses"];
    JsonArrayConst times  = o["times"];   // missing (older files) = 1
    JsonArrayConst every  = o["every"];   // missing = 500 ms
    JsonArrayConst onArr  = o["on"];      // missing = line on
    e.seqs.clear();
    e.pauses.clear();
    e.times.clear();
    e.every.clear();
    e.on.clear();
    for (uint16_t id : o["seqs"].as<JsonArrayConst>()) {
        size_t k = e.seqs.size();
        e.seqs.push_back(id);
        uint32_t ms = k < pauses.size() ? pauses[k].as<uint32_t>() : oldGap;
        e.pauses.push_back(ms < SCHED_PAUSE_MIN_MS ? SCHED_PAUSE_MIN_MS : ms);
        int n = k < times.size() ? times[k].as<int>() : 1;
        e.times.push_back(n < 1 ? 1 : n > SCHED_TIMES_MAX ? SCHED_TIMES_MAX : n);
        uint32_t ev = k < every.size() ? every[k].as<uint32_t>() : SCHED_EVERY_DEFAULT_MS;
        e.every.push_back(ev < SCHED_EVERY_MIN_MS ? SCHED_EVERY_MIN_MS :
                          ev > SCHED_EVERY_MAX_MS ? SCHED_EVERY_MAX_MS : ev);
        e.on.push_back(k < onArr.size() ? (onArr[k].as<int>() ? 1 : 0) : 1);
    }
}

void SeqReindex() {
    FsLock lock;
    if (!LittleFS.exists("/seq")) LittleFS.mkdir("/seq");
    rebuildIndex();
}

size_t StoreUsedBytes()  { FsLock lock; return LittleFS.usedBytes(); }
size_t StoreTotalBytes() { FsLock lock; return LittleFS.totalBytes(); }
