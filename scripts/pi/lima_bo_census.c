// B955 (GitLab #273): a DIAGNOSTIC LD_PRELOAD shim that logs every Lima GEM
// buffer object a process creates and closes, so the gap between the
// gpu_buffers ledger row (the kernel's count of our GEM pages) and the
// renderer's logical tally (textures + VBOs) can be named per BO.
//
// Not part of any shipped binary. Build it with the ARM image's compiler --
// and override the image's ENTRYPOINT, which otherwise runs the whole client
// build IN-SOURCE in the mounted tree:
//   docker run --rm --entrypoint /bin/bash -v "$PWD:/src" willeq-arm-noble -c
//     "arm-linux-gnueabihf-gcc -O2 -Wall -shared -fPIC
//      -o /src/build/lima_bo_census.so /src/scripts/pi/lima_bo_census.c -ldl"
// Run (a setcap binary ignores LD_PRELOAD -- use the benchmark or kmscube):
//   B955_BOLOG=/tmp/bo.log LD_PRELOAD=./lima_bo_census.so <program>
// Log lines: "<mono_s> C <handle> <size> <flags>" (LIMA_GEM_CREATE; flags
// bit 0 = LIMA_BO_FLAG_HEAP: the size is a MAXIMUM, pages grow on GPU
// demand), "<mono_s> X <handle>" (GEM_CLOSE), "<mono_s> I <handle>"
// (a PRIME import: a buffer another device allocated -- the display
// controller's scanout, CMA, not shmem). scripts/pi/lima_bo_census.py folds
// a log into the live set at any instant.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

// linux/ioctl.h encoding, 32-bit ARM ('d' = DRM_IOCTL_BASE)
#define B955_IOWR(nr, sz) ((3u << 30) | ((unsigned)(sz) << 16) | ('d' << 8) | (nr))
#define B955_IOW(nr, sz)  ((1u << 30) | ((unsigned)(sz) << 16) | ('d' << 8) | (nr))
struct lima_gem_create { uint32_t size, flags, handle, pad; };
struct gem_close { uint32_t handle, pad; };
struct prime_handle { uint32_t handle, flags; int32_t fd; };
#define IOCTL_LIMA_GEM_CREATE B955_IOWR(0x41, sizeof(struct lima_gem_create))
#define IOCTL_GEM_CLOSE       B955_IOW(0x09, sizeof(struct gem_close))
#define IOCTL_PRIME_FD_TO_HANDLE B955_IOWR(0x2e, sizeof(struct prime_handle))

static int (*real_ioctl)(int, unsigned long, ...);
static int logfd = -1;

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

__attribute__((constructor)) static void init(void) {
    real_ioctl = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
    const char* p = getenv("B955_BOLOG");
    if (p) logfd = open(p, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC, 0644);
}

int ioctl(int fd, unsigned long req, ...) {
    va_list ap;
    va_start(ap, req);
    void* arg = va_arg(ap, void*);
    va_end(ap);
    int r = real_ioctl(fd, req, arg);
    if (r == 0 && arg) {
        if (req == IOCTL_LIMA_GEM_CREATE) {
            const struct lima_gem_create* c = arg;
            logline("C %u %u %u fd%d\n", c->handle, c->size, c->flags, fd);
        } else if (req == IOCTL_GEM_CLOSE) {
            logline("X %u fd%d\n", ((const struct gem_close*)arg)->handle, fd);
        } else if (req == IOCTL_PRIME_FD_TO_HANDLE) {
            logline("I %u fd%d\n", ((const struct prime_handle*)arg)->handle, fd);
        }
    }
    return r;
}
