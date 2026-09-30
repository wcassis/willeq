#include "client/graphics/animated_texture_manager.h"
#include "common/logging.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace EQT {
namespace Graphics {

AnimatedTextureManager::AnimatedTextureManager(irr::video::IVideoDriver* driver,
                                               irr::io::IFileSystem* fileSystem)
    : driver_(driver), fileSystem_(fileSystem) {
}

AnimatedTextureManager::~AnimatedTextureManager() {
    if (constrainedCache_) {
        constrainedCache_->removeEvictionListener(this);
    }
    // Cache owns all textures — don't call driver_->removeTexture()
    textureCache_.clear();
    animatedTextures_.clear();
}

void AnimatedTextureManager::setConstrainedTextureCache(ConstrainedTextureCache* cache) {
    if (constrainedCache_) {
        constrainedCache_->removeEvictionListener(this);
    }
    constrainedCache_ = cache;
    if (constrainedCache_) {
        constrainedCache_->addEvictionListener(this);
    }
}

void AnimatedTextureManager::onTextureEvicted(const std::string& name, irr::video::ITexture* /*texture*/) {
    auto it = textureCache_.find(name);
    if (it == textureCache_.end()) return;

    irr::video::ITexture* evictedTex = it->second;
    it->second = nullptr;

    // Also null out matching frame pointers in animated textures
    if (evictedTex) {
        for (auto& [primaryName, state] : animatedTextures_) {
            for (auto& frameTex : state.frameTextures) {
                if (frameTex == evictedTex) {
                    frameTex = nullptr;
                }
            }
        }
    }
}

irr::video::ITexture* AnimatedTextureManager::loadTexture(const std::string& name,
                                                           const TextureInfo& info) {
    const std::vector<char>& data = info.data;
    // Check local cache first
    auto it = textureCache_.find(name);
    if (it != textureCache_.end()) {
        return it->second;
    }

    if (data.empty() || !constrainedCache_) {
        return nullptr;
    }

    // Use constrained cache: decode + upload
    // B947: with the archive origin, so an evicted frame can be restored.
    const TextureOrigin origin = info.origin();
    irr::video::ITexture* texture = constrainedCache_->getOrLoad(name, data, "uncategorized", "", false, &origin);
    if (!texture) {
        // Decoded but not yet uploaded — promote to GPU
        texture = constrainedCache_->uploadTexture(name);
    }

    textureCache_[name] = texture;
    return texture;
}

int AnimatedTextureManager::initialize(const ZoneGeometry& geometry,
                                        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
                                        irr::scene::IMesh* mesh) {
    animatedTextures_.clear();
    int animatedCount = 0;

    LOG_INFO(MOD_GRAPHICS, "AnimatedTextureManager::initialize: texAnimations={}, texNames={}, "
             "archiveTextures={}, mesh={}",
             geometry.textureAnimations().size(), geometry.textureNames().size(),
             textures.size(), mesh != nullptr);

    // Find all animated textures from geometry
    for (size_t i = 0; i < geometry.textureAnimations().size(); ++i) {
        const TextureAnimationInfo& animInfo = geometry.textureAnimations()[i];

        if (!animInfo.isAnimated || animInfo.frames.size() <= 1) {
            // Not logged: per-iteration skip for non-animated entries (high volume, no diagnostic value)
            continue;
        }

        std::string primaryName = (i < geometry.textureNames().size()) ?
                                   geometry.textureNames()[i] : "";
        if (primaryName.empty() && !animInfo.frames.empty()) {
            primaryName = animInfo.frames[0];
        }

        std::string lowerPrimaryName = primaryName;
        std::transform(lowerPrimaryName.begin(), lowerPrimaryName.end(),
                      lowerPrimaryName.begin(), ::tolower);

        // Already processed this texture?
        if (animatedTextures_.find(lowerPrimaryName) != animatedTextures_.end()) {
            LOG_TRACE(MOD_GRAPHICS, "  initialize[{}]: '{}' already discovered, skipping",
                      i, lowerPrimaryName);
            continue;
        }

        LOG_DEBUG(MOD_GRAPHICS, "  initialize[{}]: discovered animated texture '{}' "
                  "(frames={}, delayMs={})",
                  i, lowerPrimaryName, animInfo.frames.size(), animInfo.animationDelayMs);

        AnimatedTextureState state;
        state.animationDelayMs = animInfo.animationDelayMs > 0 ? animInfo.animationDelayMs : 100;
        state.currentFrame = 0;
        state.elapsedMs = 0;

        // Load all frame textures
        bool allFramesLoaded = true;
        for (const std::string& frameName : animInfo.frames) {
            std::string lowerFrameName = frameName;
            std::transform(lowerFrameName.begin(), lowerFrameName.end(),
                          lowerFrameName.begin(), ::tolower);

            auto texIt = textures.find(lowerFrameName);
            if (texIt != textures.end() && texIt->second) {
                irr::video::ITexture* frameTex = loadTexture(frameName, *texIt->second);
                if (frameTex) {
                    state.frameTextures.push_back(frameTex);
                    LOG_TRACE(MOD_GRAPHICS, "    frame '{}': loaded OK", frameName);
                } else {
                    allFramesLoaded = false;
                    LOG_WARN(MOD_GRAPHICS_LOAD, "AnimatedTextureManager: Failed to load frame texture: {}", frameName);
                }
            } else {
                allFramesLoaded = false;
                LOG_WARN(MOD_GRAPHICS_LOAD, "AnimatedTextureManager: Frame texture not found in archive: {}", frameName);
            }
        }

        // Only add if we got at least 2 frames
        if (state.frameTextures.size() >= 2) {
            std::string ddsLowerPrimaryName = "dds_" + lowerPrimaryName;
            if (mesh) {
                for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                    irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(b);
                    irr::video::ITexture* bufTex = buffer->getMaterial().getTexture(0);
                    if (bufTex) {
                        std::string bufTexName = bufTex->getName().getPath().c_str();
                        std::string lowerBufTexName = bufTexName;
                        std::transform(lowerBufTexName.begin(), lowerBufTexName.end(),
                                      lowerBufTexName.begin(), ::tolower);
                        if (lowerBufTexName == lowerPrimaryName || lowerBufTexName == ddsLowerPrimaryName) {
                            state.affectedBuffers.push_back(buffer);
                            LOG_TRACE(MOD_GRAPHICS, "    matched mesh buffer[{}] tex='{}'",
                                      b, lowerBufTexName);
                        }
                    }
                }
            }

            animatedTextures_[lowerPrimaryName] = state;
            animatedCount++;
            LOG_INFO(MOD_GRAPHICS, "  initialize: registered '{}' ({} frames, {}ms delay, {} buffers)",
                     lowerPrimaryName, state.frameTextures.size(), state.animationDelayMs,
                     state.affectedBuffers.size());
        } else {
            LOG_WARN(MOD_GRAPHICS, "  initialize[{}]: '{}' has animated flag but only {} frames loaded, skipping",
                     i, lowerPrimaryName, state.frameTextures.size());
        }
    }

