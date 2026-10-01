/*
 * B959 (GitLab #277): the one call every host of the GLES2 driver makes
 * BEFORE creating its Irrlicht device.
 *
 * The driver (docker/irrlicht-drm/COpenGLES2Driver.cpp, an Irrlicht patch
 * linked into libIrrlicht) sits BELOW the client and must not include client
 * headers, so it cannot call eqt::ResolveCachePathForWrite() itself. The
 * existing pattern for host-to-driver configuration is a plain free function
 * with plain types (eqtEnableGPUSyncProbe in gpu_sync_probe.h); the shader
 * cache directory follows it: the host resolves the directory the same way as
 * every other generated cache (T402/B248) and hands the driver a string.
 * Not an env var: an env var the driver read would need its own copy of the
 * candidate walk. Unset means the driver's cache is OFF, never CWD-relative.
 *
 * A host that forgets the call loses the cache (slower start), it does not
 * write into a checkout; tests/test_data_path_routing.cpp makes forgetting it
 * a red test.
 */
#pragma once

#include "client/data_paths.h"

// Defined in COpenGLES2Driver.cpp (libIrrlicht). nullptr or "" turns the cache off.
void eqtSetShaderBinaryCacheDir(const char* dir);

namespace eqt {

inline void ConfigureGles2ShaderCache() {
    eqtSetShaderBinaryCacheDir(ResolveShaderCacheDirectory().c_str());
}

}  // namespace eqt
