#include "client/graphics/animated_texture_manager.h"
#include "client/graphics/eq/dds_decoder.h"
#include <algorithm>
#include <iostream>
#include <sstream>

namespace EQT {
namespace Graphics {

AnimatedTextureManager::AnimatedTextureManager(irr::video::IVideoDriver* driver,
                                               irr::io::IFileSystem* fileSystem)
    : driver_(driver), fileSystem_(fileSystem) {
}

irr::video::ITexture* AnimatedTextureManager::loadTexture(const std::string& name,
                                                           const std::vector<char>& data) {
    // Check cache first
    auto it = textureCache_.find(name);
    if (it != textureCache_.end()) {
        return it->second;
    }

    if (data.empty()) {
        return nullptr;
    }

    irr::video::ITexture* texture = nullptr;

    // Check if DDS format
    if (DDSDecoder::isDDS(data)) {
        DecodedImage decoded = DDSDecoder::decode(data);
        if (!decoded.pixels.empty()) {
            // Convert RGBA to ARGB for Irrlicht
            std::vector<uint8_t> argbData(decoded.pixels.size());
            for (size_t i = 0; i < decoded.pixels.size(); i += 4) {
                argbData[i] = decoded.pixels[i + 2];     // B
                argbData[i + 1] = decoded.pixels[i + 1]; // G
                argbData[i + 2] = decoded.pixels[i];     // R
                argbData[i + 3] = decoded.pixels[i + 3]; // A
            }

            irr::core::dimension2d<irr::u32> size(decoded.width, decoded.height);
            irr::video::IImage* image = driver_->createImageFromData(
                irr::video::ECF_A8R8G8B8, size,
                argbData.data(), false);

            if (image) {
                irr::io::path texName(name.c_str());
                texture = driver_->addTexture(texName, image);
                image->drop();
            }
        }
    } else if (data.size() >= 2 && data[0] == 'B' && data[1] == 'M' && fileSystem_) {
        // BMP format
        irr::io::IReadFile* memFile = fileSystem_->createMemoryReadFile(
            const_cast<char*>(data.data()), static_cast<irr::s32>(data.size()),
            name.c_str(), false);

        if (memFile) {
            irr::video::IImage* image = driver_->createImageFromFile(memFile);
            if (image) {
                irr::io::path texName(name.c_str());
                texture = driver_->addTexture(texName, image);
                image->drop();
            }
            memFile->drop();
        }
    }

    textureCache_[name] = texture;
    return texture;
}

int AnimatedTextureManager::initialize(const ZoneGeometry& geometry,
                                        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
                                        irr::scene::IMesh* mesh) {
    animatedTextures_.clear();
    int animatedCount = 0;

    // Find all animated textures from geometry
    for (size_t i = 0; i < geometry.textureAnimations.size(); ++i) {
        const TextureAnimationInfo& animInfo = geometry.textureAnimations[i];

        if (!animInfo.isAnimated || animInfo.frames.size() <= 1) {
            continue;  // Not animated or only 1 frame
        }

        std::string primaryName = (i < geometry.textureNames.size()) ?
                                   geometry.textureNames[i] : "";
        if (primaryName.empty() && !animInfo.frames.empty()) {
            primaryName = animInfo.frames[0];
        }

        // Convert to lowercase for lookup
        std::string lowerPrimaryName = primaryName;
        std::transform(lowerPrimaryName.begin(), lowerPrimaryName.end(),
                      lowerPrimaryName.begin(), ::tolower);

        // Already processed this texture?
        if (animatedTextures_.find(lowerPrimaryName) != animatedTextures_.end()) {
            continue;
        }

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
                irr::video::ITexture* frameTex = loadTexture(frameName, texIt->second->data);
                if (frameTex) {
                    state.frameTextures.push_back(frameTex);
                } else {
                    allFramesLoaded = false;
                    std::cerr << "AnimatedTextureManager: Failed to load frame texture: "
                              << frameName << std::endl;
                }
            } else {
                allFramesLoaded = false;
                std::cerr << "AnimatedTextureManager: Frame texture not found in archive: "
                          << frameName << std::endl;
            }
        }

        // Only add if we got at least 2 frames
        if (state.frameTextures.size() >= 2) {
            // Find mesh buffers using this texture
            // Also check for dds_ prefix since ZoneMeshBuilder adds that for DDS textures
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
                        }
                    }
                }
            }

            animatedTextures_[lowerPrimaryName] = state;
            animatedCount++;
        }
    }

    return animatedCount;
}

void AnimatedTextureManager::addMesh(const ZoneGeometry& geometry,
                                      const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
                                      irr::scene::IMesh* mesh) {
    if (!mesh) return;

    // For each animated texture we already know about, check if this mesh uses it
    for (auto& [primaryName, state] : animatedTextures_) {
        // Find mesh buffers using this texture (check both plain name and dds_ prefixed)
        std::string ddsPrimaryName = "dds_" + primaryName;
        for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
            irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(b);
            irr::video::ITexture* bufTex = buffer->getMaterial().getTexture(0);
            if (bufTex) {
                std::string bufTexName = bufTex->getName().getPath().c_str();
                std::transform(bufTexName.begin(), bufTexName.end(),
                              bufTexName.begin(), ::tolower);
                if (bufTexName == primaryName || bufTexName == ddsPrimaryName) {
                    // Check if this buffer is already tracked
                    bool alreadyTracked = false;
                    for (const auto* existingBuf : state.affectedBuffers) {
                        if (existingBuf == buffer) {
                            alreadyTracked = true;
                            break;
                        }
                    }
                    if (!alreadyTracked) {
                        state.affectedBuffers.push_back(buffer);
                    }
                }
            }
        }
    }

    // Also check for new animated textures from this mesh's geometry
    for (size_t i = 0; i < geometry.textureAnimations.size(); ++i) {
        const TextureAnimationInfo& animInfo = geometry.textureAnimations[i];

        if (!animInfo.isAnimated || animInfo.frames.size() <= 1) {
            continue;
        }

        std::string primaryName = (i < geometry.textureNames.size()) ?
                                   geometry.textureNames[i] : "";
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
                            if (existingBuf == buffer) {
                                alreadyTracked = true;
                                break;
                            }
                        }
                        if (!alreadyTracked) {
                            it->second.affectedBuffers.push_back(buffer);
                        }
                    }
                }
            }
            continue;
        }

        // New animated texture - load all frames
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
                irr::video::ITexture* frameTex = loadTexture(frameName, texIt->second->data);
                if (frameTex) {
                    state.frameTextures.push_back(frameTex);
                }
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
                    }
                }
            }

            animatedTextures_[lowerPrimaryName] = state;
        }
    }
}

void AnimatedTextureManager::addSceneNode(irr::scene::ISceneNode* node) {
    if (!node) return;

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
            }
        }
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
            for (irr::scene::IMeshBuffer* buffer : state.affectedBuffers) {
                buffer->getMaterial().setTexture(0, newTex);
            }
            // Update scene node materials (this is what actually affects rendering)
            for (const auto& am : state.affectedMaterials) {
                am.node->getMaterial(am.materialIndex).setTexture(0, newTex);
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

} // namespace Graphics
} // namespace EQT
