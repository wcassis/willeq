// B955 (GitLab #273) + B962 (GitLab #280): a DIAGNOSTIC LD_PRELOAD shim that
// logs every Lima GEM buffer object a process creates and closes, so the gap
// between the gpu_buffers ledger row (the kernel's count of our GEM pages) and
// the renderer's logical tally (textures + VBOs) can be named per BO.
//
// Not part of any shipped binary. Build it with the ARM image's compiler --
// and override the image's ENTRYPOINT, which otherwise runs the whole client
// build IN-SOURCE in the mounted tree:
//   docker run --rm --entrypoint /bin/bash -v "$PWD:/src" willeq-arm-noble -c
//     "arm-linux-gnueabihf-gcc -O2 -Wall -shared -fPIC
//      -o /src/build/lima_bo_census.so /src/scripts/pi/lima_bo_census.c -ldl -lpthread"
// A setcap binary ignores LD_PRELOAD (secure execution): use an uncapped copy.
//
// Two independent modes, either or both:
//
// 1. B955_BOLOG=<path>: the EVENT log (unchanged since B955). Lines:
//    "<mono_s> C <handle> <size> <flags> fd<n>" (LIMA_GEM_CREATE; flags bit 0
//    = LIMA_BO_FLAG_HEAP: the size is a MAXIMUM, pages grow on GPU demand),
//    "<mono_s> X <handle> fd<n>" (GEM_CLOSE), "<mono_s> I <handle> fd<n>" (a
//    PRIME import: a buffer another device allocated -- the display
//    controller's scanout, CMA, not shmem). scripts/pi/lima_bo_census.py folds
//    a log into the live set at any instant.
//
// 2. B962_GEMDUMP=<path>: the OWNER table. The shim keeps every live BO in a
//    fixed in-process table (no malloc: the client's allocator is the memory
//    ledger, and a diagnostic must not move the figures it explains) with
//      - the OWNER TAG the creating thread had set (a stack of short strings
//        pushed by the client and its GLES2 driver around every GL call that
//        can allocate GPU storage: "tex:etc1 <name> 256x256", "draw3d:client",
//        "swap", ...; empty = Mesa/EGL work outside any tagged call),
//      - the memory-ledger SCOPE active on that thread (the client registers a
//        callback returning eqt::memory's current category name),
//      - whether Mesa has the BO MAPPED for CPU access (DRM_IOCTL_LIMA_GEM_INFO
//        gives the mmap offset; mmap64/munmap are tracked against it).
//    The client calls willeq_gem_dump("<point>") at every memory-ledger point
//    (include/client/core/gem_owner_census.h); each call appends one block:
//      "P\t<point>\t<mono_s>\t<live>\t<bytes>\t<created>\t<closed>"
//      "B\t<fd>\t<handle>\t<size>\t<flags>\t<mapped>\t<seq>\t<tid>\t<scope>\t<tag>"  (one per live BO)
//      "E\t<point>"
//    scripts/pi/lima_bo_census.py --owners folds the blocks into a per-owner
//    table.
//
// Exported API (resolved by the client with dlsym(RTLD_DEFAULT, ...); absent
// shim = every call site is a null check):
//   void willeq_gem_tag_push(const char* tag);
//   void willeq_gem_tag_pop(void);
//   void willeq_gem_set_scope_fn(const char* (*fn)(void));
//   void willeq_gem_dump(const char* point);

#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// linux/ioctl.h encoding ('d' = DRM_IOCTL_BASE); the struct sizes are the
// same on 32-bit ARM and x86_64 (all u32/u64 fields).
#define B955_IOWR(nr, sz) ((3u << 30) | ((unsigned)(sz) << 16) | ('d' << 8) | (nr))
#define B955_IOW(nr, sz)  ((1u << 30) | ((unsigned)(sz) << 16) | ('d' << 8) | (nr))
struct lima_gem_create { uint32_t size, flags, handle, pad; };
struct lima_gem_info { uint32_t handle, va; uint64_t offset; };
struct gem_close { uint32_t handle, pad; };
struct prime_handle { uint32_t handle, flags; int32_t fd; };
#define IOCTL_LIMA_GEM_CREATE B955_IOWR(0x41, sizeof(struct lima_gem_create))
#define IOCTL_LIMA_GEM_INFO   B955_IOWR(0x42, sizeof(struct lima_gem_info))
#define IOCTL_GEM_CLOSE       B955_IOW(0x09, sizeof(struct gem_close))
#define IOCTL_PRIME_FD_TO_HANDLE B955_IOWR(0x2e, sizeof(struct prime_handle))

