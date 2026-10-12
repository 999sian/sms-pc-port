#pragma once
#include <cstddef>
#include <cstdint>

struct APerformanceHintManager;
struct APerformanceHintSession;

namespace port_android {
struct HintApi {
    APerformanceHintManager* (*manager)();
    APerformanceHintSession* (*create)(APerformanceHintManager*, const int32_t*, size_t, int64_t);
    int (*target)(APerformanceHintSession*, int64_t);
    int (*report)(APerformanceHintSession*, int64_t);
    void (*close)(APerformanceHintSession*);
};

// One frame-producing thread group. Android may decline a session; avoid
// repeatedly calling an unsupported service. Recreate when a thread changes,
// since setThreads is unavailable on the older supported Android versions.
class HintSession {
    HintApi api;
    APerformanceHintManager* manager = nullptr;
    APerformanceHintSession* session = nullptr;
    bool attempted = false, failed = false;
    int32_t game = 0, render = 0;
    int64_t budget = 0;
public:
    explicit HintSession(HintApi functions) : api(functions) {}
    ~HintSession() { close(); }
    bool configure(int32_t gameTid, int32_t renderTid, int64_t targetNs) {
        if (failed || gameTid <= 0 || targetNs <= 0) return false;
        if (!attempted) { manager = api.manager(); attempted = true; }
        if (!manager) return false;
        if (session && (game != gameTid || render != renderTid)) close();
        if (!session) {
            int32_t tids[] = {gameTid, renderTid};
            size_t count = renderTid > 0 && renderTid != gameTid ? 2 : 1;
            session = api.create(manager, tids, count, targetNs);
            if (!session) { failed = true; return false; }
            game = gameTid; render = renderTid; budget = targetNs;
        } else if (budget != targetNs) {
            if (api.target(session, targetNs) != 0) { close(); failed = true; return false; }
            budget = targetNs;
        }
        return true;
    }
    bool report(int64_t actualNs) {
        if (!session || actualNs <= 0) return false;
        if (api.report(session, actualNs) != 0) { close(); failed = true; return false; }
        return true;
    }
    void close() { if (session) api.close(session); session = nullptr; }
};
}
