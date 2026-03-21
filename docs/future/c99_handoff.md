# WillEQ C99 Rewrite — Claude Code Handoff

## Project Context

This is a clean-room C99 rewrite of **willeq**, an open-source EverQuest client targeting the Titanium-era protocol (1999-2002 game assets). The original willeq is a working C++ client built on Irrlicht 1.8.5, located at https://github.com/wcassis/willeq. It achieved 45-48fps on an OrangePi One (Allwinner H3, Mali-400MP2, 512MB RAM) at 720p with GLES 2.0, ETC1 texture atlases, PVS culling, per-vertex lighting, and per-pixel player light.

**Why the rewrite:** The Irrlicht dependency created a class of bugs that were extremely difficult to diagnose remotely. Mutex contention on the main thread caused FPS drops to <15 that took many debugging sessions to find. Irrlicht's retained-mode scene graph hides work — scene node leaks, material state, internal synchronization — in ways that external profiling tools and internal instrumentation couldn't easily surface. The new architecture eliminates these problems by owning every GL call and every line of the render loop.

**What carries over as reference (not source):** The existing willeq C++ code serves as a specification for S3D/PFS archive parsing, WLD fragment handling, DaybreakConnection protocol, ETC1 atlas generation, BSP/PVS data extraction, and coordinate transforms (EQ uses Y-up, the renderer uses Z-up: `EQ(x,y,z) → render(x,z,y)`). Reimplement in C99, don't port.

**The offline tooling** (zone atlas builder, zone analysis tools) produces data files consumed by the runtime. These tools can remain C++ or be ported later — they're build-time dependencies, not runtime.

---

## Architecture Overview

The core principle: **game state and rendering are fully decoupled via read-only snapshots.** The main thread owns game state (network, entity tracking, game logic) and publishes a snapshot each tick. Renderer threads consume snapshots independently. Zero renderers (headless) is a valid configuration.

```
Main Thread              Render Thread(s)          I/O Thread
───────────              ─────────────────          ──────────
network poll             read snapshot (atomic)     sem_wait
game state update        cull + sort (shared code)  pread from pack file
snapshot publish ──────► render frame               decompress
  (atomic swap)          eglSwapBuffers             stage result ──► main thread polls
sleep                    
```

**Threading rules (non-negotiable):**
- The main thread NEVER blocks on a mutex or any synchronization primitive.
- Render threads NEVER block on a mutex. They read the snapshot via atomic load.
- The I/O thread is the ONLY thread that does blocking syscalls (pread, decompression).
- The main thread polls I/O completion via atomic load on request state — never blocks.
- If `pthread_mutex_lock` is ever called on the main or render threads, it is a bug.

---

## Snapshot System

The snapshot is the ONLY data bridge between game logic and rendering. It contains everything a renderer needs to draw a complete frame — no pointers into game state, no shared allocations, no back-references.

```c
// snapshot.h

typedef struct {
    uint16_t    spawn_id;
    vec3        position;
    vec3        prev_position;   // for interpolation between ticks
    float       heading;
    uint16_t    model_id;
    uint16_t    anim_id;
    float       anim_time;
    uint8_t     hp_percent;
    uint8_t     flags;           // PVP, AFK, LFG, GM, etc.
    char        name[64];
} EntitySnap;

typedef struct {
    uint32_t frame_id;
    int             entity_count;
    EntitySnap     *entities;
    int             spell_count;
    SpellSnap      *spells;
    ZoneSnap        zone;         // zone id, fog, sky, lighting, region data
    PlayerSnap      player;       // camera pos/heading, hp, mana, buffs, target
    GroupSnap       group;
    ChatSnap        chat;         // new messages this tick
    double          server_time;
} WorldSnapshot;

// Double-buffered: main thread writes back, renderers read front
typedef struct {
    WorldSnapshot   buffers[2];
    _Atomic int     front;
} SnapshotSwap;

// Main thread calls this after updating game state
void snapshot_publish(SnapshotSwap *swap, const GameState *gs);

// Any renderer thread calls this — never blocks
const WorldSnapshot *snapshot_read(const SnapshotSwap *swap);
```

Renderers interpolate between `prev_position` and `position` using their own frame timing. This allows renderers to run at independent frame rates without jitter.

---

## Renderer Interface

Every renderer backend implements a vtable. The renderer manager owns threads and lifecycle:

