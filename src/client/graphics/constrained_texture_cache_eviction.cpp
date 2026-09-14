// constrained_texture_cache_eviction.cpp — eviction and CPU-side image decode.
// Eviction listeners, evictUntilAvailable()/evictTexture(), texture sizing, and
// the decode/resample family (processTextureData, downsampleToMaxSize,
// convertTo16Bit, detectAlpha, decodeBMP) plus clearTextureReferences().
// Split from the 2,211-line original by refactor/J10_constrained_texture_cache_split.md.
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
#include <GL/gl.h>
#include <GL/glext.h>
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
// Eviction listeners (lock mutex_)
// ---------------------------------------------------------------------------

void ConstrainedTextureCache::addEvictionListener(TextureEvictionListener* listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (listener) {
        evictionListeners_.push_back(listener);
    }
}

void ConstrainedTextureCache::removeEvictionListener(TextureEvictionListener* listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    evictionListeners_.erase(
        std::remove(evictionListeners_.begin(), evictionListeners_.end(), listener),
        evictionListeners_.end());
}

// ---------------------------------------------------------------------------
// Private methods — NO locking (always called under mutex_ or immutable data)
// ---------------------------------------------------------------------------

bool ConstrainedTextureCache::evictUntilAvailable(size_t bytesNeeded) {
    // Check if texture is too large to ever fit
    if (bytesNeeded > config_.textureMemoryBytes) {
        return false;
    }

    // Evict LRU textures until we have enough space
    // Safe eviction: clearTextureReferences() removes texture from mesh materials before deletion
    while (currentUsage_ + bytesNeeded > config_.textureMemoryBytes && !lruOrder_.empty()) {
        // T202: skip protected entries. The UI atlas lives in this cache (it is
        // loaded from willeq.eqe1 like any other EQE1 texture) but is NOT a zone
        // asset -- it is drawn every frame for the lifetime of the process, and
        // UIAtlas holds a RAW pointer to it. Evicting it destroyed the
        // COGLES2Texture out from under UIRenderer::endFrame, and the next
        // draw2DImage ran on freed memory: SIGSEGV in 7 of 8 configurations
        // (T201), on every zone and both asset trees, but ONLY with --eqe1-path
        // -- because that is the only path that puts ui_atlas in this cache.
        std::string oldest;
        size_t scanned = 0;
        for (const auto& candidate : lruOrder_) {
            ++scanned;
            if (protectedTextures_.find(candidate) == protectedTextures_.end()) {
                oldest = candidate;
                break;
            }
        }
        if (oldest.empty()) {
            // Everything resident is protected — nothing left to give back.
            LOG_WARN(MOD_GRAPHICS, "Constrained cache: cannot free {} bytes, all {} "
                     "resident textures are protected", bytesNeeded, scanned);
            break;
        }
        evictTexture(oldest);
    }

    return (currentUsage_ + bytesNeeded <= config_.textureMemoryBytes);
}

void ConstrainedTextureCache::evictTexture(const std::string& name) {
    auto it = cache_.find(name);
    if (it == cache_.end()) return;

    irr::video::ITexture* texture = it->second.texture;
    size_t sizeBytes = it->second.sizeBytes;

    LOG_DEBUG(MOD_GRAPHICS, "Evicting texture '{}' ({} bytes)", name, sizeBytes);

    // P13: record the eviction (name/bytes/category) and thrash-detect any
    // later re-upload of this same name. No-op when disabled.
    if (ledgerEnabled_) {
        ledger_.recordEviction(name, sizeBytes, it->second.category);
    }

    // Notify listeners before clearing references and removing texture
    for (auto* listener : evictionListeners_) {
        listener->onTextureEvicted(name, texture);
    }

    // CRITICAL: Clear all references to this texture from mesh materials BEFORE removing
    // This prevents crashes from dangling texture pointers during rendering
    if (texture) {
        clearTextureReferences(texture);
        driver_->removeTexture(texture);
    }

    // Update memory tracking
    currentUsage_ -= sizeBytes;

    // Remove from LRU list
    lruOrder_.erase(it->second.lruIterator);

    // Remove from cache
    cache_.erase(it);

    ++evictionCount_;
}

