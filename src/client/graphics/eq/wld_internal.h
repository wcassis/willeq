#pragma once

#include <cstring>

namespace EQT {
namespace Graphics {

// Safe unaligned read helper - uses memcpy to avoid bus errors on ARM
template<typename T>
inline T read_val(const void* ptr) {
    T val;
    std::memcpy(&val, ptr, sizeof(T));
    return val;
}


} // namespace Graphics
} // namespace EQT