```c
// render_iface.h

typedef struct Renderer Renderer;

typedef struct {
    const char *name;
    Renderer *(*create)(const RendererConfig *cfg);
    void      (*destroy)(Renderer *r);
    void      (*render_frame)(Renderer *r, const WorldSnapshot *snap);
    void      (*handle_resize)(Renderer *r, int w, int h);
    void      (*notify)(Renderer *r, int event_type, const void *data);
} RendererOps;

struct Renderer {
    const RendererOps *ops;
    int                id;
    bool               running;
};

// Each backend casts Renderer* to its own struct (base must be first member)
```

The renderer manager spawns one thread per attached renderer:

```c
// render_mgr.h
typedef struct RenderMgr RenderMgr;

RenderMgr *render_mgr_create(const SnapshotSwap *snap);
void       render_mgr_destroy(RenderMgr *mgr);
int        render_mgr_attach(RenderMgr *mgr, const RendererOps *ops,
                             const RendererConfig *cfg);
void       render_mgr_detach(RenderMgr *mgr, int renderer_id);
```

Each renderer thread loop:
1. Read snapshot (atomic load)
2. If new frame_id: cull, sort, render, swap
3. If same frame_id: yield (nanosleep 1ms)

---

## Renderer Backends

### GL Renderer (shared ES 2.0 / Desktop GL)

A single codebase with a compatibility shim:

```c
// gl_compat.h
#ifdef USE_GLES2
  #include <GLES2/gl2.h>
  #include <EGL/egl.h>
  #define GL_SHADER_HEADER "#version 100\nprecision mediump float;\n"
#else
  #include <GL/gl.h>
  #define GL_SHADER_HEADER "#version 120\n"
#endif
```

**GL renderer struct** (first member is Renderer base):
```c
typedef struct {
    Renderer        base;
    GLPlatform     *platform;    // EGL/GLX/WGL context — platform-specific
    ShaderCache     shaders;
    TexturePool     textures;
    MeshPool        meshes;
    // Sub-renderers
    // ... zone, entity, spell, sky, ui rendering state
} GLRenderer;
```

**Key GL rendering facts from willeq experience:**
- Mali-400 (primary target): per-vertex lighting, ETC1 textures (native HW decode), 64MB texture budget, 5k-10k polys at 30fps minimum
- ETC1 has no alpha channel. Use a separate alpha atlas texture with dual-texture sampling. ~90% of EQ textures (originally DXT1) have no alpha at all.
- Material-sorted draw calls minimize state changes: sort by shader, then texture, then VBO.
- Static VBOs (`GL_STATIC_DRAW`) for zone geometry. Dynamic VBO for particles.
- Front-to-back opaque rendering, back-to-front alpha rendering.
- CMA (contiguous memory allocation) matters on OrangePi — avoid copies, let textures go directly to GPU-visible memory.
- Shader permutations for vertex light counts (Mali-400 has 512 instruction limit, branches are flattened — generate shader variants for 0/1/2/3/4 lights instead of branching).

**GL platform layer** — context creation is the ONLY platform-specific GL code:
```c
// gl_platform.h
typedef struct GLPlatform GLPlatform;
typedef struct {
    GLPlatform *(*create)(const GLPlatformConfig *cfg);
    void        (*destroy)(GLPlatform *p);
    void        (*swap)(GLPlatform *p);
    void        (*get_size)(GLPlatform *p, int *w, int *h);
} GLPlatformOps;

// Implementations:
extern const GLPlatformOps gl_platform_x11_egl;   // X11 + EGL
extern const GLPlatformOps gl_platform_drm_gbm;   // DRM/KMS + GBM + EGL (no X)
extern const GLPlatformOps gl_platform_android;    // ANativeWindow + EGL
extern const GLPlatformOps gl_platform_ios;        // EAGL / UIView
extern const GLPlatformOps gl_platform_x11_glx;    // Desktop GL on X11
extern const GLPlatformOps gl_platform_win32_wgl;  // Desktop GL on Windows
```

### Software Renderer

Entirely self-contained — no GPU API. Transforms vertices, rasterizes triangles, writes to a framebuffer in memory. Target: 320x200 @ 15-20fps on a DX4-100 class CPU, or higher resolutions on modern hardware.

```c
typedef struct {
    Renderer    base;
    uint16_t   *framebuffer;     // RGB565, owned
    uint16_t   *depthbuffer;     // 16-bit Z
    int         width, height;
    SoftTarget *target;          // how pixels reach the display
    SoftVertex  xform_verts[MAX_ZONE_VERTS]; // scratch space
    SoftTexture textures[MAX_TEXTURES];
} SoftRenderer;
```

Rasterizer: half-space or scanline, affine texture mapping (perspective correction not worth it at 320x200 with EQ polygon sizes). RGB565 color, 16-bit Z.

