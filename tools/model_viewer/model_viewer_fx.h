// tools/model_viewer/model_viewer_fx.h -- ModelViewerFX, the particle-effect
// driver the viewer uses to visualise a cast.
//
// T385 (2026-09-05, board B167): split out of tools/model_viewer_spell_bar.h,
// which was 1,965 lines -- over the ceiling that check_file_sizes.sh did not
// apply to tools/ until this unit. Declarations moved verbatim; the original
// header is now an umbrella that includes this one.

#pragma once

#include <irrlicht.h>
#include <vector>
#include <string>
#include <functional>
#include <cstdint>
#include <fstream>
#include <cmath>
#include <unordered_map>
#include "client/graphics/eq/dds_decoder.h"
#include "model_viewer/spell_effect_db.h"
#include "model_viewer/spell_bar_ui.h"
// Forward declaration for hand bone queries
namespace EQT {
namespace Graphics {
class EQAnimatedMeshSceneNode;
}
}

namespace EQT {

// ModelViewerFX - Particle effects for spell casting visualization
// ============================================================================
class ModelViewerFX {
public:
    ModelViewerFX() = default;
    ~ModelViewerFX() {
        // Don't call clearAllEffects() here - the scene manager may already be destroyed
        // if we're being killed by SIGTERM or during abnormal exit.
        // clearAllEffects() should be called explicitly before device->drop().
    }

    void initialize(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                    const std::string& eqClientPath) {
        m_smgr = smgr;
        m_driver = driver;
        m_eqClientPath = eqClientPath;
        loadTextures();

        // Load spell effect database (EDD/EFF files)
        if (m_spellEffectDB.load(eqClientPath)) {
            std::cout << "Loaded spell effect database: "
                      << m_spellEffectDB.eddParser().size() << " emitters, "
                      << m_spellEffectDB.effParser().size() << " spell effects" << std::endl;
        }
    }

    // Get authentic emitter parameters for a spell
    const EmitterDefinition* getAuthenticEmitter(const SpellBarEntry& spell) const {
        // First try to find by resist type
        const EmitterDefinition* emitter = m_spellEffectDB.getEmitterForResistType(spell.resistType);
        if (emitter) return emitter;

        // Fall back to category-based lookup
        return m_spellEffectDB.getEmitterForCategory(spell.category);
    }

    // Access to spell effect database
    const SpellEffectDatabase& getSpellEffectDB() const { return m_spellEffectDB; }
    SpellEffectDatabase& getSpellEffectDB() { return m_spellEffectDB; }

    // Create casting effect using a specific EmitterDefinition
    void createCastingEffectFromEmitter(Graphics::EQAnimatedMeshSceneNode* animNode,
                                         const EmitterDefinition& emitter,
                                         const SpellBarEntry& spell) {
        if (!m_smgr || !animNode) return;

        m_animatedNode = animNode;
        m_currentSpellColor = emitter.getColor();  // Use emitter color
        m_currentResistType = spell.resistType;

        // Find hand bone indices
        m_rightHandBoneIndex = animNode->findRightHandBoneIndex();
        m_leftHandBoneIndex = animNode->findLeftHandBoneIndex();

        std::cout << "Using emitter: " << emitter.name << " (texture: " << emitter.texture << ")" << std::endl;
        std::cout << "  Particles: burst " << emitter.burstCount << ", " << emitter.perEmission
                  << " x " << emitter.emissionRate << "/s, shape " << emitter.shape
                  << " radius " << emitter.shapeRadiusX << ", up speed " << emitter.upSpeedMax << std::endl;

        // Get initial hand positions
        irr::core::vector3df rightPos, leftPos;
        bool hasRightHand = (m_rightHandBoneIndex >= 0) &&
                            animNode->getBoneWorldPosition(m_rightHandBoneIndex, rightPos);
        bool hasLeftHand = (m_leftHandBoneIndex >= 0) &&
                           animNode->getBoneWorldPosition(m_leftHandBoneIndex, leftPos);

        // Create particle systems using emitter definition
        if (hasRightHand) {
            m_rightHandEffect = createHandParticleSystemFromEmitter(rightPos, emitter);
        }
        if (hasLeftHand) {
            m_leftHandEffect = createHandParticleSystemFromEmitter(leftPos, emitter);
        }

        // Create body glow using emitter colors
        createBodyGlowEffectFromEmitter(animNode, emitter);
    }

