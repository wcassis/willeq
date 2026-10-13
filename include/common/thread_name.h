#pragma once

// B1039 (#359): name the calling thread (visible in `top -H`, gdb, and the
// [srcchar] parse lines -- T504). pthread_setname_np takes the thread on
// Linux (glibc: 2 arguments, 15 chars + NUL) and names only the CALLING
// thread on macOS (1 argument). Every caller names itself, so one helper
// covers both.

#include <pthread.h>

namespace eqt {

inline void setCurrentThreadName(const char* name) {
#if defined(__APPLE__)
    pthread_setname_np(name);
#else
    pthread_setname_np(pthread_self(), name);
#endif
}

}  // namespace eqt