**Software target layer** — how the framebuffer reaches the display:
```c
typedef struct SoftTarget SoftTarget;
typedef struct {
    SoftTarget *(*create)(int w, int h, const TargetConfig *cfg);
    void        (*destroy)(SoftTarget *t);
    void        (*present)(SoftTarget *t, const uint16_t *pixels);
} SoftTargetOps;

extern const SoftTargetOps soft_target_fbdev;   // /dev/fb0 direct write
extern const SoftTargetOps soft_target_x11;     // XShmPutImage
extern const SoftTargetOps soft_target_win32;   // StretchDIBits
extern const SoftTargetOps soft_target_sdl;     // fallback
```

### Text Renderer

Console/headless output. Chat messages, entity positions (ASCII map), game state. No graphics dependencies at all. Useful for bots, monitoring, debugging (run simultaneously with GL renderer to verify spatial state via text).

---

## Shared Rendering Code (Backend-Independent)

Frustum culling, PVS culling, and draw sorting operate on snapshot data — no graphics API needed. All renderers call these:

```c
// render/cull.h
CullResult cull_scene(const WorldSnapshot *snap);

// render/sort.h
DrawList sort_draws(const CullResult *cull);
```

The CullResult contains lists of visible regions and entities with precomputed distances. The DrawList is sorted by material (shader → texture → VBO) for the GL renderers, or by depth for the software renderer.

---

## File I/O

A single dedicated I/O thread with a request queue. No async I/O API (io_uring, AIO) — a thread with `pread` is portable, simple, and handles decompression on the same thread:

```c
// io.h
typedef struct IOSystem IOSystem;

IOSystem *io_create(void);
void      io_destroy(IOSystem *io);
int       io_request(IOSystem *io, int fd, void *buf,
                     size_t size, off_t offset, void *userdata);
int       io_poll(IOSystem *io, IOCompletion *out, int max);
```

The I/O thread does: `pread` → decompress (LZ4 or zlib) → mark complete (atomic store). The main thread polls completions via `io_poll` which does non-blocking atomic loads. The main thread NEVER blocks on I/O.

Pack files: single `.dat` file per zone, TOC loaded at startup. Assets accessed by offset into a single fd.

---

## Main Loop

```c
int main(int argc, char **argv) {
    Config cfg = config_parse(argc, argv);

    GameState *gs    = game_state_create();
    Connection *conn = connection_create(cfg.server, cfg.port);
    AssetDB *assets  = asset_db_open(cfg.asset_path);

    SnapshotSwap snap = {0};
    RenderMgr *rmgr  = render_mgr_create(&snap);

    // Attach renderers based on config
    if (cfg.renderer & RENDER_GL)
        render_mgr_attach(rmgr, render_gl(), &cfg.gl_cfg);
    if (cfg.renderer & RENDER_SOFT)
        render_mgr_attach(rmgr, render_soft(), &cfg.soft_cfg);
    if (cfg.renderer & RENDER_TEXT)
        render_mgr_attach(rmgr, render_text(), &cfg.text_cfg);
    // cfg.renderer == 0 → headless, valid

    double dt = 1.0 / 20.0;  // EQ server tick ~20hz
    double accumulator = 0;
    double last = platform_time();

    while (gs->running) {
        double now = platform_time();
        accumulator += fmin(now - last, 0.25);
        last = now;

        connection_poll(conn, gs);

        while (accumulator >= dt) {
            game_state_update(gs, dt);
            accumulator -= dt;
        }

        snapshot_publish(&snap, gs);
        platform_sleep_ms(5);
    }

    render_mgr_destroy(rmgr);
    connection_destroy(conn);
    game_state_destroy(gs);
    asset_db_close(assets);
}
```

The main loop has NO rendering code. Headless mode is the same binary with no renderers attached.

---

## Project Structure