    // Create casting effect with hand-emanating particles (Phase 4)
    // Uses EQAnimatedMeshSceneNode to get hand bone positions
    void createCastingEffect(Graphics::EQAnimatedMeshSceneNode* animNode, const SpellBarEntry& spell) {
        if (!m_smgr || !animNode) return;

        m_animatedNode = animNode;
        m_currentSpellColor = spell.getColor();
        m_currentResistType = spell.resistType;

        // Find hand bone indices
        m_rightHandBoneIndex = animNode->findRightHandBoneIndex();
        m_leftHandBoneIndex = animNode->findLeftHandBoneIndex();

        std::cout << "Hand bone indices: R=" << m_rightHandBoneIndex
                  << " L=" << m_leftHandBoneIndex << std::endl;

        // Get initial hand positions
        irr::core::vector3df rightPos, leftPos;
        bool hasRightHand = (m_rightHandBoneIndex >= 0) &&
                            animNode->getBoneWorldPosition(m_rightHandBoneIndex, rightPos);
        bool hasLeftHand = (m_leftHandBoneIndex >= 0) &&
                           animNode->getBoneWorldPosition(m_leftHandBoneIndex, leftPos);

        // Create particle systems for each hand
        if (hasRightHand) {
            m_rightHandEffect = createHandParticleSystem(rightPos, spell);
            std::cout << "Created right hand particles at: " << rightPos.X << ", "
                      << rightPos.Y << ", " << rightPos.Z << std::endl;
        }

        if (hasLeftHand) {
            m_leftHandEffect = createHandParticleSystem(leftPos, spell);
            std::cout << "Created left hand particles at: " << leftPos.X << ", "
                      << leftPos.Y << ", " << leftPos.Z << std::endl;
        }

        // Also create body glow effect for visual richness
        createBodyGlowEffect(animNode, spell);
    }

    // Backwards-compatible overload for non-animated nodes
    void createCastingEffect(irr::scene::ISceneNode* casterNode, const SpellBarEntry& spell) {
        if (!m_smgr || !casterNode) return;

        // Clear any previous animated node reference
        m_animatedNode = nullptr;
        m_rightHandBoneIndex = -1;
        m_leftHandBoneIndex = -1;

        // Fall back to body-centered effect
        createBodyGlowEffect(casterNode, spell);
    }

private:
    // Create particle system at hand position
    irr::scene::IParticleSystemSceneNode* createHandParticleSystem(
        const irr::core::vector3df& pos, const SpellBarEntry& spell) {

        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return nullptr;

        irr::video::SColor color = spell.getColor();

        // Point emitter at hand with outward/upward direction
        irr::scene::IParticleEmitter* emitter = ps->createPointEmitter(
            irr::core::vector3df(0, 0.03f, 0),    // Slight upward direction
            40, 60,                                // Higher particle rate for visibility
            color, color,                          // Color based on spell type
            300, 600,                              // Shorter lifetime for hand effects
            30,                                    // Max angle for spread
            irr::core::dimension2df(0.6f, 0.6f),  // Min size
            irr::core::dimension2df(1.2f, 1.2f)   // Max size
        );

        ps->setEmitter(emitter);
        emitter->drop();

        // Fade out affector
        auto* fadeAff = ps->createFadeOutParticleAffector(irr::video::SColor(0, 0, 0, 0), 150);
        ps->addAffector(fadeAff);
        fadeAff->drop();

        // Set texture
        irr::video::ITexture* tex = getTextureForResistType(spell.resistType);
        if (tex) {
            ps->setMaterialTexture(0, tex);
        }
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        return ps;
    }

