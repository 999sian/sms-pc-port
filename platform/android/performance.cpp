#ifdef __ANDROID__
#include "performance_session.h"
#include "port_android_performance.h"
#include <atomic>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

namespace {
std::atomic<int32_t> renderTid{0};
port_android::HintSession* hints = nullptr;
bool initialized = false, unavailable = false;
int32_t frameTid = 0, sessionGameTid = 0, sessionRenderTid = 0;
int64_t frameStart = 0, sessionTarget = 0;
unsigned reports = 0;
int64_t nowNs() {
    timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return int64_t(t.tv_sec) * 1000000000 + t.tv_nsec;
}
void initialize() {
    initialized = true;
    const char* enabled = getenv("SMS_ANDROID_PERFORMANCE_HINTS");
    if (enabled && enabled[0] == '0') {
        unavailable = true;
        fprintf(stderr, "[android] performance hints disabled\n");
        return;
    }
    // Resolve API 31 functions at runtime so Android 10/11 still load the APK.
    // Keep libandroid open for the lifetime of the function pointers.
    void* library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    port_android::HintApi api{};
    if (library) {
#define HINT_ENTRY(field, name) api.field = reinterpret_cast<decltype(api.field)>(dlsym(library, name))
        HINT_ENTRY(manager, "APerformanceHint_getManager");
        HINT_ENTRY(create, "APerformanceHint_createSession");
        HINT_ENTRY(target, "APerformanceHint_updateTargetWorkDuration");
        HINT_ENTRY(report, "APerformanceHint_reportActualWorkDuration");
        HINT_ENTRY(close, "APerformanceHint_closeSession");
#undef HINT_ENTRY
    }
    if (!api.manager || !api.create || !api.target || !api.report || !api.close) {
        unavailable = true;
        fprintf(stderr, "[android] performance hint API unavailable\n");
        return;
    }
    hints = new port_android::HintSession(api);
}
}

extern "C" void port_android_performance_render_thread() {
    renderTid.store(gettid(), std::memory_order_release);
}
extern "C" void port_android_performance_frame_end() {
    if (!hints || !frameStart || frameTid != gettid()) return;
    const int64_t work = nowNs() - frameStart;
    frameStart = 0;
    if (!hints->report(work)) {
        unavailable = true;
        fprintf(stderr, "[android] performance hint reporting stopped\n");
    } else if (++reports == 1) {
        fprintf(stderr, "[android] performance hints: first frame report %.2f ms\n", double(work) / 1000000);
    }
}
extern "C" void port_android_performance_frame_begin(int64_t targetNs) {
    if (targetNs <= 0) { frameStart = 0; return; }
    if (!initialized) initialize();
    if (unavailable || !hints) return;
    const int32_t game = gettid();
    const int32_t worker = renderTid.load(std::memory_order_acquire);
    // Android normally keeps GL on the game's thread. A separate GL worker
    // can opt in through the existing renderer setting; deduplicate IDs.
    const int32_t render = worker > 0 ? worker : game;
    if (!hints->configure(game, render, targetNs)) {
        unavailable = true;
        fprintf(stderr, "[android] performance hint sessions unsupported or rejected\n");
        return;
    }
    if (game != sessionGameTid || render != sessionRenderTid || targetNs != sessionTarget) {
        fprintf(stderr, "[android] performance hints active: game TID %d, render TID %d, target %.2f ms\n",
                game, render, double(targetNs) / 1000000);
        sessionGameTid = game; sessionRenderTid = render; sessionTarget = targetNs;
    }
    frameTid = game;
    frameStart = nowNs();
}
extern "C" void port_android_performance_pause() {
    frameStart = 0;
    if (hints) hints->close();
    sessionGameTid = sessionRenderTid = 0;
    sessionTarget = 0;
}
extern "C" void port_android_performance_shutdown() {
    port_android_performance_pause();
    delete hints; hints = nullptr;
}
#endif