    LOG_INFO(MOD_GRAPHICS, "AnimatedTextureManager::initialize: total={} animated textures discovered",
             animatedCount);
    return animatedCount;
}

void AnimatedTextureManager::addMesh(const ZoneGeometry& geometry,
                                      const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
                                      irr::scene::IMesh* mesh) {
    if (!mesh) return;

    LOG_TRACE(MOD_GRAPHICS, "AnimatedTextureManager::addMesh: buffers={}, texAnimations={}, texNames={}, "
              "knownAnimated={}", mesh->getMeshBufferCount(), geometry.textureAnimations().size(),
              geometry.textureNames().size(), animatedTextures_.size());

    // Phase 1: For each animated texture we already know about, check if this mesh uses it
    int existingMatched = 0;
    for (auto& [primaryName, state] : animatedTextures_) {
        std::string ddsPrimaryName = "dds_" + primaryName;
        for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
            irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(b);
            irr::video::ITexture* bufTex = buffer->getMaterial().getTexture(0);
            if (bufTex) {
                std::string bufTexName = bufTex->getName().getPath().c_str();
                std::transform(bufTexName.begin(), bufTexName.end(),
                              bufTexName.begin(), ::tolower);
                if (bufTexName == primaryName || bufTexName == ddsPrimaryName) {
                    bool alreadyTracked = false;
                    for (const auto* existingBuf : state.affectedBuffers) {
                        if (existingBuf == buffer) { alreadyTracked = true; break; }
                    }
                    if (!alreadyTracked) {
                        state.affectedBuffers.push_back(buffer);
                        existingMatched++;
                        LOG_TRACE(MOD_GRAPHICS, "  addMesh phase1: buffer[{}] tex='{}' matched existing anim '{}'",
                                  b, bufTexName, primaryName);
                    }
                }
            }
        }
    }

    // Phase 2: Check for new animated textures from this mesh's geometry
    int newDiscovered = 0;
    int newBuffersMatched = 0;
    for (size_t i = 0; i < geometry.textureAnimations().size(); ++i) {
        const TextureAnimationInfo& animInfo = geometry.textureAnimations()[i];

        if (!animInfo.isAnimated || animInfo.frames.size() <= 1) {
            // Not logged: per-iteration skip (high volume)
            continue;
        }

        std::string primaryName = (i < geometry.textureNames().size()) ?
                                   geometry.textureNames()[i] : "";
        if (primaryName.empty() && !animInfo.frames.empty()) {
            primaryName = animInfo.frames[0];
        }

        std::string lowerPrimaryName = primaryName;
        std::transform(lowerPrimaryName.begin(), lowerPrimaryName.end(),
                      lowerPrimaryName.begin(), ::tolower);

        // If we already have this animated texture, just add mesh buffers
        auto it = animatedTextures_.find(lowerPrimaryName);
        if (it != animatedTextures_.end()) {
            std::string ddsLowerPrimaryName = "dds_" + lowerPrimaryName;
            for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(b);
                irr::video::ITexture* bufTex = buffer->getMaterial().getTexture(0);
                if (bufTex) {
                    std::string bufTexName = bufTex->getName().getPath().c_str();
                    std::transform(bufTexName.begin(), bufTexName.end(),
                                  bufTexName.begin(), ::tolower);
                    if (bufTexName == lowerPrimaryName || bufTexName == ddsLowerPrimaryName) {
                        bool alreadyTracked = false;
                        for (const auto* existingBuf : it->second.affectedBuffers) {
                            if (existingBuf == buffer) { alreadyTracked = true; break; }
                        }
                        if (!alreadyTracked) {
                            it->second.affectedBuffers.push_back(buffer);
                            LOG_TRACE(MOD_GRAPHICS, "  addMesh phase2: buffer[{}] tex='{}' matched known anim '{}'",
                                      b, bufTexName, lowerPrimaryName);
                        }
                    }
                }
            }
            continue;
        }

        // New animated texture - load all frames
        LOG_DEBUG(MOD_GRAPHICS, "  addMesh phase2: NEW animated texture '{}' (index={}, frames={}, delayMs={})",
                  lowerPrimaryName, i, animInfo.frames.size(), animInfo.animationDelayMs);

        AnimatedTextureState state;
        state.animationDelayMs = animInfo.animationDelayMs > 0 ? animInfo.animationDelayMs : 100;
        state.currentFrame = 0;
        state.elapsedMs = 0;

        for (const std::string& frameName : animInfo.frames) {
            std::string lowerFrameName = frameName;
            std::transform(lowerFrameName.begin(), lowerFrameName.end(),
                          lowerFrameName.begin(), ::tolower);

            auto texIt = textures.find(lowerFrameName);
            if (texIt != textures.end() && texIt->second) {
                irr::video::ITexture* frameTex = loadTexture(frameName, *texIt->second);
                if (frameTex) {
                    state.frameTextures.push_back(frameTex);
                    LOG_TRACE(MOD_GRAPHICS, "    frame '{}': loaded OK", frameName);
                } else {
                    LOG_WARN(MOD_GRAPHICS, "  addMesh: failed to load frame texture: {}", frameName);
                }
            } else {
                LOG_WARN(MOD_GRAPHICS, "  addMesh: frame texture not found in archive: {}", frameName);
            }
        }

        if (state.frameTextures.size() >= 2) {
            std::string ddsLowerPrimaryName = "dds_" + lowerPrimaryName;
            for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(b);
                irr::video::ITexture* bufTex = buffer->getMaterial().getTexture(0);
                if (bufTex) {
                    std::string bufTexName = bufTex->getName().getPath().c_str();
                    std::transform(bufTexName.begin(), bufTexName.end(),
                                  bufTexName.begin(), ::tolower);
                    if (bufTexName == lowerPrimaryName || bufTexName == ddsLowerPrimaryName) {
                        state.affectedBuffers.push_back(buffer);
                        newBuffersMatched++;
                        LOG_TRACE(MOD_GRAPHICS, "    matched buffer[{}] tex='{}'", b, bufTexName);
                    }
                }
            }

            animatedTextures_[lowerPrimaryName] = state;
            newDiscovered++;
            LOG_INFO(MOD_GRAPHICS, "  addMesh: registered NEW anim '{}' ({} frames, {}ms, {} buffers)",
                     lowerPrimaryName, state.frameTextures.size(), state.animationDelayMs,
                     state.affectedBuffers.size());
        } else {
            LOG_WARN(MOD_GRAPHICS, "  addMesh: '{}' animated but only {} frames loaded, skipping",
                     lowerPrimaryName, state.frameTextures.size());
        }
    }

    if (existingMatched > 0 || newDiscovered > 0) {
        LOG_DEBUG(MOD_GRAPHICS, "AnimatedTextureManager::addMesh result: existingMatched={}, "
                  "newDiscovered={}, newBuffers={}, totalAnimated={}",
                  existingMatched, newDiscovered, newBuffersMatched, animatedTextures_.size());
    }
}