static int (*real_ioctl)(int, unsigned long, ...);
static int (*real_ioctl_time64)(int, unsigned long, ...);
static void* (*real_mmap64)(void*, size_t, int, int, int, off64_t);
static void* (*real_mmap)(void*, size_t, int, int, int, long);
static int (*real_munmap)(void*, size_t);
static int logfd = -1;
static int dumpfd = -1;

static double mono_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

static void logline(const char* fmt, ...) {
    if (logfd < 0) return;
    char buf[160];
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int n = snprintf(buf, sizeof(buf), "%ld.%06ld ", (long)ts.tv_sec, ts.tv_nsec / 1000);
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    if (n > 0) (void)!write(logfd, buf, (size_t)n);
}

// ---- the owner table (B962) -------------------------------------------------
#define TAG_LEN 96
#define SCOPE_LEN 20
#define TABLE_SIZE 16384            // power of two; the client holds ~1-3k live BOs
#define MAP_TABLE_SIZE 8192
#define TAG_DEPTH 6

struct bo_ent {
    uint8_t used;
    uint8_t mapped;
    uint8_t heap;
    uint8_t pad;
    int32_t fd;
    uint32_t handle;
    uint32_t size;
    uint32_t seq;
    uint32_t tid;
    uint64_t offset;
    char scope[SCOPE_LEN];
    char tag[TAG_LEN];
};
struct map_ent {
    uintptr_t addr;                 // 0 = empty
    int32_t fd;
    uint32_t handle;
};

static struct bo_ent g_table[TABLE_SIZE];
static struct map_ent g_maps[MAP_TABLE_SIZE];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_seq;
static uint64_t g_created, g_closed;
static int g_overflow;
static const char* (*g_scope_fn)(void);

static __thread int t_depth;
static __thread char t_tags[TAG_DEPTH][TAG_LEN];

static uint32_t bo_hash(int fd, uint32_t handle) {
    uint32_t h = handle * 2654435761u ^ (uint32_t)fd * 40503u;
    return h & (TABLE_SIZE - 1);
}
static uint32_t map_hash(uintptr_t a) {
    return (uint32_t)((a >> 12) * 2654435761u) & (MAP_TABLE_SIZE - 1);
}

static struct bo_ent* bo_find(int fd, uint32_t handle) {
    for (uint32_t i = bo_hash(fd, handle), n = 0; n < TABLE_SIZE; i = (i + 1) & (TABLE_SIZE - 1), ++n) {
        if (!g_table[i].used) return NULL;
        if (g_table[i].fd == fd && g_table[i].handle == handle) return &g_table[i];
    }
    return NULL;
}

// Linear probing with backward-shift deletion: no tombstones, so a run with
// millions of create/close pairs (LIMA_DEBUG=nobocache) never degrades.
static void bo_erase(struct bo_ent* e) {
    uint32_t i = (uint32_t)(e - g_table);
    uint32_t j = i;
    for (;;) {
        j = (j + 1) & (TABLE_SIZE - 1);
        if (!g_table[j].used) break;
        uint32_t k = bo_hash(g_table[j].fd, g_table[j].handle);
        // can the entry at j move to the hole at i?
        if ((j > i && (k <= i || k > j)) || (j < i && (k <= i && k > j))) {
            g_table[i] = g_table[j];
            i = j;
        }
    }
    g_table[i].used = 0;
}

static void map_put(uintptr_t addr, int fd, uint32_t handle) {
    for (uint32_t i = map_hash(addr), n = 0; n < MAP_TABLE_SIZE; i = (i + 1) & (MAP_TABLE_SIZE - 1), ++n) {
        if (g_maps[i].addr == 0 || g_maps[i].addr == addr) {
            g_maps[i].addr = addr;
            g_maps[i].fd = fd;
            g_maps[i].handle = handle;
            return;
        }
    }
}

