// include/client/core/malloc_compat.h -- <malloc.h> plus a mallinfo2() shim
// for glibc < 2.33 (B1056, #376: lane 4 / Debian bullseye runs 2.31). Include
// THIS next to every mallinfo2() use instead of <malloc.h>, so no call site
// depends on a transitive include for the shim.
#pragma once

#include <malloc.h>  // struct mallinfo2 (mallinfo2(), glibc >= 2.33)
#if defined(__GLIBC__)
#if !__GLIBC_PREREQ(2, 33)
// B1056 (#376): lane 4 (Debian bullseye) runs glibc 2.31, which has only the
// int-field mallinfo(). Same counters, one type apart; glibc 2.33 added the
// size_t form because int saturates past 2 GiB, which no run under the 256 MB
// bound reaches. Defined at global scope so `struct mallinfo2` inside
// eqt::memory names THIS one, exactly as it names glibc's on a newer libc.
struct mallinfo2 {
    size_t arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost;
};
static inline struct mallinfo2 mallinfo2() {
    const struct mallinfo m = mallinfo();
    struct mallinfo2 r{};
    r.arena = static_cast<size_t>(m.arena);       r.ordblks = static_cast<size_t>(m.ordblks);
    r.smblks = static_cast<size_t>(m.smblks);     r.hblks = static_cast<size_t>(m.hblks);
    r.hblkhd = static_cast<size_t>(m.hblkhd);     r.usmblks = static_cast<size_t>(m.usmblks);
    r.fsmblks = static_cast<size_t>(m.fsmblks);   r.uordblks = static_cast<size_t>(m.uordblks);
    r.fordblks = static_cast<size_t>(m.fordblks); r.keepcost = static_cast<size_t>(m.keepcost);
    return r;
}
#endif
#endif