void AnimatedTextureManager::addSceneNode(irr::scene::ISceneNode* node) {
    if (!node) return;

    int matched = 0;
    // Scan all materials on this node for animated textures
    for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
        irr::video::ITexture* tex = node->getMaterial(i).getTexture(0);
        if (!tex) continue;

        std::string texName = tex->getName().getPath().c_str();
        std::transform(texName.begin(), texName.end(), texName.begin(), ::tolower);

        // Strip dds_ prefix if present
        std::string lookupName = texName;
        if (lookupName.substr(0, 4) == "dds_") {
            lookupName = lookupName.substr(4);
        }

        auto it = animatedTextures_.find(lookupName);
        if (it != animatedTextures_.end()) {
            // Check if already registered
            bool alreadyTracked = false;
            for (const auto& am : it->second.affectedMaterials) {
                if (am.node == node && am.materialIndex == i) {
                    alreadyTracked = true;
                    break;
                }
            }
            if (!alreadyTracked) {
                it->second.affectedMaterials.push_back({node, i});
                matched++;
                LOG_TRACE(MOD_GRAPHICS, "  addSceneNode: material[{}] tex='{}' matched anim '{}'",
                          i, texName, lookupName);
            }
        }
    }
    if (matched > 0) {
        LOG_DEBUG(MOD_GRAPHICS, "AnimatedTextureManager::addSceneNode: {} materials matched on node with {} materials total",
                  matched, node->getMaterialCount());
    }
}