size_t ConstrainedTextureCache::calculateTextureSize(int width, int height) const {
    // Always use 4 bytes per pixel (32-bit ARGB) for compatibility with software renderer
    size_t base = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    if (config_.enableMipmaps) {
        // Mipmap chain adds ~33% overhead (sum of 1/4 + 1/16 + ... converges to 1/3)
        // NOTE (P42, P38 finding #4, refactor/perf_truth_audit.md): this
        // budget inflation is charged even though no code path actually
        // uploads mip levels (getOrLoad()/uploadPending()/processOne()'s
        // filter-setting sites were always stomped before use -- see their
        // P42 comments -- and this class's own upload paths never call
        // glGenerateMipmap). For the orangepi preset, where enableMipmaps
        // is true, this is real phantom memory pressure that could trigger
        // unnecessary LRU eviction. P38 named this a candidate follow-up,
        // not measured/closed there; left unfixed here too -- changing the
        // budget accounting changes eviction timing under memory pressure,
        // a real behavior change out of scope for a no-default-behavior-
        // change hygiene pass. Flagging honestly rather than fixing silently.
        return base + base / 3;
    }
    return base;
}

bool ConstrainedTextureCache::processTextureData(const std::vector<char>& rawData,
                                                  std::vector<uint8_t>& processedData,
                                                  int& width, int& height,
                                                  bool& hasAlpha,
                                                  bool applyPaletteMask) {
    // Decode based on format
    if (DDSDecoder::isDDS(rawData)) {
        DecodedImage image = DDSDecoder::decode(rawData);
        if (!image.isValid()) {
            return false;
        }
        width = static_cast<int>(image.width);
        height = static_cast<int>(image.height);
        processedData = std::move(image.pixels);
    } else {
        // Assume BMP
        if (!decodeBMP(rawData, processedData, width, height, applyPaletteMask)) {
            return false;
        }
    }

    // Downsample if needed
    if (width > config_.maxTextureDimension || height > config_.maxTextureDimension) {
        downsampleToMaxSize(processedData, width, height, 4);  // 4 channels (RGBA)
    }

    // Detect alpha channel
    hasAlpha = detectAlpha(processedData, width, height);

    return true;
}

void ConstrainedTextureCache::downsampleToMaxSize(std::vector<uint8_t>& data,
                                                   int& width, int& height,
                                                   int channels) {
    // Repeatedly halve dimensions using box filter until within limits
    while (width > config_.maxTextureDimension || height > config_.maxTextureDimension) {
        int newWidth = width / 2;
        int newHeight = height / 2;

        // Ensure minimum size of 1
        if (newWidth < 1) newWidth = 1;
        if (newHeight < 1) newHeight = 1;

        std::vector<uint8_t> newData(newWidth * newHeight * channels);

        // Box filter: average 2x2 blocks
        for (int y = 0; y < newHeight; ++y) {
            for (int x = 0; x < newWidth; ++x) {
                for (int c = 0; c < channels; ++c) {
                    int sum = 0;
                    int count = 0;

                    // Sample up to 2x2 pixels
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            int srcX = x * 2 + dx;
                            int srcY = y * 2 + dy;
                            if (srcX < width && srcY < height) {
                                sum += data[(srcY * width + srcX) * channels + c];
                                ++count;
                            }
                        }
                    }

                    newData[(y * newWidth + x) * channels + c] =
                        static_cast<uint8_t>(sum / count);
                }
            }
        }

        data = std::move(newData);
        width = newWidth;
        height = newHeight;
    }
}

