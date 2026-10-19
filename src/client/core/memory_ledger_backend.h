// Allocator primitives used by memory_ledger.cpp's interposition hooks.
// Included inside its anonymous namespace; never include from another TU.
inline uint64_t backendChunkBytes(void* mem) {
#if defined(__APPLE__)
    // malloc_size includes the allocation class's rounding and our header,
    // but not zone metadata or freed-but-resident pages.
    return malloc_size(mem);
#else
    const size_t sz = reinterpret_cast<size_t*>(mem)[-1];
    return sz & ~static_cast<size_t>(7);
#endif
}

inline void* backendMalloc(size_t size) {
#if defined(__APPLE__)
    return malloc_zone_malloc(malloc_default_zone(), size);
#else
    return __libc_malloc(size);
#endif
}

inline void* backendMemalign(size_t align, size_t size) {
#if defined(__APPLE__)
    return malloc_zone_memalign(malloc_default_zone(), align, size);
#else
    return __libc_memalign(align, size);
#endif
}

inline void* backendRealloc(void* p, size_t size) {
#if defined(__APPLE__)
    return malloc_zone_realloc(malloc_default_zone(), p, size);
#else
    return __libc_realloc(p, size);
#endif
}

inline void backendFree(void* p) {
#if defined(__APPLE__)
    malloc_zone_free(malloc_default_zone(), p);
#else
    __libc_free(p);
#endif
}