void AnimatedTextureManager::removeSceneNode(irr::scene::ISceneNode* node) {
    if (!node) return;

    for (auto& [name, state] : animatedTextures_) {
        state.affectedMaterials.erase(
            std::remove_if(state.affectedMaterials.begin(), state.affectedMaterials.end(),
                [node](const AnimatedMaterial& am) { return am.node == node; }),
            state.affectedMaterials.end());
    }
}

void AnimatedTextureManager::update(float deltaMs) {
    for (auto& [name, state] : animatedTextures_) {
        if (state.frameTextures.size() <= 1) {
            continue;
        }

        state.elapsedMs += deltaMs;

        // Check if we need to advance to next frame
        if (state.elapsedMs >= static_cast<float>(state.animationDelayMs)) {
            state.elapsedMs = std::fmod(state.elapsedMs, static_cast<float>(state.animationDelayMs));
            state.currentFrame = (state.currentFrame + 1) % static_cast<int>(state.frameTextures.size());

            // Update all affected mesh buffers with the new texture
            irr::video::ITexture* newTex = state.frameTextures[state.currentFrame];
            if (!newTex) continue;  // Skip evicted frames
            for (irr::scene::IMeshBuffer* buffer : state.affectedBuffers) {
                buffer->getMaterial().setTexture(0, newTex);
            }
            // Update scene node materials (this is what actually affects rendering)
            for (const auto& am : state.affectedMaterials) {
                am.node->getMaterial(am.materialIndex).setTexture(0, newTex);
            }
            if (frameSwapHook_) {
                for (irr::video::ITexture* other : state.frameTextures) {
                    if (other && other != newTex) frameSwapHook_(other, newTex);
                }
            }
        }
    }
}

irr::video::ITexture* AnimatedTextureManager::getCurrentTexture(const std::string& primaryName) const {
    std::string lowerName = primaryName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

    auto it = animatedTextures_.find(lowerName);
    if (it != animatedTextures_.end() && !it->second.frameTextures.empty()) {
        return it->second.frameTextures[it->second.currentFrame];
    }
    return nullptr;
}

bool AnimatedTextureManager::isAnimated(const std::string& textureName) const {
    std::string lowerName = textureName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    return animatedTextures_.find(lowerName) != animatedTextures_.end();
}

std::string AnimatedTextureManager::getDebugInfo() const {
    std::stringstream ss;
    ss << "Animated textures: " << animatedTextures_.size() << "\n";
    for (const auto& [name, state] : animatedTextures_) {
        ss << "  " << name << ": " << state.frameTextures.size() << " frames, "
           << state.animationDelayMs << "ms, frame " << state.currentFrame
           << ", " << state.affectedBuffers.size() << " buffers\n";
    }
    return ss.str();
}

