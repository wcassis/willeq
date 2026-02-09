# CMake toolchain file for cross-compiling to ARM Linux (armhf)
# Targets: Orange Pi One (Allwinner H3, Cortex-A7), Raspberry Pi 2/3, etc.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-linux-gnueabihf.cmake \
#         -DARM_SYSROOT=/path/to/jessie-sysroot ..

# Target system
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Sysroot (must be set by caller or environment)
if(NOT DEFINED ARM_SYSROOT)
    if(DEFINED ENV{ARM_SYSROOT})
        set(ARM_SYSROOT "$ENV{ARM_SYSROOT}" CACHE PATH "ARM sysroot directory")
    else()
        set(ARM_SYSROOT "/opt/arm-sysroot" CACHE PATH "ARM sysroot directory")
    endif()
endif()

# Cross-compiler
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
set(CMAKE_AR arm-linux-gnueabihf-ar)
set(CMAKE_RANLIB arm-linux-gnueabihf-ranlib)
set(CMAKE_STRIP arm-linux-gnueabihf-strip)

# Sysroot for linking against target libraries
set(CMAKE_SYSROOT "${ARM_SYSROOT}")

# Compiler flags for Cortex-A7 (Allwinner H3)
set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard")

# Search paths - only search sysroot for libraries and headers
set(CMAKE_FIND_ROOT_PATH "${ARM_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config for cross-compilation
set(ENV{PKG_CONFIG_PATH} "${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf/pkgconfig:${ARM_SYSROOT}/usr/lib/pkgconfig:${ARM_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "${ARM_SYSROOT}/usr/lib/arm-linux-gnueabihf/pkgconfig:${ARM_SYSROOT}/usr/lib/pkgconfig:${ARM_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${ARM_SYSROOT}")
