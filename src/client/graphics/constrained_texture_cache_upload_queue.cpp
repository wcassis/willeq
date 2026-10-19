// constrained_texture_cache_upload_queue.cpp — the placeholder-texture accessor
// and the live pending-work predicates.
// Split from the 2,211-line original by refactor/J10_constrained_texture_cache_split.md.
// T326 deleted the quarantined T04 upload queue that made up most of this file
// (see the pointer below); what remains here is all live.
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/background_thread_pool.h"
#include "common/logging.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <fstream>
#include <cstdlib>

// P03: the EQE1 upload path (ETC1 + v2 payloads) is available on any GLES2
// driver build, not just DRM/ARM — EQT_HAS_GLES2 is defined for the x86
// llvmpipe dev build (EQT_DRM=OFF, EQT_GLES2=ON), where gles2WrapTexture and
// the GL entry points below are equally present. The ARM/Pi build defines
// BOTH macros, so its preprocessed output is unchanged by this widening.
#if defined(EQT_HAS_DRM) || defined(EQT_HAS_GLES2)
#define EQT_EQE1_UPLOAD_AVAILABLE 1
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
// glGenerateMipmap and glCompressedTexImage2D may not be directly available
// in all GL headers; get function pointers via EGL at runtime
#include <EGL/egl.h>
typedef void (*PFNGLGENERATEMIPMAPPROC_)(GLenum target);
typedef void (*PFNGLCOMPRESSEDTEXIMAGE2DPROC_)(GLenum target, GLint level,
    GLenum internalformat, GLsizei width, GLsizei height, GLint border,
    GLsizei imageSize, const void* data);

// Defined in COpenGLES2Driver.cpp — wraps a raw GL texture handle in an ITexture*
extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
                               unsigned int width, unsigned int height,
                               unsigned int gpuBytes);
#endif

namespace EQT {
namespace Graphics {

// ---------------------------------------------------------------------------
// Placeholder texture (public locks, internal does not)
// ---------------------------------------------------------------------------

irr::video::ITexture* ConstrainedTextureCache::getPlaceholderTexture() {
    std::lock_guard<std::mutex> lock(mutex_);
    return getPlaceholderTextureInternal();
}

// ---------------------------------------------------------------------------
// T04: Upload queue removed. Use getOrLoad() + uploadTexture() instead.
// ---------------------------------------------------------------------------

// T326: the quarantined T04 upload queue that stood here was deleted. It could
// no longer compile -- T04 had already removed every member it touched from the
// header (which still carries the prose saying so), and the predicate pair after
// it read a member that no longer exists. Recover with:
//   git show bcbf2ddc:src/client/graphics/constrained_texture_cache_upload_queue.cpp

// T475 (board B670): both predicates also read pendingAsyncUploads_, a set
// nothing had written since T04; the member is deleted.
bool ConstrainedTextureCache::isPending(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return decodedEntries_.count(name) > 0;
}

bool ConstrainedTextureCache::hasPendingWork() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !decodedEntries_.empty();
}

} // namespace Graphics
} // namespace EQT