std::vector<uint16_t> ConstrainedTextureCache::convertTo16Bit(const std::vector<uint8_t>& rgba,
                                                               int width, int height,
                                                               bool hasAlpha) {
    std::vector<uint16_t> result(width * height);

    for (int i = 0; i < width * height; ++i) {
        uint8_t r = rgba[i * 4 + 0];
        uint8_t g = rgba[i * 4 + 1];
        uint8_t b = rgba[i * 4 + 2];
        uint8_t a = rgba[i * 4 + 3];

        if (hasAlpha) {
            // RGBA 1555: 1 bit alpha, 5 bits each RGB
            // Irrlicht ECF_A1R5G5B5 format: A(1) R(5) G(5) B(5)
            uint16_t a1 = (a >= 128) ? 1 : 0;
            uint16_t r5 = (r >> 3) & 0x1F;
            uint16_t g5 = (g >> 3) & 0x1F;
            uint16_t b5 = (b >> 3) & 0x1F;
            result[i] = (a1 << 15) | (r5 << 10) | (g5 << 5) | b5;
        } else {
            // RGB 565: 5 bits red, 6 bits green, 5 bits blue
            // Irrlicht ECF_R5G6B5 format: R(5) G(6) B(5)
            uint16_t r5 = (r >> 3) & 0x1F;
            uint16_t g6 = (g >> 2) & 0x3F;
            uint16_t b5 = (b >> 3) & 0x1F;
            result[i] = (r5 << 11) | (g6 << 5) | b5;
        }
    }

    return result;
}

bool ConstrainedTextureCache::detectAlpha(const std::vector<uint8_t>& rgba, int width, int height) {
    // Check if any pixel has alpha < 255
    for (int i = 0; i < width * height; ++i) {
        if (rgba[i * 4 + 3] < 255) {
            return true;
        }
    }
    return false;
}

bool ConstrainedTextureCache::decodeBMP(const std::vector<char>& data,
                                         std::vector<uint8_t>& rgba,
                                         int& width, int& height,
                                         bool applyPaletteMask) {
    if (data.size() < 54) return false;  // Minimum BMP header size

    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.data());

    // Check BMP magic
    if (ptr[0] != 'B' || ptr[1] != 'M') return false;

    // Read header
    uint32_t dataOffset = *reinterpret_cast<const uint32_t*>(ptr + 10);
    uint32_t dibHeaderSize = *reinterpret_cast<const uint32_t*>(ptr + 14);
    int32_t bmpWidth = *reinterpret_cast<const int32_t*>(ptr + 18);
    int32_t bmpHeight = *reinterpret_cast<const int32_t*>(ptr + 22);
    uint16_t bitsPerPixel = *reinterpret_cast<const uint16_t*>(ptr + 28);

    if (bmpWidth <= 0 || bmpHeight == 0) return false;

    // Handle negative height (top-down DIB)
    bool topDown = (bmpHeight < 0);
    if (topDown) bmpHeight = -bmpHeight;

    width = bmpWidth;
    height = bmpHeight;

    // 8bpp paletted BMP: read palette from after DIB header
    const uint8_t* palette = nullptr;
    uint32_t paletteEntries = 0;
    if (bitsPerPixel == 8) {
        // "Colors used" field at offset 46 (if DIB header is large enough)
        if (dibHeaderSize >= 36 && data.size() >= 50) {
            paletteEntries = *reinterpret_cast<const uint32_t*>(ptr + 46);
        }
        if (paletteEntries == 0) paletteEntries = 256;  // 0 means 2^bpp
        uint32_t paletteOffset = 14 + dibHeaderSize;
        uint32_t paletteBytes = paletteEntries * 4;
        if (paletteOffset + paletteBytes > data.size()) return false;
        palette = ptr + paletteOffset;
    }

    // Calculate row size (must be aligned to 4 bytes)
    int bytesPerPixel = (bitsPerPixel == 8) ? 1 : (bitsPerPixel / 8);
    int rowSize = ((bmpWidth * bytesPerPixel + 3) / 4) * 4;

    if (dataOffset + static_cast<size_t>(rowSize) * height > data.size()) return false;

    // Allocate output
    rgba.resize(width * height * 4);

    // Decode pixels
    const uint8_t* pixelData = ptr + dataOffset;

    for (int y = 0; y < height; ++y) {
        int srcY = topDown ? y : (height - 1 - y);
        const uint8_t* row = pixelData + srcY * rowSize;

        for (int x = 0; x < width; ++x) {
            int dstIdx = (y * width + x) * 4;

            if (bitsPerPixel == 24) {
                rgba[dstIdx + 0] = row[x * 3 + 2];  // R
                rgba[dstIdx + 1] = row[x * 3 + 1];  // G
                rgba[dstIdx + 2] = row[x * 3 + 0];  // B
                rgba[dstIdx + 3] = 255;              // A
            } else if (bitsPerPixel == 32) {
                rgba[dstIdx + 0] = row[x * 4 + 2];  // R
                rgba[dstIdx + 1] = row[x * 4 + 1];  // G
                rgba[dstIdx + 2] = row[x * 4 + 0];  // B
                rgba[dstIdx + 3] = row[x * 4 + 3];  // A
            } else if (bitsPerPixel == 8) {
                uint8_t idx = row[x];
                if (idx >= paletteEntries) idx = 0;
                rgba[dstIdx + 0] = palette[idx * 4 + 2];  // R (palette is BGRA)
                rgba[dstIdx + 1] = palette[idx * 4 + 1];  // G
                rgba[dstIdx + 2] = palette[idx * 4 + 0];  // B
                // P46: EQ convention is that palette index 0 is the
                // transparent color (mirrors tools/eqe1_builder.cpp's
                // identical decode, previously the only place this was
                // implemented). Gated on applyPaletteMask — false everywhere
                // except callers who have already confirmed via the
                // WLD-derived textureMasked() flag that THIS texture is
                // WLD-masked (entity/equipment per-part loads only; see
                // getOrLoad()'s doc comment). Ungated callers (zone
                // geometry, spell FX, UI, etc.) keep the pre-P46 alpha=255
                // behavior byte-for-byte, since palette index 0 is
                // frequently a legitimate opaque color there.
                rgba[dstIdx + 3] = (applyPaletteMask && idx == 0) ? 0 : 255;  // A
            } else {
                // Unsupported format
                return false;
            }
        }
    }

    return true;
}