static int map_take(uintptr_t addr, int* fd, uint32_t* handle) {
    for (uint32_t i = map_hash(addr), n = 0; n < MAP_TABLE_SIZE; i = (i + 1) & (MAP_TABLE_SIZE - 1), ++n) {
        if (g_maps[i].addr == 0) return 0;
        if (g_maps[i].addr == addr) {
            *fd = g_maps[i].fd;
            *handle = g_maps[i].handle;
            // backward-shift delete
            uint32_t hole = i, j = i;
            for (;;) {
                j = (j + 1) & (MAP_TABLE_SIZE - 1);
                if (g_maps[j].addr == 0) break;
                uint32_t k = map_hash(g_maps[j].addr);
                if ((j > hole && (k <= hole || k > j)) || (j < hole && (k <= hole && k > j))) {
                    g_maps[hole] = g_maps[j];
                    hole = j;
                }
            }
            g_maps[hole].addr = 0;
            return 1;
        }
    }
    return 0;
}

static void copy_str(char* dst, size_t cap, const char* src) {
    size_t n = 0;
    if (src) {
        for (; src[n] && n + 1 < cap; ++n) {
            char c = src[n];
            dst[n] = (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
        }
    }
    dst[n] = 0;
}

// The tag recorded for a new BO: the innermost tag, prefixed by the
// outermost when they differ ("<outer>|<inner>"), so a texture uploaded
// inside a zone-load step keeps both names.
static void current_tag(char* out, size_t cap) {
    if (t_depth <= 0) { out[0] = 0; return; }
    int top = (t_depth > TAG_DEPTH ? TAG_DEPTH : t_depth) - 1;
    if (top == 0) { copy_str(out, cap, t_tags[0]); return; }
    char tmp[TAG_LEN * 2];
    snprintf(tmp, sizeof(tmp), "%s|%s", t_tags[0], t_tags[top]);
    copy_str(out, cap, tmp);
}

static void table_create(int fd, uint32_t handle, uint32_t size, uint32_t flags) {
    if (dumpfd < 0) return;
    char tag[TAG_LEN];
    current_tag(tag, sizeof(tag));
    const char* scope = g_scope_fn ? g_scope_fn() : "";
    pthread_mutex_lock(&g_lock);
    ++g_created;
    struct bo_ent* e = bo_find(fd, handle);
    if (!e) {
        for (uint32_t i = bo_hash(fd, handle), n = 0; n < TABLE_SIZE; i = (i + 1) & (TABLE_SIZE - 1), ++n) {
            if (!g_table[i].used) { e = &g_table[i]; break; }
        }
    }
    if (!e) {
        g_overflow = 1;
    } else {
        e->used = 1;
        e->mapped = 0;
        e->heap = (uint8_t)(flags & 1u);
        e->fd = fd;
        e->handle = handle;
        e->size = size;
        e->seq = ++g_seq;
        e->tid = (uint32_t)syscall(SYS_gettid);
        e->offset = 0;
        copy_str(e->scope, sizeof(e->scope), scope);
        memcpy(e->tag, tag, sizeof(e->tag));
    }
    pthread_mutex_unlock(&g_lock);
}

static void table_close(int fd, uint32_t handle) {
    if (dumpfd < 0) return;
    pthread_mutex_lock(&g_lock);
    struct bo_ent* e = bo_find(fd, handle);
    if (e) {
        ++g_closed;
        bo_erase(e);
    }
    pthread_mutex_unlock(&g_lock);
}

static void table_info(int fd, uint32_t handle, uint64_t offset) {
    if (dumpfd < 0) return;
    pthread_mutex_lock(&g_lock);
    struct bo_ent* e = bo_find(fd, handle);
    if (e) e->offset = offset;
    pthread_mutex_unlock(&g_lock);
}

static void table_mmap(void* addr, int fd, uint64_t offset) {
    if (dumpfd < 0 || addr == MAP_FAILED || fd < 0) return;
    pthread_mutex_lock(&g_lock);
    // Offsets are unique per fd; a full scan only on a DRM-fd mmap (once per
    // BO, never per frame).
    for (uint32_t i = 0; i < TABLE_SIZE; ++i) {
        if (g_table[i].used && g_table[i].fd == fd && g_table[i].offset == offset && offset != 0) {
            g_table[i].mapped = 1;
            map_put((uintptr_t)addr, fd, g_table[i].handle);
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

static void table_munmap(void* addr) {
    if (dumpfd < 0) return;
    pthread_mutex_lock(&g_lock);
    int fd;
    uint32_t handle;
    if (map_take((uintptr_t)addr, &fd, &handle)) {
        struct bo_ent* e = bo_find(fd, handle);
        if (e) e->mapped = 0;
    }
    pthread_mutex_unlock(&g_lock);
}

// ---- exported API -------------------------------------------------------------
__attribute__((visibility("default"))) void willeq_gem_tag_push(const char* tag) {
    if (t_depth < TAG_DEPTH) copy_str(t_tags[t_depth], TAG_LEN, tag);
    ++t_depth;
}

__attribute__((visibility("default"))) void willeq_gem_tag_pop(void) {
    if (t_depth > 0) --t_depth;
}

__attribute__((visibility("default"))) void willeq_gem_set_scope_fn(const char* (*fn)(void)) {
    g_scope_fn = fn;
}

static void dump_write(const char* s, size_t n) {
    while (n > 0) {
        ssize_t w = write(dumpfd, s, n);
        if (w <= 0) return;
        s += w;
        n -= (size_t)w;
    }
}

__attribute__((visibility("default"))) void willeq_gem_dump(const char* point) {
    if (dumpfd < 0) return;
    char buf[8192];
    size_t used = 0;
    pthread_mutex_lock(&g_lock);
    uint32_t live = 0;
    uint64_t bytes = 0;
    for (uint32_t i = 0; i < TABLE_SIZE; ++i) {
        if (g_table[i].used) { ++live; bytes += g_table[i].size; }
    }
    used += (size_t)snprintf(buf + used, sizeof(buf) - used, "P\t%s\t%.6f\t%u\t%llu\t%llu\t%llu\t%d\n",
                             point ? point : "?", mono_now(), live, (unsigned long long)bytes,
                             (unsigned long long)g_created, (unsigned long long)g_closed, g_overflow);
    for (uint32_t i = 0; i < TABLE_SIZE; ++i) {
        const struct bo_ent* e = &g_table[i];
        if (!e->used) continue;
        if (used + 256 > sizeof(buf)) { dump_write(buf, used); used = 0; }
        used += (size_t)snprintf(buf + used, sizeof(buf) - used, "B\t%d\t%u\t%u\t%u\t%u\t%u\t%u\t%s\t%s\n",
                                 e->fd, e->handle, e->size, (unsigned)e->heap, (unsigned)e->mapped,
                                 e->seq, e->tid, e->scope[0] ? e->scope : "-", e->tag[0] ? e->tag : "-");
    }
    used += (size_t)snprintf(buf + used, sizeof(buf) - used, "E\t%s\n", point ? point : "?");
    dump_write(buf, used);
    pthread_mutex_unlock(&g_lock);
}

// ---- interposers ----------------------------------------------------------------
__attribute__((constructor)) static void init(void) {
    real_ioctl = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
    real_ioctl_time64 = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "__ioctl_time64");
    real_mmap64 = (void* (*)(void*, size_t, int, int, int, off64_t))dlsym(RTLD_NEXT, "mmap64");
    real_mmap = (void* (*)(void*, size_t, int, int, int, long))dlsym(RTLD_NEXT, "mmap");
    real_munmap = (int (*)(void*, size_t))dlsym(RTLD_NEXT, "munmap");
    const char* p = getenv("B955_BOLOG");
    if (p) logfd = open(p, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC, 0644);
    const char* d = getenv("B962_GEMDUMP");
    if (d) dumpfd = open(d, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC, 0644);
}

static void observe_ioctl(int fd, unsigned long req, void* arg) {
    if (req == IOCTL_LIMA_GEM_CREATE) {
        const struct lima_gem_create* c = arg;
        logline("C %u %u %u fd%d\n", c->handle, c->size, c->flags, fd);
        table_create(fd, c->handle, c->size, c->flags);
    } else if (req == IOCTL_LIMA_GEM_INFO) {
        const struct lima_gem_info* g = arg;
        table_info(fd, g->handle, g->offset);
    } else if (req == IOCTL_GEM_CLOSE) {
        uint32_t h = ((const struct gem_close*)arg)->handle;
        logline("X %u fd%d\n", h, fd);
        table_close(fd, h);
    } else if (req == IOCTL_PRIME_FD_TO_HANDLE) {
        logline("I %u fd%d\n", ((const struct prime_handle*)arg)->handle, fd);
    }
}

// Both libc names: a 64-bit-time_t build (Ubuntu armhf, which libdrm is)
// calls __ioctl_time64, an older one plain ioctl. The DRM ioctls carry no
// time_t, so either real entry point serves both.
int shim_ioctl(int fd, unsigned long req, ...) __asm__("ioctl");
int shim_ioctl_time64(int fd, unsigned long req, ...) __asm__("__ioctl_time64");

int shim_ioctl(int fd, unsigned long req, ...) {
    va_list ap;
    va_start(ap, req);
    void* arg = va_arg(ap, void*);
    va_end(ap);
    int r = real_ioctl(fd, req, arg);
    if (r == 0 && arg) observe_ioctl(fd, req, arg);
    return r;
}

int shim_ioctl_time64(int fd, unsigned long req, ...) {
    va_list ap;
    va_start(ap, req);
    void* arg = va_arg(ap, void*);
    va_end(ap);
    int r = (real_ioctl_time64 ? real_ioctl_time64 : real_ioctl)(fd, req, arg);
    if (r == 0 && arg) observe_ioctl(fd, req, arg);
    return r;
}

// Exported under the libc names with asm labels: on a 64-bit-off_t build
// (Ubuntu armhf) <sys/mman.h> already redirects `mmap` to the symbol mmap64,
// so plain C definitions of both would collide.
void* shim_mmap64(void* addr, size_t len, int prot, int flags, int fd, off64_t off) __asm__("mmap64");
void* shim_mmap32(void* addr, size_t len, int prot, int flags, int fd, long off) __asm__("mmap");

void* shim_mmap64(void* addr, size_t len, int prot, int flags, int fd, off64_t off) {
    if (!real_mmap64) real_mmap64 = (void* (*)(void*, size_t, int, int, int, off64_t))dlsym(RTLD_NEXT, "mmap64");
    void* r = real_mmap64(addr, len, prot, flags, fd, off);
    if (fd >= 0 && dumpfd >= 0) table_mmap(r, fd, (uint64_t)off);
    return r;
}

void* shim_mmap32(void* addr, size_t len, int prot, int flags, int fd, long off) {
    if (!real_mmap) real_mmap = (void* (*)(void*, size_t, int, int, int, long))dlsym(RTLD_NEXT, "mmap");
    void* r = real_mmap(addr, len, prot, flags, fd, off);
    if (fd >= 0 && dumpfd >= 0) table_mmap(r, fd, (uint64_t)(unsigned long)off);
    return r;
}

int munmap(void* addr, size_t len) {
    if (!real_munmap) real_munmap = (int (*)(void*, size_t))dlsym(RTLD_NEXT, "munmap");
    if (dumpfd >= 0) table_munmap(addr);
    return real_munmap(addr, len);
}

#ifdef B962_SHIM_SELFTEST
// ctest LimaBoCensusShim (tests/cmake/core_utils.cmake): the owner table's
// bookkeeping on the host, with a fake ioctl standing in for Lima. Linked as
// a program, so its ioctl/mmap/munmap interpose this process only.
static uint32_t fake_next_handle = 1;
static int fake_ioctl(int fd, unsigned long req, ...) {
    (void)fd;
    va_list ap;
    va_start(ap, req);
    void* arg = va_arg(ap, void*);
    va_end(ap);
    if (req == IOCTL_LIMA_GEM_CREATE) ((struct lima_gem_create*)arg)->handle = fake_next_handle++;
    else if (req == IOCTL_LIMA_GEM_INFO) ((struct lima_gem_info*)arg)->offset = 0x100000ull * ((struct lima_gem_info*)arg)->handle;
    return 0;
}
static uint32_t fake_create(int fd, uint32_t size, uint32_t flags) {
    struct lima_gem_create c = {size, flags, 0, 0};
    shim_ioctl(fd, IOCTL_LIMA_GEM_CREATE, &c);
    struct lima_gem_info g = {c.handle, 0, 0};
    shim_ioctl(fd, IOCTL_LIMA_GEM_INFO, &g);
    return c.handle;
}
static void fake_close(int fd, uint32_t h) {
    struct gem_close c = {h, 0};
    shim_ioctl_time64(fd, IOCTL_GEM_CLOSE, &c);
}
static const char* test_scope(void) { return "zone_mesh"; }
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "/tmp/b962_shim_selftest.tsv";
    dumpfd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC, 0644);
    CHECK(dumpfd >= 0);
    real_ioctl = fake_ioctl;
    real_ioctl_time64 = fake_ioctl;
    willeq_gem_set_scope_fn(test_scope);
    // 1. a tagged set, half closed again
    willeq_gem_tag_push("draw:zone");
    willeq_gem_tag_push("draw3d:client");
    uint32_t handles[3000];
    for (int i = 0; i < 3000; ++i) handles[i] = fake_create(7, 4096u * (uint32_t)(1 + i % 5), 0);
    willeq_gem_tag_pop();
    willeq_gem_tag_pop();
    for (int i = 0; i < 3000; i += 2) fake_close(7, handles[i]);
    // 2. churn: a million create/close pairs (LIMA_DEBUG=nobocache's shape)
    for (int i = 0; i < 1000000; ++i) fake_close(7, fake_create(7, 65536, 0));
    // 3. one heap BO, one mapped BO (mapped then unmapped then mapped again)
    willeq_gem_tag_push("egl:device");
    uint32_t heap = fake_create(9, 16u << 20, 1);
    uint32_t mapped = fake_create(9, 8192, 0);
    willeq_gem_tag_pop();
    table_mmap((void*)0x7000000, 9, 0x100000ull * mapped);
    table_munmap((void*)0x7000000);
    table_mmap((void*)0x7100000, 9, 0x100000ull * mapped);
    // every surviving handle is still findable
    for (int i = 1; i < 3000; i += 2) CHECK(bo_find(7, handles[i]) != NULL);
    for (int i = 0; i < 3000; i += 2) CHECK(bo_find(7, handles[i]) == NULL);
    CHECK(bo_find(9, heap) && bo_find(9, heap)->heap == 1);
    CHECK(bo_find(9, mapped) && bo_find(9, mapped)->mapped == 1);
    willeq_gem_dump("selftest");
    close(dumpfd);
    // re-read the dump: 1500 + 2 live BOs, sizes summed
    FILE* f = fopen(path, "r");
    CHECK(f != NULL);
    char line[512];
    unsigned live = 0, tagged = 0;
    unsigned long long bytes = 0, expect = 16u << 20;
    for (int i = 1; i < 3000; i += 2) expect += 4096u * (uint32_t)(1 + i % 5);
    expect += 8192;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != 'B') continue;
        ++live;
        unsigned fd, h, sz, heapf, mp, seq, tid;
        char scope[64], tag[256];
        CHECK(sscanf(line, "B\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%63[^\t]\t%255[^\n]", &fd, &h, &sz, &heapf, &mp, &seq, &tid, scope, tag) == 9);
        bytes += sz;
        if (!strcmp(tag, "draw:zone|draw3d:client")) { CHECK(!strcmp(scope, "zone_mesh")); ++tagged; }
    }
    fclose(f);
    CHECK(live == 1502);
    CHECK(tagged == 1500);
    CHECK(bytes == expect);
    printf("lima_bo_census selftest OK: live=%u bytes=%llu created=%llu closed=%llu\n",
           live, bytes, (unsigned long long)g_created, (unsigned long long)g_closed);
    return 0;
}
#endif