    // Create body glow effect (used alongside hand effects or as fallback)
    void createBodyGlowEffect(irr::scene::ISceneNode* casterNode, const SpellBarEntry& spell) {
        // Get caster position
        irr::core::vector3df pos = casterNode->getAbsolutePosition();
        pos.Y += 3.0f;  // Offset above character center

        // Create particle system for casting glow
        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return;

        irr::video::SColor color = spell.getColor();

        // Box emitter around caster
        irr::scene::IParticleEmitter* emitter = ps->createBoxEmitter(
            irr::core::aabbox3df(-1.5f, -2.0f, -1.5f, 1.5f, 2.0f, 1.5f),  // Box around caster
            irr::core::vector3df(0, 0.02f, 0),     // Slight upward direction
            20, 35,                                  // Lower rate since we have hand effects too
            color, color,                            // Color
            500, 1000,                               // Lifetime ms
            45,                                      // Max angle
            irr::core::dimension2df(0.6f, 0.6f),    // Min size
            irr::core::dimension2df(1.2f, 1.2f)     // Max size
        );

        ps->setEmitter(emitter);
        emitter->drop();

        // Fade out affector
        auto* fadeAff = ps->createFadeOutParticleAffector(irr::video::SColor(0, 0, 0, 0), 200);
        ps->addAffector(fadeAff);
        fadeAff->drop();

        // Set texture
        irr::video::ITexture* tex = getTextureForResistType(spell.resistType);
        if (tex) {
            ps->setMaterialTexture(0, tex);
        }
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        m_castingEffect = ps;
        m_castingNode = casterNode;
    }

    // Create hand particle system using EmitterDefinition parameters
    irr::scene::IParticleSystemSceneNode* createHandParticleSystemFromEmitter(
        const irr::core::vector3df& pos, const EmitterDefinition& emitter) {

        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return nullptr;

        irr::video::SColor color = emitter.getColor();

        // Use emitter parameters with sensible defaults
        // Approximate viewer mapping of the B1141 code-proven fields (not the client's simulation).
        int minRate = std::max(10u, emitter.burstCount / 4);
        int maxRate = std::max(20u, emitter.perEmission / 4);
        float velocity = std::max(0.02f, emitter.upSpeedMax * 0.01f);
        int lifetime = static_cast<int>(std::max(200.0f, emitter.particleLifetime * 1000.0f));
        float sizeMin = std::max(0.3f, emitter.spriteWidth * 0.5f);
        float sizeMax = std::max(0.6f, emitter.spriteWidthMax * 0.5f);

        // Point emitter at hand
        irr::scene::IParticleEmitter* em = ps->createPointEmitter(
            irr::core::vector3df(0, velocity, 0),
            minRate, maxRate,
            color, color,
            lifetime, lifetime * 2,
            30,
            irr::core::dimension2df(sizeMin, sizeMin),
            irr::core::dimension2df(sizeMax, sizeMax)
        );

        ps->setEmitter(em);
        em->drop();

        // Fade out affector
        auto* fadeAff = ps->createFadeOutParticleAffector(irr::video::SColor(0, 0, 0, 0), lifetime / 2);
        ps->addAffector(fadeAff);
        fadeAff->drop();

        // Use default texture (TODO: load emitter.texture from SpellEffects folder)
        if (m_defaultTexture) {
            ps->setMaterialTexture(0, m_defaultTexture);
        }
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        return ps;
    }

    // Create body glow using EmitterDefinition
    void createBodyGlowEffectFromEmitter(irr::scene::ISceneNode* casterNode, const EmitterDefinition& emitter) {
        irr::core::vector3df pos = casterNode->getAbsolutePosition();
        pos.Y += 3.0f;

        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return;

        irr::video::SColor color = emitter.getColor();

        // Use emitter parameters
        int minRate = std::max(15u, emitter.burstCount / 3);
        int maxRate = std::max(30u, emitter.perEmission / 3);
        float radius = std::max(1.5f, emitter.shapeRadiusX);
        int lifetime = static_cast<int>(std::max(400.0f, emitter.particleLifetime * 1000.0f));
        float sizeMin = std::max(0.5f, emitter.spriteWidth * 0.4f);
        float sizeMax = std::max(1.0f, emitter.spriteWidthMax * 0.4f);

        irr::scene::IParticleEmitter* em = ps->createBoxEmitter(
            irr::core::aabbox3df(-radius, -2.0f, -radius, radius, 2.0f, radius),
            irr::core::vector3df(0, 0.02f, 0),
            minRate, maxRate,
            color, color,
            lifetime, lifetime * 2,
            45,
            irr::core::dimension2df(sizeMin, sizeMin),
            irr::core::dimension2df(sizeMax, sizeMax)
        );

        ps->setEmitter(em);
        em->drop();

        auto* fadeAff = ps->createFadeOutParticleAffector(irr::video::SColor(0, 0, 0, 0), lifetime / 2);
        ps->addAffector(fadeAff);
        fadeAff->drop();

        if (m_defaultTexture) {
            ps->setMaterialTexture(0, m_defaultTexture);
        }
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        m_castingEffect = ps;
        m_castingNode = casterNode;
    }

public:

    // Create completion effect based on spell category
    void createCompletionEffect(irr::scene::ISceneNode* casterNode, const SpellBarEntry& spell) {
        if (!m_smgr || !casterNode) return;

        irr::core::vector3df pos = casterNode->getAbsolutePosition();
        pos.Y += 3.0f;

        irr::video::SColor color = spell.getColor();

        // Different effects based on category
        switch (spell.category) {
            case SpellCategory::DirectDamage:
            case SpellCategory::AEDamage:
            case SpellCategory::PBAE:
                createBurstEffect(pos, color, spell.resistType);
                break;

            case SpellCategory::DoT:
                createLingeringEffect(pos, color, spell.resistType);
                break;

            case SpellCategory::Heal:
                createHealEffect(pos, spell.resistType);
                break;

            case SpellCategory::Buff:
                createBuffEffect(pos, color, spell.resistType);
                break;

            case SpellCategory::SummonPet:
            case SpellCategory::Gate:
                createPortalEffect(pos, color, spell.resistType);
                break;

            case SpellCategory::Root:
            case SpellCategory::Mez:
            case SpellCategory::Stun:
                createCCEffect(pos, color, spell.resistType);
                break;

            default:
                createBurstEffect(pos, color, spell.resistType);
                break;
        }
    }

    // Stop casting effect
    void stopCastingEffect() {
        // Stop body glow effect
        if (m_castingEffect) {
            m_castingEffect->setEmitter(nullptr);  // Stop emitting
            // Schedule for removal after particles fade
            m_pendingRemoval.push_back({m_castingEffect, 1.5f});
            m_castingEffect = nullptr;
            m_castingNode = nullptr;
        }

        // Stop hand effects
        if (m_rightHandEffect) {
            m_rightHandEffect->setEmitter(nullptr);
            m_pendingRemoval.push_back({m_rightHandEffect, 1.0f});
            m_rightHandEffect = nullptr;
        }

        if (m_leftHandEffect) {
            m_leftHandEffect->setEmitter(nullptr);
            m_pendingRemoval.push_back({m_leftHandEffect, 1.0f});
            m_leftHandEffect = nullptr;
        }

        // Clear animated node reference
        m_animatedNode = nullptr;
        m_rightHandBoneIndex = -1;
        m_leftHandBoneIndex = -1;
    }