std::vector<std::string> AnimatedTextureManager::findTextureSequence(
    const std::string& baseName,
    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures) const {

    std::vector<std::string> frames;

    // Common extensions to try
    const std::vector<std::string> extensions = {"", ".bmp", ".dds", ".png"};

    // Try numbered patterns: base01, base02, ... or base1, base2, ...
    for (int num = 0; num <= 99; ++num) {
        bool found = false;

        // Try two-digit pattern (e.g., water01)
        std::stringstream ss2;
        ss2 << baseName << std::setfill('0') << std::setw(2) << num;
        std::string twoDigit = ss2.str();
        std::transform(twoDigit.begin(), twoDigit.end(), twoDigit.begin(), ::tolower);

        for (const auto& ext : extensions) {
            std::string name = twoDigit + ext;
            auto it = textures.find(name);
            if (it != textures.end()) {
                frames.push_back(name);
                found = true;
                break;
            }
        }
        if (found) continue;

        // Try single-digit pattern (e.g., water1, w1)
        std::stringstream ss1;
        ss1 << baseName << num;
        std::string oneDigit = ss1.str();
        std::transform(oneDigit.begin(), oneDigit.end(), oneDigit.begin(), ::tolower);

        for (const auto& ext : extensions) {
            std::string name = oneDigit + ext;
            auto it = textures.find(name);
            if (it != textures.end()) {
                frames.push_back(name);
                found = true;
                break;
            }
        }

        // If we haven't found anything after checking num=10 and no frames yet, stop early
        if (num >= 10 && frames.empty()) {
            break;
        }
        // If we found frames but this number is missing, stop (sequence ended)
        if (!found && !frames.empty()) {
            break;
        }
    }

    return frames;
}

int AnimatedTextureManager::detectWaterAnimations(
    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
    irr::scene::IMesh* mesh) {

    int addedCount = 0;

    // Common water texture base names in EQ
    // Note: "w" catches w1, w2, w3, w4 pattern used in oasis and other zones
    std::vector<std::string> waterBases = {
        "water", "watr", "lava", "lavaf", "lavab", "wtr",
        "river", "sea", "ocean", "swamp", "pond", "lake", "w"
    };

    // Look for texture sequences for each base
    for (const std::string& baseName : waterBases) {
        std::string lowerBase = baseName;
        std::transform(lowerBase.begin(), lowerBase.end(), lowerBase.begin(), ::tolower);

        // Skip if we already have this animated
        if (animatedTextures_.find(lowerBase) != animatedTextures_.end()) {
            continue;
        }

        // Find sequence
        std::vector<std::string> frames = findTextureSequence(baseName, textures);

        // Need at least 2 frames for animation
        if (frames.size() < 2) {
            continue;
        }

        // Create animated texture state
        AnimatedTextureState state;
        state.animationDelayMs = 150;  // Default water animation speed
        state.currentFrame = 0;
        state.elapsedMs = 0;

        // Load all frame textures
        for (const std::string& frameName : frames) {
            auto texIt = textures.find(frameName);
            if (texIt != textures.end() && texIt->second) {
                irr::video::ITexture* frameTex = loadTexture(frameName, *texIt->second);
                if (frameTex) {
                    state.frameTextures.push_back(frameTex);
                }
            }
        }

        if (state.frameTextures.size() < 2) {
            continue;  // Failed to load enough frames
        }

        // Find mesh buffers using any of these textures
        if (mesh) {
            for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(b);
                irr::video::ITexture* bufTex = buffer->getMaterial().getTexture(0);
                if (!bufTex) continue;

                std::string bufTexName = bufTex->getName().getPath().c_str();
                std::transform(bufTexName.begin(), bufTexName.end(), bufTexName.begin(), ::tolower);

                // Strip dds_ prefix
                if (bufTexName.substr(0, 4) == "dds_") {
                    bufTexName = bufTexName.substr(4);
                }

                // Check if this buffer uses any frame texture
                for (const std::string& frameName : frames) {
                    if (bufTexName == frameName) {
                        state.affectedBuffers.push_back(buffer);
                        break;
                    }
                }
            }
        }

        // Only add if we found mesh buffers using it
        if (!state.affectedBuffers.empty() || state.affectedMaterials.empty()) {
            // Use first frame as key
            animatedTextures_[frames[0]] = state;
            addedCount++;
            LOG_DEBUG(MOD_GRAPHICS, "AnimatedTextureManager: Auto-detected water animation '{}' with {} frames, {} buffers",
                      frames[0], state.frameTextures.size(), state.affectedBuffers.size());
        }
    }

    return addedCount;
}

} // namespace Graphics
} // namespace EQT