void ConstrainedTextureCache::clearTextureReferences(irr::video::ITexture* texture) {
    if (!smgr_ || !texture) {
        return;
    }

    // Get placeholder to replace evicted textures (use internal — already under mutex_)
    irr::video::ITexture* placeholder = getPlaceholderTextureInternal();

    // Recursively scan all scene nodes and their mesh materials
    std::function<void(irr::scene::ISceneNode*)> scanNode = [&](irr::scene::ISceneNode* node) {
        if (!node) return;

        // Check if this node has a mesh (CMeshSceneNode or CAnimatedMeshSceneNode)
        irr::scene::IMeshSceneNode* meshNode = nullptr;
        irr::scene::IAnimatedMeshSceneNode* animNode = nullptr;

        if (node->getType() == irr::scene::ESNT_MESH) {
            meshNode = static_cast<irr::scene::IMeshSceneNode*>(node);
        } else if (node->getType() == irr::scene::ESNT_ANIMATED_MESH) {
            animNode = static_cast<irr::scene::IAnimatedMeshSceneNode*>(node);
        }

        // Check mesh materials
        irr::scene::IMesh* mesh = nullptr;
        if (meshNode && meshNode->getMesh()) {
            mesh = meshNode->getMesh();
        } else if (animNode && animNode->getMesh()) {
            mesh = animNode->getMesh()->getMesh(0);  // Get first frame mesh
        }

        if (mesh) {
            for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(i);
                if (buffer) {
                    irr::video::SMaterial& mat = buffer->getMaterial();
                    for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
                        if (mat.getTexture(t) == texture) {
                            mat.setTexture(t, placeholder);
                        }
                    }
                }
            }
        }

        // Also check node's own materials (override materials)
        for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
            irr::video::SMaterial& mat = node->getMaterial(i);
            for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
                if (mat.getTexture(t) == texture) {
                    mat.setTexture(t, placeholder);
                }
            }
        }

        // Recurse to children
        const auto& children = node->getChildren();
        for (auto* child : children) {
            scanNode(child);
        }
    };

    // Start scan from root
    scanNode(smgr_->getRootSceneNode());
}

} // namespace Graphics
} // namespace EQT
