# CMake toolchain file for cross-compiling to macOS x86_64
# Used with osxcross

# Target system
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# macOS deployment target (10.15 Catalina)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Target architecture")

# osxcross toolchain location (set by environment or override)
if(NOT DEFINED ENV{OSXCROSS_ROOT})
    set(OSXCROSS_ROOT "/opt/osxcross" CACHE PATH "osxcross installation directory")
else()
    set(OSXCROSS_ROOT "$ENV{OSXCROSS_ROOT}" CACHE PATH "osxcross installation directory")
endif()

set(OSXCROSS_TARGET_DIR "${OSXCROSS_ROOT}/target")
set(OSXCROSS_SDK "${OSXCROSS_TARGET_DIR}/SDK/MacOSX10.15.sdk")

# Cross-compiler
set(CMAKE_C_COMPILER "${OSXCROSS_TARGET_DIR}/bin/x86_64-apple-darwin19-clang")
set(CMAKE_CXX_COMPILER "${OSXCROSS_TARGET_DIR}/bin/x86_64-apple-darwin19-clang++")
set(CMAKE_AR "${OSXCROSS_TARGET_DIR}/bin/x86_64-apple-darwin19-ar")
set(CMAKE_RANLIB "${OSXCROSS_TARGET_DIR}/bin/x86_64-apple-darwin19-ranlib")

# Sysroot
set(CMAKE_SYSROOT "${OSXCROSS_SDK}")
set(CMAKE_OSX_SYSROOT "${OSXCROSS_SDK}")

# Search paths
set(CMAKE_FIND_ROOT_PATH "${OSXCROSS_SDK}" "${OSXCROSS_TARGET_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Additional search paths for cross-compiled dependencies
list(APPEND CMAKE_PREFIX_PATH "${OSXCROSS_TARGET_DIR}/macports/pkgs/opt/local")

# Compiler flags
set(CMAKE_C_FLAGS_INIT "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
set(CMAKE_CXX_FLAGS_INIT "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")

# macOS frameworks location
set(CMAKE_FRAMEWORK_PATH "${OSXCROSS_SDK}/System/Library/Frameworks")

# pkg-config for cross-compilation
set(PKG_CONFIG_EXECUTABLE "${OSXCROSS_TARGET_DIR}/bin/x86_64-apple-darwin19-pkg-config")
set(ENV{PKG_CONFIG_PATH} "${OSXCROSS_TARGET_DIR}/macports/pkgs/opt/local/lib/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "${OSXCROSS_TARGET_DIR}/macports/pkgs/opt/local/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${OSXCROSS_SDK}")