    // Update effects (call each frame)
    void update(float deltaSeconds) {
        // Update hand effect positions to follow hand bones
        if (m_animatedNode) {
            irr::core::vector3df pos;

            // Update right hand particles
            if (m_rightHandEffect && m_rightHandBoneIndex >= 0) {
                if (m_animatedNode->getBoneWorldPosition(m_rightHandBoneIndex, pos)) {
                    m_rightHandEffect->setPosition(pos);
                }
            }

            // Update left hand particles
            if (m_leftHandEffect && m_leftHandBoneIndex >= 0) {
                if (m_animatedNode->getBoneWorldPosition(m_leftHandBoneIndex, pos)) {
                    m_leftHandEffect->setPosition(pos);
                }
            }
        }

        // Update casting effect position to follow caster
        if (m_castingEffect && m_castingNode) {
            irr::core::vector3df pos = m_castingNode->getAbsolutePosition();
            pos.Y += 3.0f;
            m_castingEffect->setPosition(pos);
        }

        // Update completion effects
        for (auto it = m_completionEffects.begin(); it != m_completionEffects.end(); ) {
            it->lifetime -= deltaSeconds;
            if (it->lifetime <= 0) {
                if (it->ps) {
                    it->ps->remove();
                }
                it = m_completionEffects.erase(it);
            } else {
                ++it;
            }
        }

        // Update pending removals
        for (auto it = m_pendingRemoval.begin(); it != m_pendingRemoval.end(); ) {
            it->second -= deltaSeconds;
            if (it->second <= 0) {
                if (it->first) {
                    it->first->remove();
                }
                it = m_pendingRemoval.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Clear all effects
    void clearAllEffects() {
        // Clear hand effects first (stopCastingEffect will handle them too)
        if (m_rightHandEffect) {
            m_rightHandEffect->remove();
            m_rightHandEffect = nullptr;
        }
        if (m_leftHandEffect) {
            m_leftHandEffect->remove();
            m_leftHandEffect = nullptr;
        }

        stopCastingEffect();
        for (auto& effect : m_completionEffects) {
            if (effect.ps) {
                effect.ps->remove();
            }
        }
        m_completionEffects.clear();
        for (auto& pr : m_pendingRemoval) {
            if (pr.first) {
                pr.first->remove();
            }
        }
        m_pendingRemoval.clear();

        // Clear animated node reference
        m_animatedNode = nullptr;
        m_rightHandBoneIndex = -1;
        m_leftHandBoneIndex = -1;
    }

private:
    struct CompletionEffect {
        irr::scene::IParticleSystemSceneNode* ps = nullptr;
        float lifetime = 0;
    };

    void loadTextures() {
        if (!m_driver) return;

        std::string path = m_eqClientPath;
        if (!path.empty() && path.back() != '/') path += '/';
        path += "SpellEffects/";

        // Load textures for each resist type
        m_defaultTexture = loadDDSTexture(path + "Glowflare03.dds");
        if (!m_defaultTexture) m_defaultTexture = loadDDSTexture(path + "glittersp501.dds");
        if (!m_defaultTexture) createFallbackTexture();

        m_fireTexture = loadDDSTexture(path + "Firesp501.dds");
        if (!m_fireTexture) m_fireTexture = loadDDSTexture(path + "flamesp501.dds");

        m_coldTexture = loadDDSTexture(path + "frostsp501.dds");
        if (!m_coldTexture) m_coldTexture = loadDDSTexture(path + "frostsp502.dds");

        m_magicTexture = loadDDSTexture(path + "ElectricA.dds");
        if (!m_magicTexture) m_magicTexture = loadDDSTexture(path + "ElectricB.dds");

        m_poisonTexture = loadDDSTexture(path + "PoisonC.dds");
        if (!m_poisonTexture) m_poisonTexture = loadDDSTexture(path + "Acid1.dds");

        m_diseaseTexture = loadDDSTexture(path + "diseasesp501.dds");
        if (!m_diseaseTexture) m_diseaseTexture = loadDDSTexture(path + "genbrownA.dds");

        m_chromaticTexture = loadDDSTexture(path + "Corona3.dds");
        m_corruptionTexture = loadDDSTexture(path + "darknesssp501.dds");

        // Fallback to default if not loaded
        if (!m_fireTexture) m_fireTexture = m_defaultTexture;
        if (!m_coldTexture) m_coldTexture = m_defaultTexture;
        if (!m_magicTexture) m_magicTexture = m_defaultTexture;
        if (!m_poisonTexture) m_poisonTexture = m_defaultTexture;
        if (!m_diseaseTexture) m_diseaseTexture = m_defaultTexture;
        if (!m_chromaticTexture) m_chromaticTexture = m_defaultTexture;
        if (!m_corruptionTexture) m_corruptionTexture = m_defaultTexture;

        std::cout << "Loaded spell effect textures" << std::endl;
    }

    irr::video::ITexture* loadDDSTexture(const std::string& path) {
        if (!m_driver) return nullptr;

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            std::cout << "Could not load texture: " << path << std::endl;
            return nullptr;
        }

        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> data(fileSize);
        if (!file.read(data.data(), fileSize)) {
            std::cout << "Could not read texture: " << path << std::endl;
            return nullptr;
        }

        // Use DDSDecoder to decode the DDS file
        if (!Graphics::DDSDecoder::isDDS(data)) {
            std::cout << "Not a valid DDS file: " << path << std::endl;
            return nullptr;
        }

        Graphics::DecodedImage decoded = Graphics::DDSDecoder::decode(data);
        if (!decoded.isValid()) {
            std::cout << "Failed to decode DDS: " << path << std::endl;
            return nullptr;
        }

        // Create Irrlicht image from decoded RGBA data
        irr::core::dimension2du dim(decoded.width, decoded.height);
        irr::video::IImage* image = m_driver->createImage(irr::video::ECF_A8R8G8B8, dim);
        if (!image) {
            std::cout << "Failed to create image for: " << path << std::endl;
            return nullptr;
        }

        // Copy decoded pixels to Irrlicht image (RGBA to ARGB)
        uint8_t* dest = static_cast<uint8_t*>(image->lock());
        if (dest) {
            const uint8_t* src = decoded.pixels.data();
            for (size_t i = 0; i < decoded.width * decoded.height; ++i) {
                // Source is RGBA, dest is ARGB (Irrlicht's ECF_A8R8G8B8)
                dest[i * 4 + 0] = src[i * 4 + 2]; // B
                dest[i * 4 + 1] = src[i * 4 + 1]; // G
                dest[i * 4 + 2] = src[i * 4 + 0]; // R
                dest[i * 4 + 3] = src[i * 4 + 3]; // A
            }
            image->unlock();
        }

        // Extract filename for texture name
        std::string texName = path;
        size_t lastSlash = texName.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            texName = texName.substr(lastSlash + 1);
        }

        // Create texture from image
        irr::video::ITexture* tex = m_driver->addTexture(texName.c_str(), image);
        image->drop();

        if (tex) {
            std::cout << "Loaded spell texture: " << texName << " (" << decoded.width << "x" << decoded.height << ")" << std::endl;
        }

        return tex;
    }

    void createFallbackTexture() {
        if (!m_driver) return;

        const int size = 32;
        irr::core::dimension2du dim(size, size);
        irr::video::IImage* image = m_driver->createImage(irr::video::ECF_A8R8G8B8, dim);
        if (!image) return;

        float center = size / 2.0f;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                float dx = x - center + 0.5f;
                float dy = y - center + 0.5f;
                float dist = std::sqrt(dx * dx + dy * dy);
                float normalizedDist = dist / center;
                int alpha = 0;
                if (normalizedDist < 1.0f) {
                    float falloff = std::cos(normalizedDist * 3.14159f * 0.5f);
                    alpha = static_cast<int>(falloff * 255.0f);
                }
                image->setPixel(x, y, irr::video::SColor(alpha, 255, 255, 255));
            }
        }

