#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/dds_decoder.h"
#include "common/logging.h"
#include <algorithm>
#include <cstring>
#include <functional>

namespace EQT {
namespace Graphics {

ConstrainedTextureCache::ConstrainedTextureCache(const ConstrainedRendererConfig& config,
                                                   irr::video::IVideoDriver* driver)
    : config_(config)
    , driver_(driver)
{
}

ConstrainedTextureCache::~ConstrainedTextureCache() {
    clear();
}

irr::video::ITexture* ConstrainedTextureCache::getOrLoad(const std::string& name,
                                                          const std::vector<char>& data) {
    // Check if already cached
    auto it = cache_.find(name);
    if (it != cache_.end()) {
        // Move to back of LRU list (most recently used)
        lruOrder_.erase(it->second.lruIterator);
        lruOrder_.push_back(name);
        it->second.lruIterator = std::prev(lruOrder_.end());
        ++cacheHits_;
        return it->second.texture;
    }

    ++cacheMisses_;

    // Process texture data: decode, downsample, convert to 16-bit
    std::vector<uint8_t> processedData;
    int width = 0, height = 0;
    bool hasAlpha = false;

    if (!processTextureData(data, processedData, width, height, hasAlpha)) {
        return nullptr;
    }

    // Calculate size needed for this texture
    size_t textureSize = calculateTextureSize(width, height);

    // Evict textures if needed to make room
    if (!evictUntilAvailable(textureSize)) {
        // Cannot free enough space - texture is too large for budget
        return nullptr;
    }

    // Create Irrlicht texture from processed data
    // Always use 32-bit ARGB format for compatibility with Irrlicht's software renderer
    // (16-bit formats like ECF_A1R5G5B5/ECF_R5G6B5 can cause crashes in Burning's Video)
    irr::video::ITexture* texture = nullptr;

    // Convert RGBA to ARGB (Irrlicht's native format)
    std::vector<uint8_t> argbData(processedData.size());
    for (size_t i = 0; i < processedData.size(); i += 4) {
        argbData[i + 0] = processedData[i + 2];  // B
        argbData[i + 1] = processedData[i + 1];  // G
        argbData[i + 2] = processedData[i + 0];  // R
        argbData[i + 3] = processedData[i + 3];  // A
    }

    irr::core::dimension2d<irr::u32> size(width, height);
    irr::video::IImage* image = driver_->createImageFromData(
        irr::video::ECF_A8R8G8B8, size, argbData.data(), false);

    if (image) {
        texture = driver_->addTexture(name.c_str(), image);
        image->drop();
    }

    if (!texture) {
        return nullptr;
    }

    // Add to cache
    lruOrder_.push_back(name);
    CachedTexture entry;
    entry.texture = texture;
    entry.sizeBytes = textureSize;
    entry.lruIterator = std::prev(lruOrder_.end());
    cache_[name] = entry;
    currentUsage_ += textureSize;

    return texture;
}

void ConstrainedTextureCache::touch(const std::string& name) {
    auto it = cache_.find(name);
    if (it != cache_.end()) {
        // Move to back of LRU list
        lruOrder_.erase(it->second.lruIterator);
        lruOrder_.push_back(name);
        it->second.lruIterator = std::prev(lruOrder_.end());
    }
}

bool ConstrainedTextureCache::hasTexture(const std::string& name) const {
    return cache_.find(name) != cache_.end();
}

irr::video::ITexture* ConstrainedTextureCache::getTexture(const std::string& name) {
    auto it = cache_.find(name);
    if (it != cache_.end()) {
        touch(name);
        return it->second.texture;
    }
    return nullptr;
}

void ConstrainedTextureCache::clear() {
    for (auto& pair : cache_) {
        if (pair.second.texture) {
            driver_->removeTexture(pair.second.texture);
        }
    }
    cache_.clear();
    lruOrder_.clear();
    currentUsage_ = 0;
}

float ConstrainedTextureCache::getHitRate() const {
    size_t total = cacheHits_ + cacheMisses_;
    if (total == 0) return 0.0f;
    return (static_cast<float>(cacheHits_) / total) * 100.0f;
}

void ConstrainedTextureCache::resetStatistics() {
    cacheHits_ = 0;
    cacheMisses_ = 0;
    evictionCount_ = 0;
}

bool ConstrainedTextureCache::evictUntilAvailable(size_t bytesNeeded) {
    // Check if texture is too large to ever fit
    if (bytesNeeded > config_.textureMemoryBytes) {
        return false;
    }

    // Evict LRU textures until we have enough space
    // Safe eviction: clearTextureReferences() removes texture from mesh materials before deletion
    while (currentUsage_ + bytesNeeded > config_.textureMemoryBytes && !lruOrder_.empty()) {
        std::string oldest = lruOrder_.front();
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
    return static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
}

bool ConstrainedTextureCache::processTextureData(const std::vector<char>& rawData,
                                                  std::vector<uint8_t>& processedData,
                                                  int& width, int& height,
                                                  bool& hasAlpha) {
    // Decode based on format
    if (DDSDecoder::isDDS(rawData)) {
        if (!decodeDDS(rawData, processedData, width, height)) {
            return false;
        }
    } else {
        // Assume BMP
        if (!decodeBMP(rawData, processedData, width, height)) {
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
                                         int& width, int& height) {
    if (data.size() < 54) return false;  // Minimum BMP header size

    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.data());

    // Check BMP magic
    if (ptr[0] != 'B' || ptr[1] != 'M') return false;

    // Read header
    uint32_t dataOffset = *reinterpret_cast<const uint32_t*>(ptr + 10);
    int32_t bmpWidth = *reinterpret_cast<const int32_t*>(ptr + 18);
    int32_t bmpHeight = *reinterpret_cast<const int32_t*>(ptr + 22);
    uint16_t bitsPerPixel = *reinterpret_cast<const uint16_t*>(ptr + 28);

    if (bmpWidth <= 0 || bmpHeight == 0) return false;

    // Handle negative height (top-down DIB)
    bool topDown = (bmpHeight < 0);
    if (topDown) bmpHeight = -bmpHeight;

    width = bmpWidth;
    height = bmpHeight;

    // Calculate row size (must be aligned to 4 bytes)
    int bytesPerPixel = bitsPerPixel / 8;
    int rowSize = ((bmpWidth * bytesPerPixel + 3) / 4) * 4;

    if (dataOffset + rowSize * height > data.size()) return false;

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
                rgba[dstIdx + 3] = 255;             // A
            } else if (bitsPerPixel == 32) {
                rgba[dstIdx + 0] = row[x * 4 + 2];  // R
                rgba[dstIdx + 1] = row[x * 4 + 1];  // G
                rgba[dstIdx + 2] = row[x * 4 + 0];  // B
                rgba[dstIdx + 3] = row[x * 4 + 3];  // A
            } else if (bitsPerPixel == 8) {
                // 8-bit indexed - need palette
                // For simplicity, treat as grayscale
                uint8_t val = row[x];
                rgba[dstIdx + 0] = val;
                rgba[dstIdx + 1] = val;
                rgba[dstIdx + 2] = val;
                rgba[dstIdx + 3] = 255;
            } else {
                // Unsupported format
                return false;
            }
        }
    }

