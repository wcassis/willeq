#pragma once

// B962 (GitLab #280): GEM buffer OWNER tags for the diagnostic LD_PRELOAD
// census, scripts/pi/lima_bo_census.c.
//
// On the Orange Pi every texture, vertex/index buffer, depth buffer, render
// target and Mesa-internal buffer is a Lima GEM object, and the gpu_buffers
// row (gpu_buffer_census.h) sees only their SUM. The census shim records each
// GEM_CREATE with the tag the creating thread has pushed here and the memory
// ledger scope open on it, and at every ledger point (recordPoint) the client
// asks it to dump the live set -- so the cgroup's figure becomes a per-owner
// table (`scripts/pi/lima_bo_census.py --owners`, refactor/B962/README.md).
//
// Off by default and free when off: the shim's entry points are resolved once
// with dlsym(RTLD_DEFAULT, ...). With no shim preloaded every GemTag is one
// predictable branch and gemCensusDump() returns at once. The GLES2 driver
// (docker/irrlicht-drm) tags its own entry points the same way
// (COpenGLES2Driver.h, GemTagScope), so the two layers nest.
//
// Header-only on purpose (inline functions, one function-local static): any
// TU may tag without a link dependency, the tools and narrow tests included.

#include "client/core/memory_ledger.h"

#include <cstdint>
#include <cstdio>

#if defined(__linux__)
#include <dlfcn.h>
#endif

namespace eqt {
namespace memory {

namespace gem_detail {

struct Shim {
    void (*push)(const char*) = nullptr;
    void (*pop)() = nullptr;
    void (*dump)(const char*) = nullptr;
};

inline const char* scopeName() {
    return categoryName(currentCategory());
}

inline const Shim& shim() {
    static const Shim s = [] {
        Shim r;
#if defined(__linux__)
        r.push = reinterpret_cast<void (*)(const char*)>(dlsym(RTLD_DEFAULT, "willeq_gem_tag_push"));
        r.pop = reinterpret_cast<void (*)()>(dlsym(RTLD_DEFAULT, "willeq_gem_tag_pop"));
        r.dump = reinterpret_cast<void (*)(const char*)>(dlsym(RTLD_DEFAULT, "willeq_gem_dump"));
        auto setScope = reinterpret_cast<void (*)(const char* (*)())>(
            dlsym(RTLD_DEFAULT, "willeq_gem_set_scope_fn"));
        if (setScope) setScope(&scopeName);
        if (!r.push || !r.pop) r.push = nullptr;
#endif
        return r;
    }();
    return s;
}

} // namespace gem_detail

// True when the census shim is preloaded (its symbols resolve).
inline bool gemCensusActive() {
    return gem_detail::shim().push != nullptr;
}

// Ask the shim to append the live-BO table under `point` (no-op without it).
// Called by recordPoint() beside the [memgpu] line.
inline void gemCensusDump(const char* point) {
    if (const auto dump = gem_detail::shim().dump) dump(point);
}

// RAII owner tag: every GEM buffer this thread creates while it is open is
// recorded under it. Nests (the shim records the outermost and innermost).
// The formatting constructor builds "<kind> <name> <w>x<h> L<levels>" only
// when the shim is present.
class GemTag {
public:
    explicit GemTag(const char* tag) {
        const auto& s = gem_detail::shim();
        if (!s.push) return;
        s.push(tag ? tag : "");
        pushed_ = true;
    }
    GemTag(const char* kind, const char* name, uint32_t w, uint32_t h, uint32_t levels) {
        const auto& s = gem_detail::shim();
        if (!s.push) return;
        char buf[160];
        snprintf(buf, sizeof(buf), "%s %s %ux%u L%u", kind ? kind : "?", name ? name : "?",
                 static_cast<unsigned>(w), static_cast<unsigned>(h), static_cast<unsigned>(levels));
        s.push(buf);
        pushed_ = true;
    }
    ~GemTag() {
        if (pushed_) gem_detail::shim().pop();
    }
    GemTag(const GemTag&) = delete;
    GemTag& operator=(const GemTag&) = delete;
private:
    bool pushed_ = false;
};

} // namespace memory
} // namespace eqt