        m_defaultTexture = m_driver->addTexture("spell_fallback", image);
        image->drop();
    }

    irr::video::ITexture* getTextureForResistType(ResistType type) {
        switch (type) {
            case ResistType::Fire:       return m_fireTexture;
            case ResistType::Cold:       return m_coldTexture;
            case ResistType::Magic:      return m_magicTexture;
            case ResistType::Poison:     return m_poisonTexture;
            case ResistType::Disease:    return m_diseaseTexture;
            case ResistType::Chromatic:
            case ResistType::Prismatic:  return m_chromaticTexture;
            case ResistType::Corruption: return m_corruptionTexture;
            default:                     return m_defaultTexture;
        }
    }

    // Burst effect for damage spells
    void createBurstEffect(const irr::core::vector3df& pos, irr::video::SColor color, ResistType type) {
        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return;

        // Ring emitter for expanding burst
        auto* emitter = ps->createRingEmitter(
            irr::core::vector3df(0, 0, 0),
            0.5f, 3.0f,                              // Expanding ring
            irr::core::vector3df(0, 0.05f, 0),       // Slight upward
            80, 120,                                  // High particle rate
            color, color,
            200, 400,
            60,
            irr::core::dimension2df(1.5f, 1.5f),
            irr::core::dimension2df(3.0f, 3.0f)
        );
        ps->setEmitter(emitter);
        emitter->drop();

        auto* fadeAff = ps->createFadeOutParticleAffector();
        ps->addAffector(fadeAff);
        fadeAff->drop();

        auto* tex = getTextureForResistType(type);
        if (tex) ps->setMaterialTexture(0, tex);
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        // Track effect lifetime - particles will fade naturally
        // Don't add to m_pendingRemoval since that would double-remove the node
        m_completionEffects.push_back({ps, 1.5f});
    }