    return true;
}

bool ConstrainedTextureCache::decodeDDS(const std::vector<char>& data,
                                         std::vector<uint8_t>& rgba,
                                         int& width, int& height) {
    DecodedImage image = DDSDecoder::decode(data);
    if (!image.isValid()) {
        return false;
    }

    width = static_cast<int>(image.width);
    height = static_cast<int>(image.height);
    rgba = std::move(image.pixels);

    return true;
}

irr::video::ITexture* ConstrainedTextureCache::getPlaceholderTexture() {
    if (placeholderTexture_) {
        return placeholderTexture_;
    }

    // Create a small checkerboard placeholder texture (8x8 magenta/black)
    const int size = 8;
    std::vector<uint32_t> pixels(size * size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            // Checkerboard pattern: magenta and dark gray
            bool checker = ((x / 2) + (y / 2)) % 2 == 0;
            pixels[y * size + x] = checker ? 0xFFFF00FF : 0xFF202020;  // ARGB
        }
    }

    irr::video::IImage* image = driver_->createImageFromData(
        irr::video::ECF_A8R8G8B8,
        irr::core::dimension2d<irr::u32>(size, size),
        pixels.data(), false);

    if (image) {
        placeholderTexture_ = driver_->addTexture("_placeholder_", image);
        image->drop();
    }

    return placeholderTexture_;
}

void ConstrainedTextureCache::clearTextureReferences(irr::video::ITexture* texture) {
    if (!smgr_ || !texture) {
        return;
    }

    // Get placeholder to replace evicted textures
    irr::video::ITexture* placeholder = getPlaceholderTexture();

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