```
src/
├── core/                      # No rendering, no platform deps
│   ├── game_state.h/c         # Authoritative state from network
│   ├── snapshot.h/c           # Snapshot production
│   ├── net/
│   │   ├── protocol.h/c       # EQ packet parse/build
│   │   ├── connection.h/c     # Session, sequencing, ack
│   │   ├── udp_transport.h    # Platform-independent UDP interface
│   │   ├── udp_posix.c        # POSIX raw UDP socket implementation
│   │   ├── udp_win32.c        # Win32 implementation
│   │   └── login.h/c          # Login flow state machine
│   ├── entity/
│   │   ├── entity.h/c         # Spawn tracking
│   │   └── anim.h/c           # Animation state (not rendering)
│   ├── spell/
│   │   └── spell_state.h/c
│   ├── inventory.h/c
│   ├── chat.h/c
│   └── zone/
│       └── zone_state.h/c     # Zone identity, fog, sky params
│
├── render/
│   ├── render_iface.h         # RendererOps vtable
│   ├── render_mgr.h/c         # Thread management
│   ├── cull.h/c               # Frustum + PVS culling (shared)
│   ├── sort.h/c               # Draw sorting (shared)
│   │
│   ├── gl/                    # GL ES 2.0 + Desktop GL
│   │   ├── gl_compat.h        # ES2 vs desktop shim
│   │   ├── gl_renderer.c      # Create/destroy, frame dispatch
│   │   ├── gl_zone.c          # Zone geometry rendering
│   │   ├── gl_entity.c        # Skeletal mesh rendering
│   │   ├── gl_spell.c         # Particle system
│   │   ├── gl_ui.c            # UI rendering
│   │   ├── gl_sky.c           # Skydome
│   │   ├── gl_shader.c        # Shader compile/cache/permutations
│   │   └── gl_texture.c       # Texture upload, ETC1 handling
│   │
│   ├── soft/                  # Software renderer
│   │   ├── soft_renderer.c    # Rasterizer, transform pipeline
│   │   ├── soft_zone.c
│   │   ├── soft_entity.c
│   │   └── soft_texture.c
│   │
│   └── text/                  # Console/headless
│       └── text_renderer.c
│
├── platform/
│   ├── platform.h             # Time, threads, file I/O
│   ├── platform_posix.c
│   ├── platform_win32.c
│   ├── gl_platform/
│   │   ├── gl_platform.h
│   │   ├── gl_platform_x11_egl.c
│   │   ├── gl_platform_drm_gbm.c
│   │   ├── gl_platform_android.c
│   │   ├── gl_platform_ios.c
│   │   ├── gl_platform_x11_glx.c
│   │   └── gl_platform_win32_wgl.c
│   └── soft_target/
│       ├── soft_target.h
│       ├── soft_target_fbdev.c
│       ├── soft_target_x11.c
│       └── soft_target_win32.c
│
├── asset/
│   ├── asset.h/c              # Pack file TOC, async request interface
│   ├── s3d.h/c                # S3D/PFS archive format
│   ├── wld.h/c                # WLD fragment parser
│   └── io.h/c                 # I/O thread, request queue
│
├── util/
│   ├── math3d.h               # vec3, mat4, quat (inline)
│   ├── str.h/c                # Simple string type
│   ├── array.h                # DEFINE_ARRAY macro
│   ├── hash.h/c               # uint32 → void* map
│   └── log.h/c                # Logging
│
├── debug/
│   ├── prof.h                 # PROF_SCOPE macro (scoped timers)
│   ├── lock_prof.h            # ProfiledMutex wrapper
│   ├── thread_safety.h        # Hot-thread mutex detection
│   ├── scene_audit.h/c        # Scene census, invariant checking
│   ├── entity_tracker.h/c     # Spawn/despawn leak detection
│   ├── cull_validate.h/c      # Brute-force culling verification
│   ├── render_manifest.h/c    # Text description of rendered scene
│   └── debug_state.h          # Global debug struct (g_debug)
│
└── main.c
```

---

## Build System

CMake with renderer/platform selection:

```cmake
project(willeq2 C)
set(CMAKE_C_STANDARD 99)

# Always: debug symbols + frame pointers (required for perf/gdb)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fno-omit-frame-pointer")

option(BUILD_RENDERER_GL "Build OpenGL renderer" ON)
option(BUILD_RENDERER_SOFT "Build software renderer" ON)
option(BUILD_RENDERER_TEXT "Build text/headless renderer" ON)
option(USE_GLES2 "Use OpenGL ES 2.0 instead of desktop GL" OFF)
option(DEBUG_THREAD_SAFETY "Enable hot-thread mutex detection" ON)
option(DEBUG_PROF_SCOPES "Enable scoped profiling timers" ON)
option(DEBUG_LOCK_PROF "Enable mutex contention profiling" ON)

# Core sources — always built
file(GLOB_RECURSE CORE_SRC src/core/*.c src/asset/*.c src/util/*.c src/debug/*.c)

# Platform
if(WIN32)
    list(APPEND PLAT_SRC src/platform/platform_win32.c)
else()
    list(APPEND PLAT_SRC src/platform/platform_posix.c)
endif()

# Renderers
if(BUILD_RENDERER_GL)
    add_definitions(-DHAS_GL_RENDERER)
    file(GLOB GL_SRC src/render/gl/*.c)
    if(USE_GLES2)
        add_definitions(-DUSE_GLES2)
        # Select appropriate platform files for EGL targets
    else()
        # Select appropriate platform files for GLX/WGL targets
    endif()
endif()

if(BUILD_RENDERER_SOFT)
    add_definitions(-DHAS_SOFT_RENDERER)
    file(GLOB SOFT_SRC src/render/soft/*.c)
    # Select appropriate soft_target platform files
endif()

if(BUILD_RENDERER_TEXT)
    add_definitions(-DHAS_TEXT_RENDERER)
    list(APPEND TEXT_SRC src/render/text/text_renderer.c)
endif()
```