    // Lingering effect for DoTs
    void createLingeringEffect(const irr::core::vector3df& pos, irr::video::SColor color, ResistType type) {
        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return;

        // Cylinder emitter for cloud effect
        auto* emitter = ps->createCylinderEmitter(
            irr::core::vector3df(0, 0, 0),
            2.0f, irr::core::vector3df(0, 1, 0), 3.0f,  // Radius, normal, length
            false,                                       // Not outline only
            irr::core::vector3df(0, 0.01f, 0),          // Slow rise
            20, 40,                                      // Lower particle rate
            color, color,
            800, 1500,                                   // Longer lifetime
            30,
            irr::core::dimension2df(1.0f, 1.0f),
            irr::core::dimension2df(2.5f, 2.5f)
        );
        ps->setEmitter(emitter);
        emitter->drop();

        auto* fadeAff = ps->createFadeOutParticleAffector();
        ps->addAffector(fadeAff);
        fadeAff->drop();

        auto* tex = getTextureForResistType(type);
        if (tex) ps->setMaterialTexture(0, tex);
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        m_completionEffects.push_back({ps, 2.5f});
    }

    // Rising sparkles for heals
    void createHealEffect(const irr::core::vector3df& pos, ResistType type) {
        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return;

        irr::video::SColor healColor(255, 200, 255, 200);  // Light green/gold

        auto* emitter = ps->createBoxEmitter(
            irr::core::aabbox3df(-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f),
            irr::core::vector3df(0, 0.08f, 0),          // Rise upward
            40, 70,
            healColor, irr::video::SColor(255, 255, 255, 150),
            600, 1200,
            20,
            irr::core::dimension2df(0.5f, 0.5f),
            irr::core::dimension2df(1.2f, 1.2f)
        );
        ps->setEmitter(emitter);
        emitter->drop();

        auto* fadeAff = ps->createFadeOutParticleAffector();
        ps->addAffector(fadeAff);
        fadeAff->drop();

        auto* tex = getTextureForResistType(type);
        if (tex) ps->setMaterialTexture(0, tex);
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        m_completionEffects.push_back({ps, 2.0f});
    }

    // Swirling aura for buffs
    void createBuffEffect(const irr::core::vector3df& pos, irr::video::SColor color, ResistType type) {
        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return;

        // Ring emitter for swirling effect
        auto* emitter = ps->createRingEmitter(
            irr::core::vector3df(0, 0, 0),
            1.5f, 2.5f,
            irr::core::vector3df(0, 0.03f, 0),
            30, 50,
            color, color,
            600, 1000,
            40,
            irr::core::dimension2df(0.8f, 0.8f),
            irr::core::dimension2df(1.5f, 1.5f)
        );
        ps->setEmitter(emitter);
        emitter->drop();

        // Rotation affector for swirl
        auto* rotAff = ps->createRotationAffector(irr::core::vector3df(0, 50.0f, 0));
        ps->addAffector(rotAff);
        rotAff->drop();

        auto* fadeAff = ps->createFadeOutParticleAffector();
        ps->addAffector(fadeAff);
        fadeAff->drop();

        auto* tex = getTextureForResistType(type);
        if (tex) ps->setMaterialTexture(0, tex);
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        m_completionEffects.push_back({ps, 2.0f});
    }

