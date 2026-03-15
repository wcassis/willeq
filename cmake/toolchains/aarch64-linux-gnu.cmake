# CMake toolchain file for cross-compiling to AArch64 Linux (arm64)
# Targets: Rock64 (RK3328, Cortex-A53), Pine64, etc.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
#         -DARM_SYSROOT=/path/to/noble-sysroot ..

# Target system
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Sysroot (must be set by caller or environment)
if(NOT DEFINED ARM_SYSROOT)
    if(DEFINED ENV{ARM_SYSROOT})
        set(ARM_SYSROOT "$ENV{ARM_SYSROOT}" CACHE PATH "ARM64 sysroot directory")
    else()
        set(ARM_SYSROOT "/opt/arm64-sysroot" CACHE PATH "ARM64 sysroot directory")
    endif()
endif()

# Cross-compiler
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR aarch64-linux-gnu-ar)
set(CMAKE_RANLIB aarch64-linux-gnu-ranlib)
set(CMAKE_STRIP aarch64-linux-gnu-strip)

# Sysroot for linking against target libraries
set(CMAKE_SYSROOT "${ARM_SYSROOT}")

# Compiler flags for Cortex-A53 (RK3328)
set(CMAKE_C_FLAGS_INIT "-march=armv8-a -mtune=cortex-a53")
set(CMAKE_CXX_FLAGS_INIT "-march=armv8-a -mtune=cortex-a53")

# Search paths - only search sysroot for libraries and headers
set(CMAKE_FIND_ROOT_PATH "${ARM_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config for cross-compilation
set(ENV{PKG_CONFIG_PATH} "${ARM_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${ARM_SYSROOT}/usr/lib/pkgconfig:${ARM_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "${ARM_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${ARM_SYSROOT}/usr/lib/pkgconfig:${ARM_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${ARM_SYSROOT}")