**Target build examples:**
- OrangePi (primary): `cmake -DUSE_GLES2=ON -DBUILD_RENDERER_SOFT=OFF`
- Headless server: `cmake -DBUILD_RENDERER_GL=OFF -DBUILD_RENDERER_SOFT=OFF`
- DX4-100 retro: `cmake -DBUILD_RENDERER_GL=OFF -DBUILD_RENDERER_SOFT=ON` (fbdev target)
- Desktop Linux: `cmake -DUSE_GLES2=OFF` (both GL + soft, simultaneous)

**Compile flags (always, including release):**
- `-g` — debug symbols (required for perf, gdb)
- `-fno-omit-frame-pointer` — required for perf call graphs and gdb backtraces on ARM
- `-O2` — optimization (compatible with -g)

---

## Debugging Strategy

### Critical lesson from willeq: external tools first, internal instrumentation second.

**Diagnosis (when something is wrong and you don't know why):** Use Linux tools. Do NOT try to find the problem by adding more internal instrumentation.

| Problem | Tool | Command |
|---|---|---|
| Steady-state slow FPS | `perf record` | `perf record -g --call-graph fp -p PID -- sleep 10` |
| Intermittent stalls/spikes | `perf trace` | `perf trace -p PID --duration 5` (only shows syscalls >5ms) |
| Mutex contention | `perf lock` or `mutrace` | `perf lock record -p PID -- sleep 5; perf lock report` |
| Thread sleeping/blocked | `perf sched` | `perf sched record -p PID -- sleep 5; perf sched latency --sort max` |
| GPU driver stalls | `strace -T` | `strace -T -tt -e ioctl -p PID` then grep for slow ioctls |
| Kernel-level events | `trace-cmd` | `trace-cmd record -e sched:sched_switch -p PID sleep 5` |
| Memory leaks | `valgrind` | `valgrind --leak-check=full ./willeq` |
| Thermal throttling | sysfs | `cat /sys/class/thermal/thermal_zone0/temp` |
| Off-CPU analysis | `offcputime` (BCC) | `sudo offcputime-bpfcc -p PID -f 5` |

**A debug shell script with all modes should exist before the first line of game code.** See the `debug_willeq.sh` template below.

### Validation (catching bugs at the moment they happen): Use internal instrumentation.

**Always-on scoped timers with threshold alerts:**

```c
// debug/prof.h
typedef struct {
    const char *label;
    const char *file;
    int         line;
    double      start;
    double      threshold_ms;
} ScopeTimer;

static inline ScopeTimer scope_begin(const char *label,
                                      const char *file, int line,
                                      double threshold_ms) {
    return (ScopeTimer){label, file, line, platform_time_ms(), threshold_ms};
}

static inline void scope_end(ScopeTimer *t) {
    double elapsed = platform_time_ms() - t->start;
    if (elapsed > t->threshold_ms) {
        log_warn("SLOW [%6.2fms > %.1fms] %s (%s:%d)",
                 elapsed, t->threshold_ms, t->label, t->file, t->line);
        log_warn("  frame %u: %d entities, %d regions_vis, %d draws, %d tris",
                 g_debug.last_frame.frame_id,
                 g_debug.last_frame.entity_count,
                 g_debug.last_frame.regions_in_frustum,
                 g_debug.last_frame.draw_calls,
                 g_debug.last_frame.triangles_drawn);
    }
}

#define PROF_SCOPE(label, threshold_ms) \
    ScopeTimer _prof_##__LINE__ \
        __attribute__((cleanup(scope_end))) = \
        scope_begin(label, __FILE__, __LINE__, threshold_ms)
```

Usage: `PROF_SCOPE("zone_render", 8.0);` at the top of any function. Zero overhead when fast, loud when slow.

**Profiled mutex wrapper (catches contention):**

```c
// debug/lock_prof.h
typedef struct {
    pthread_mutex_t mutex;
    const char     *name;
    _Atomic int     contention_count;
    _Atomic double  worst_wait_ms;
} ProfiledMutex;

// Uses trylock first — if it fails, times the blocking wait.
// Logs any wait over 1ms with file/line of caller.
```

**Hot-thread mutex detection (catches architectural violations):**

```c
// debug/thread_safety.h
// In debug builds, wraps pthread_mutex_lock to assert it's never
// called from the main thread (role=1) or render threads (role=2).
// Set tl_thread_role at top of each thread's entry function.
```

If a mutex is ever taken on a hot thread, it logs immediately. The willeq mutex bug would have been caught on the first frame it occurred.

**Scene census and entity tracking:**

```c
// debug/scene_audit.h
// Counts actual scene objects vs expected counts from snapshot.
// Detects: leaked nodes, duplicate spawns, orphaned objects.
// Run every 60 frames (once per second) or on FPS dip.

// debug/entity_tracker.h
// Logs every spawn/despawn. Detects duplicate spawn_ids
// (spawn without preceding despawn = leaked scene node).
```

**Culling validation:**

```c
// debug/cull_validate.h
// Brute-force frustum test on all regions, compares against
// fast cull result. Reports false positives (rendering things
// outside frustum = performance waste) and false negatives.
// Run once per second, not every frame.
```

**Render manifest (text description of what's drawn):**

```c
// debug/render_manifest.h
// On command, outputs: camera pos/heading, visible regions with
// tri counts and materials, visible entities with positions,
// draw list with shader/texture/VBO/tri count per call.
// Allows remote diagnosis without screenshots.
```

**Global debug state (always inspectable from GDB):**

```c
// debug/debug_state.h
typedef struct {
    FrameLog             last_frame;
    int                  current_shader;
    int                  current_texture;
    int                  current_vbo;
    int                  draw_call_count;
    const WorldSnapshot *current_snap;
    const CullResult    *current_cull;
    FrameAssertions      asserts;
} DebugState;

extern DebugState g_debug;
// From GDB: print g_debug.last_frame — complete frame stats
```

---

## Debug Shell Script (create on day one)

```bash
#!/bin/bash
# debug_willeq.sh — create this BEFORE writing game code

MODE=${1:-run}
WILLEQ=./willeq2
CONFIG=./config.json

case $MODE in
    run)
        $WILLEQ --config $CONFIG
        ;;
    gdb)
        echo "Waiting for GDB on port 9999..."
        gdbserver :9999 $WILLEQ --config $CONFIG
        ;;
    perf)
        $WILLEQ --config $CONFIG &
        PID=$!; sleep 2
        echo "Recording perf for ${2:-10}s..."
        perf record -g --call-graph fp -p $PID -- sleep ${2:-10}
        kill $PID 2>/dev/null
        perf report --stdio --no-children > perf_report.txt
        perf report --stdio -g --no-children > perf_callgraph.txt
        echo "Done: perf_report.txt, perf_callgraph.txt"
        ;;
    stalls)
        $WILLEQ --config $CONFIG &
        PID=$!; sleep 3
        echo "Recording stall diagnostics for ${2:-15}s..."
        perf trace -p $PID --duration 5 2> perf_trace.txt &
        TRACE_PID=$!
        perf sched record -p $PID -o sched.data -- sleep ${2:-15}
        kill $TRACE_PID 2>/dev/null; wait $TRACE_PID 2>/dev/null
        perf sched latency -i sched.data --sort max > sched_latency.txt
        echo "=== Slow syscalls (>5ms): ==="
        cat perf_trace.txt
        echo "=== Worst scheduling delays: ==="
        head -20 sched_latency.txt
        kill $PID 2>/dev/null
        ;;
    lock)
        perf lock record -p $(pidof willeq2) -- sleep ${2:-5}
        perf lock report > lock_report.txt
        echo "Done: lock_report.txt"
        ;;
    mutrace)
        mutrace $WILLEQ --config $CONFIG 2>&1 | tee mutrace.txt
        ;;
    thermal)
        $WILLEQ --config $CONFIG &
        PID=$!
        echo "timestamp,temp_c,cpu_freq_mhz" > thermal_log.csv
        while kill -0 $PID 2>/dev/null; do
            TEMP=$(cat /sys/class/thermal/thermal_zone0/temp)
            FREQ=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq)
            echo "$(date +%s.%N),$((TEMP/1000)),$((FREQ/1000))" >> thermal_log.csv
            sleep 0.5
        done
        ;;
    gpu)
        strace -T -tt -e trace=ioctl -p $(pidof willeq2) 2>&1 | \
            grep -E 'DRM|<0\.0[2-9]|<0\.[1-9]|<[1-9]' | tee gpu_stalls.txt
        ;;
    strace)
        strace -T -tt -f -e trace=ioctl,futex,read,write,mmap,munmap,poll \
            -p $(pidof willeq2) 2>&1 | tee strace_timed.txt
        ;;
    memcheck)
        valgrind --tool=memcheck --leak-check=full \
            $WILLEQ --config $CONFIG 2>&1 | tee valgrind.txt
        ;;
esac
```

---

## Remote Debugging Setup

The OrangePi runs `gdbserver`. Claude Code connects via SSH and drives GDB remotely:

```
Claude Code terminal ──► ssh orangepi ──► gdbserver :9999 ./willeq2
Claude Code terminal ──► gdb-multiarch -ex "target remote orangepi.local:9999"
```

This allows stepping through execution, inspecting variables, setting breakpoints — all without the user relaying information manually.

Ensure SSH access from the development machine to the OrangePi is working before starting implementation.

---

## Utility Types Needed

**String:**
```c
typedef struct { char *data; int len; int cap; } Str;
Str   str_from(const char *cstr);
Str   str_fmt(const char *fmt, ...);
void  str_append(Str *s, const char *data, int len);
void  str_free(Str *s);
int   str_find(Str s, const char *needle);
Str   str_slice(Str s, int start, int end);
```

**Dynamic array (macro-generated, type-safe):**
```c
#define DEFINE_ARRAY(Name, Type)                                    \
    typedef struct { Type *data; int len, cap; } Name;              \
    static inline void Name##_push(Name *a, Type v) {              \
        if (a->len == a->cap) {                                     \
            a->cap = a->cap ? a->cap * 2 : 8;                     \
            a->data = realloc(a->data, a->cap * sizeof(Type));    \
        }                                                           \
        a->data[a->len++] = v;                                     \
    }                                                               \
    static inline void Name##_free(Name *a) { free(a->data); }
```

**Hash map:** `uint32_t` key → `void*` value. ~80 lines. Used for asset lookup by name hash.

**Math:** `vec3`, `mat4`, `quat` as inline functions in `math3d.h`. No library dependency.

---

## Module Design Principles

- Every `.h` file exposes opaque types and a narrow function API. Internals are in the `.c` file only.
- No module in `core/` includes anything from `render/`.
- No module in `render/gl/` includes anything from `render/soft/` or vice versa.
- Each `.c` file should fit in a context window (aim for <500 lines, hard limit 1000).
- Named constants everywhere — no magic numbers. GDB inspection must be self-explanatory.
- Important state lives in structs reachable from function parameters or well-known globals, not static locals.

---

## EQ-Specific Technical Reference

**Coordinate system:** EQ uses (x, y, z) with Y-up. The renderer should use (x, z, y) — swap Y↔Z on import from game state.

**S3D/PFS archives:** ZIP-like container format. Contains WLD files, textures (DXT1 originally), and other assets. One per zone plus `_chr.s3d` files for character models.

**WLD fragments:** The zone geometry format. Key fragments:
- 0x21: BSP tree structure
- 0x22: BSP region — contains bounding box, geometry reference, and RLE-encoded PVS (nearby visible regions)
- 0x36: Mesh data (vertices, indices, UV coords, normals)
- Texture references chain through several fragment types

**PVS (Potentially Visible Set):** Pre-computed per-region visibility stored in WLD 0x22 fragments as RLE-encoded bitfield. Determines which regions are potentially visible from any given region. This is the primary culling mechanism before frustum culling.

**DaybreakConnection protocol:** SOE/Daybreak reliable UDP protocol with sequencing, acks, combined packets, oversized packet fragmentation, CRC, and optional compression. The existing willeq C++ implementation in `DaybreakConnection.cpp` is the reference. The raw UDP transport layer has already been separated from libuv into a clean platform interface.

**ETC1 texture atlas pipeline:** An offline tool reads DXT1 textures from S3D archives, transcodes to ETC1, and packs them into 2048×2048 atlas pages with a metadata file mapping original texture names to atlas UV coordinates. The runtime loads these atlas pages directly — one `glCompressedTexImage2D` call per atlas page. ~90% of textures have no alpha; the ~10% with alpha use a separate alpha-only atlas page.

---

## Implementation Phases

### Phase 0: Foundation (do this first)
1. Project skeleton: directories, CMakeLists.txt, main.c that does nothing
2. `debug_willeq.sh` script (before any game code)
3. `platform.h/c` — `platform_time()`, `platform_sleep_ms()`, `platform_time_ms()`
4. `log.h/c` — basic logging to stderr
5. `debug/prof.h` — PROF_SCOPE macro
6. `debug/lock_prof.h` — ProfiledMutex
7. `debug/thread_safety.h` — hot-thread mutex detection
8. `debug/debug_state.h` — g_debug global
9. Verify: builds, runs, exits cleanly, debug script works

### Phase 1: Snapshot + Headless Loop
1. `util/` — str, array, hash, math3d
2. `core/game_state.h/c` — minimal game state struct
3. `core/snapshot.h/c` — snapshot structs, double-buffer swap
4. `render/render_iface.h` — RendererOps vtable
5. `render/render_mgr.h/c` — thread spawn, attach/detach
6. `render/text/text_renderer.c` — prints snapshot stats each frame
7. Main loop: create game state, publish snapshots, text renderer prints them
8. Verify: runs headless, text renderer shows ticking frame IDs

### Phase 2: GL Context + Clear Screen
1. `platform/gl_platform/gl_platform_x11_egl.c` (or drm_gbm for OrangePi)
2. `render/gl/gl_renderer.c` — create context, clear to blue, swap
3. Verify: blue window appears on OrangePi at target resolution

### Phase 3: Zone Geometry
1. `asset/s3d.h/c` — S3D archive reader (reference: willeq s3d_loader.cpp)
2. `asset/wld.h/c` — WLD fragment parser (reference: willeq wld_loader.cpp)
3. `render/gl/gl_zone.c` — upload zone mesh VBOs, draw with basic shader
4. `render/gl/gl_shader.c` — shader compile, cache
5. `render/gl/gl_texture.c` — ETC1 atlas loader, texture upload
6. `render/cull.h/c` — PVS + frustum culling
7. `render/sort.h/c` — material sort
8. `debug/scene_audit.h/c` — region count validation
9. `debug/cull_validate.h/c` — brute-force cull verification
10. Verify: zone renders on OrangePi, FPS ≥ 30, scene audit clean

### Phase 4: Network + Entities
1. `core/net/udp_transport.h`, `udp_posix.c` — raw UDP socket
2. `core/net/connection.h/c` — DaybreakConnection protocol
3. `core/net/protocol.h/c` — EQ packet parsing
4. `core/entity/entity.h/c` — spawn tracking from network
5. `render/gl/gl_entity.c` — entity mesh rendering (start with placeholder cubes)
6. `debug/entity_tracker.h/c` — spawn/despawn leak detection
7. Verify: connect to EQEmu server, see entities as cubes in zone

### Phase 5: Full Rendering
1. Skeletal animation, character models from _chr.s3d
2. `render/gl/gl_sky.c` — skydome
3. `render/gl/gl_spell.c` — particle system
4. `render/gl/gl_ui.c` — EQ-style UI
5. `render/gl/gl_shader.c` — shader permutations for light counts
6. Per-vertex lighting, fog, per-pixel player light
7. `asset/io.h/c` — I/O thread for streaming (if needed for zone transitions)
8. `debug/render_manifest.h/c`

### Phase 6: Software Renderer
1. `render/soft/soft_renderer.c` — vertex transform, rasterizer
2. `platform/soft_target/soft_target_fbdev.c` — direct framebuffer output
3. Target: 320×200 rendering on minimal hardware

### Phase 7: Additional Platforms
1. Win32 platform layer
2. Android (EGL + ANativeWindow)
3. Desktop GL (GLX/WGL via gl_compat.h shim)

---

## Key Reminders

- **Build with `-g -fno-omit-frame-pointer` always.** Even release builds. Non-negotiable. Without this, perf and gdb are useless on ARM.
- **The main thread and render threads never take a mutex.** If you need synchronization, use atomics. If you can't use atomics, the design is wrong.
- **External tools (perf, strace, mutrace) for diagnosis. Internal instrumentation (PROF_SCOPE, scene audit, entity tracker) for validation.** Don't confuse these roles.
- **Each source file should fit in a context window.** If a file exceeds 500 lines, consider splitting it.
- **The original willeq codebase is reference, not source.** Look at it to understand the EQ protocol, S3D format, WLD fragments, coordinate transforms. Reimplement in C99 with the new architecture.
- **Test the debug script before testing the game.** If `./debug_willeq.sh perf 5` doesn't produce a clean report, fix that first.
- **Named constants everywhere.** When inspecting state in GDB, every value should be self-explanatory.