    // Portal effect for summons/gates
    void createPortalEffect(const irr::core::vector3df& pos, irr::video::SColor color, ResistType type) {
        irr::core::vector3df groundPos = pos;
        groundPos.Y -= 2.0f;  // On ground

        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, groundPos);
        if (!ps) return;

        // Ring emitter on ground
        auto* emitter = ps->createRingEmitter(
            irr::core::vector3df(0, 0, 0),
            0.2f, 2.0f,
            irr::core::vector3df(0, 0.1f, 0),          // Rise from ground
            60, 100,
            color, irr::video::SColor(255, 150, 150, 255),
            400, 800,
            10,
            irr::core::dimension2df(1.0f, 1.0f),
            irr::core::dimension2df(2.0f, 2.0f)
        );
        ps->setEmitter(emitter);
        emitter->drop();

        auto* fadeAff = ps->createFadeOutParticleAffector();
        ps->addAffector(fadeAff);
        fadeAff->drop();

        auto* tex = getTextureForResistType(type);
        if (tex) ps->setMaterialTexture(0, tex);
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        m_completionEffects.push_back({ps, 3.0f});
    }

    // CC effect (roots, mez, stun)
    void createCCEffect(const irr::core::vector3df& pos, irr::video::SColor color, ResistType type) {
        auto* ps = m_smgr->addParticleSystemSceneNode(false, nullptr, -1, pos);
        if (!ps) return;

        // Sphere emitter around target
        auto* emitter = ps->createSphereEmitter(
            irr::core::vector3df(0, 0, 0),
            2.0f,                                        // Radius
            irr::core::vector3df(0, 0, 0),              // No direction (orbiting)
            15, 30,                                      // Low rate
            color, color,
            1000, 2000,                                  // Long lifetime
            20,
            irr::core::dimension2df(0.6f, 0.6f),
            irr::core::dimension2df(1.2f, 1.2f)
        );
        ps->setEmitter(emitter);
        emitter->drop();

        // Attraction to center for orbiting effect
        auto* attrAff = ps->createAttractionAffector(pos, 5.0f);
        ps->addAffector(attrAff);
        attrAff->drop();

        auto* fadeAff = ps->createFadeOutParticleAffector();
        ps->addAffector(fadeAff);
        fadeAff->drop();

        auto* tex = getTextureForResistType(type);
        if (tex) ps->setMaterialTexture(0, tex);
        ps->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        ps->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

        m_completionEffects.push_back({ps, 2.5f});
    }

    irr::scene::ISceneManager* m_smgr = nullptr;
    irr::video::IVideoDriver* m_driver = nullptr;
    std::string m_eqClientPath;

    // Textures by resist type
    irr::video::ITexture* m_defaultTexture = nullptr;
    irr::video::ITexture* m_fireTexture = nullptr;
    irr::video::ITexture* m_coldTexture = nullptr;
    irr::video::ITexture* m_magicTexture = nullptr;
    irr::video::ITexture* m_poisonTexture = nullptr;
    irr::video::ITexture* m_diseaseTexture = nullptr;
    irr::video::ITexture* m_chromaticTexture = nullptr;
    irr::video::ITexture* m_corruptionTexture = nullptr;

    // Active effects
    irr::scene::IParticleSystemSceneNode* m_castingEffect = nullptr;
    irr::scene::ISceneNode* m_castingNode = nullptr;
    std::vector<CompletionEffect> m_completionEffects;
    std::vector<std::pair<irr::scene::IParticleSystemSceneNode*, float>> m_pendingRemoval;

    // Hand-emanating particle effects (Phase 4)
    Graphics::EQAnimatedMeshSceneNode* m_animatedNode = nullptr;
    irr::scene::IParticleSystemSceneNode* m_rightHandEffect = nullptr;
    irr::scene::IParticleSystemSceneNode* m_leftHandEffect = nullptr;
    int m_rightHandBoneIndex = -1;
    int m_leftHandBoneIndex = -1;
    irr::video::SColor m_currentSpellColor;
    ResistType m_currentResistType = ResistType::None;

    // Spell effect database (Phase 5 - EDD/EFF parsers)
    SpellEffectDatabase m_spellEffectDB;
};

} // namespace EQT
