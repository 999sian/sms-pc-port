# ARM64 Android

Android builds use the native PC port, SDL 2.32.10, and a GLES renderer.
No GameCube CPU emulation is involved. Build an APK through the Android
project in the accompanying SMS Launcher checkout; its Android README
describes the SDK, JDK, install and disc/save import steps.

To compile just the native libraries on a Linux host:

```sh
export ANDROID_SDK_ROOT=/path/to/android-sdk
./android/build-native.sh
```

NDK 28.2.13676358 is the pinned toolchain. The result in
`build/android-arm64/` includes `libsms.so`, `libsmsbootstrap.so` and
`SDL2/libSDL2.so`. These libraries run inside SDLActivity; they are not
standalone shell executables. All Android targets are `arm64-v8a`, PIC,
and use the NDK's 16 KiB-compatible ELF alignment. The launcher unpacks
native libraries to permit the bootstrap's explicit game load.

The game remains 64-bit code. The existing decompilation still stores some
pointers in 32-bit fields, so the bootstrap reserves a low address range
and loads the game library there with `android_dlopen_ext`. The existing
MEM1 and low-stack allocators preserve those fields, without changing
Android's normal address space for SDL, libc, the graphics driver or Java.
This is an internal layout compatibility constraint, not a 32-bit APK.

GLES 3.1 or newer is required. Base-vertex drawing uses GLES 3.2 or its
extension when available, with an index-rebasing fallback for GLES 3.1.
Ordered metric passes rebase each range once from retained CPU indices;
stream uploads avoid synchronous GPU readbacks and map/unmap round trips.
Android shader adapters cache uniform locations and retained values, with
invalidation on program deletion, linking and binary loading. Shader generation
uses GLSL ES, with explicit fragment locations and precision. Desktop-only
depth-range/clear-depth, point-size and multi-draw calls have GLES adapters.
Color logic operations use a destination snapshot per primitive. Numeric
pixel metrics use SSBO counters and ordered drawing, preserving counts that
GLES boolean visibility queries cannot provide. MSAA stays off on Android.
Clip distances and depth clamp use their GLES extensions when available.
The clipping extension predeclares `gl_ClipDistance`; constant indices size
its array implicitly. Avoid a standalone redeclaration, which the Adreno
750 V@0762.41 driver rejects with a reserved-built-in-name compile error.
Without those extensions the vertex shader clamps forward-facing depth;
this fallback passes the shared graphics tests but is not a substitute for
testing every scene on each device's driver.
Depth peeks sample the depth texture into RGB bytes because GLES does not
permit desktop depth readback. Sampler LOD bias is supplied in the shader.

The renderer adapts the shared GX code rather than maintaining a second
copy. Desktop paths remain available and should pass the existing GX tests.
Android crash reports use the NDK unwinder and module offsets in the game
log, rather than desktop `execinfo` or an external `addr2line` process.
Resolve offsets offline using the matching unstripped `libsms.so` and the
NDK's `llvm-addr2line`.

The launcher isolates each game session in its own process. Closing its SDL
window releases the renderer, flushes the game log, and ends that process
without running global destructors alongside still-running SDK workers.
The next Play starts with fresh heaps, threads, and caches. Android frame waits
also yield a retrace when lower-priority resource loading is runnable, so
a slow presentation cannot starve the stage loader after movies.

For renderer diagnostics, `SMS_GX_FINISH_BEFORE_SWAP=1` forces `glFinish`
before presentation and logs render-completion wait and buffer-swap time
separately every 30 frames. It is off by default. This serializes rendering
and is not an asynchronous GPU timestamp or a normal performance benchmark.
The overlay's ordinary swap time includes work the driver finishes inside
`SDL_GL_SwapWindow`; its explicit GPU-wait field excludes that wait.

Validation must include installing the actual ARM64 APK, title/file selection,
an imported save, stage gameplay and unskipped movies. ARM64 translation in
an Android emulator is suitable for correctness checks, but physical handheld
testing is still needed for performance, thermal behavior and controller feel.

Android 12+ performance hints register the SDK thread producing frames and
its GL worker when enabled (Android normally keeps both on one thread) with APerformanceHintManager. Each SDK frame wait reports the
work cycle since the last wait ended, then starts the next cycle after the
intentional pacing sleep. The budget follows the actual retrace count per
frame (including 30 fps menus/movies and 60 fps gameplay). Work includes
synchronous driver/graphics waits; it is not a GPU timestamp or CPU utilization
counter. These advisory CPU scheduling hints do not lock clocks or override
thermal limits. Missing/rejected APIs disable the session once without
changing game timing. Runtime symbol lookup keeps Android 10/11 supported.
Backgrounding closes the session and discards partial frame timing; returning
recreates it. Normal game shutdown explicitly closes the session. See
`platform/android/tests/test_performance_session.cpp` for thread-group,
budget-update, pause/resume and service-failure checks.

The Android launcher declares the application category as game. Its
**Android performance hints** switch defaults on; switch it off for a device
comparison (`SMS_ANDROID_PERFORMANCE_HINTS=0`). The **Show performance overlay**
switch reports our own frame rate/timings rather than only SurfaceFlinger FPS.
A device's game policies and clocks remain under Android/OEM control.
