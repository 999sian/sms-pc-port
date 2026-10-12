// SDLActivity loads this small library normally. The Android linker loads the
// game into a reserved address range so the decomp's pointer-in-u32 fields
// work with its existing low stacks and MEM1 allocator on a 64-bit device.
#include <SDL.h>
#include <android/dlext.h>
#include <android/log.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv) {
    std::string data, libs, disc;
    bool selftest = false;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--data")) data = argv[i + 1];
        else if (!strcmp(argv[i], "--libraries")) libs = argv[i + 1];
        else if (!strcmp(argv[i], "--disc")) disc = argv[i + 1];
        else if (!strcmp(argv[i], "--gx-selftest")) selftest = !strcmp(argv[i + 1], "1");
        else if (!strcmp(argv[i], "--env")) {
            std::string e = argv[i + 1];
            auto at = e.find('=');
            if (at != std::string::npos) setenv(e.substr(0, at).c_str(), e.substr(at + 1).c_str(), 1);
        }
    }
    if (data.empty() || libs.empty() || disc.empty() || chdir(data.c_str())) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SMS Launcher", "The game files could not be opened.", nullptr);
        return 1;
    }
    int log = open((data + "/game.log").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (log >= 0) { dup2(log, STDOUT_FILENO); dup2(log, STDERR_FILENO); close(log); }
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    if (const char* policy = getenv("SMS_ANDROID_STARTUP_POLICY"))
        fprintf(stderr, "[android] startup policy: %s\n", policy);
    setenv("HOME", data.c_str(), 1);
    setenv("XDG_CACHE_HOME", (data + "/cache").c_str(), 1);
    setenv("SMS_GX_GL_THREAD", "0", 0); // Android SDL owns the surface's thread.
    setenv("SMS_MSAA", "0", 0);
    const size_t bytes = size_t(128) << 20;
    void* region = MAP_FAILED;
    for (uintptr_t at = 0x10000000; at < 0x40000000; at += bytes) {
        void* want = reinterpret_cast<void*>(at);
        void* p = mmap(want, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == want) { region = p; break; }
        if (p != MAP_FAILED) munmap(p, bytes);
    }
    if (region == MAP_FAILED) {
        fprintf(stderr, "[android] could not reserve the game's compatibility address range\n");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SMS Launcher", "The game runtime could not reserve its address range.", nullptr);
        return 1;
    }
    android_dlextinfo info{};
    info.flags = ANDROID_DLEXT_RESERVED_ADDRESS;
    info.reserved_addr = region;
    info.reserved_size = bytes;
    void* game = android_dlopen_ext((libs + (selftest ? "/libgx_selftest.so" : "/libsms.so")).c_str(), RTLD_NOW | RTLD_LOCAL, &info);
    if (!game) {
        const char* error = dlerror();
        fprintf(stderr, "[android] loading libsms.so: %s\n", error ? error : "unknown error");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SMS Launcher", error ? error : "The game runtime could not be loaded.", nullptr);
        munmap(region, bytes);
        return 1;
    }
    auto entry = reinterpret_cast<int (*)(int, char**)>(dlsym(game, "SDL_main"));
    if (!entry) { fprintf(stderr, "[android] SDL_main missing: %s\n", dlerror()); return 1; }
    fprintf(stderr, "[android] 64-bit game loaded at %p, page size %ld\n", region, sysconf(_SC_PAGESIZE));
    if (selftest) {
        std::string output = data + "/gx-selftest.ppm";
        char* testArgs[] = {const_cast<char*>("gx_selftest"), const_cast<char*>("--window"), const_cast<char*>(output.c_str()), nullptr};
        int result = entry(3, testArgs);
        fprintf(stderr, "[android] GX self-test returned %d\n", result);
        return result;
    }
    char* args[] = {const_cast<char*>("sms"), const_cast<char*>(disc.c_str()), nullptr};
    return entry(2, args);
}
