#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/environment_config.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/zone_biome.h"
#include "client/graphics/environment/spell_sprite_atlas.h"
#include "client/graphics/environment/emitters/dust_mote_emitter.h"
#include "client/graphics/environment/emitters/pollen_emitter.h"
#include "client/graphics/environment/emitters/firefly_emitter.h"
#include "client/graphics/environment/emitters/mist_emitter.h"
#include "client/graphics/environment/emitters/sand_dust_emitter.h"
#include "client/graphics/environment/emitters/ember_emitter.h"
#include "client/graphics/environment/emitters/smoke_emitter.h"
#ifdef EQT_HAS_GLES2
#include "client/graphics/environment/unified_particle_renderer.h"
#include <GLES2/gl2.h>
#endif
#include "common/logging.h"
#include "client/data_paths.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace EQT {
namespace Graphics {
namespace Environment {

ParticleManager::ParticleManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver)
    : smgr_(smgr)
    , driver_(driver)
{
    // Initialize all types as enabled
    for (size_t i = 0; i < static_cast<size_t>(ParticleType::Count); ++i) {
        typeEnabled_[i] = true;
    }

    // Set default budget based on medium quality
    budget_ = ParticleBudget::fromQuality(EffectQuality::Medium);
}

ParticleManager::~ParticleManager() {
    if (constrainedCache_) {
        constrainedCache_->removeEvictionListener(this);
    }
    clearEmitters();
}

void ParticleManager::setConstrainedTextureCache(ConstrainedTextureCache* cache) {
    // P34: mirrors AnimatedTextureManager::setConstrainedTextureCache — this
    // is where the previously-missing eviction-listener registration was
    // added (see onTextureEvicted()'s doc comment below for the crash this
    // fixes).
    if (constrainedCache_) {
        constrainedCache_->removeEvictionListener(this);
    }
    constrainedCache_ = cache;
    if (constrainedCache_) {
        constrainedCache_->addEvictionListener(this);
    }
}

// P34 — ParticleManager::renderUnified() use-after-free root cause and fix.
//
// Root cause (gdb-confirmed by P33b, root-caused here): atlasTexture_ is
// loaded ONCE, in init() (guarded by initialized_, never re-entered), via
// constrainedCache_->getOrLoad("particle_atlas", ...) /
// uploadTexture("particle_atlas") — an ordinary entry in
// ConstrainedTextureCache's LRU-evicting cache_ map, identical in every
// respect to any zone texture. Unlike EVERY other subsystem that caches a
// raw ITexture* from this cache (AnimatedTextureManager, SkyRenderer,
// DetailManager, StormCloudLayer all self-register as
// TextureEvictionListener; RegionMeshTextureGuard/EntityMeshTextureGuard/
// SingleTexturePointerGuard cover region meshes, entity meshes, and the
// software cursor respectively — see renderer_zone_merge.h),
// ParticleManager was never registered as a listener at all.
//
// The exact freeing sequence: IrrlichtRenderer::unloadZone()
// (renderer_zone_lifecycle.cpp) runs SYNCHRONOUSLY on every zone transition
// (P17) and unconditionally calls constrainedTextureCache_->clear()
// (renderer_zone_lifecycle.cpp:294-295), which — per
// ConstrainedTextureCache::clear() — notifies every registered
// TextureEvictionListener for each cached entry (including
// "particle_atlas") and THEN calls driver_->removeTexture() on it,
// freeing the underlying ITexture. Because ParticleManager was never a
// listener, atlasTexture_ (and the copy cached in particleMaterial_'s
// texture slot 0) were never nulled — they were left pointing at freed
// memory. The next zone's loadZoneSequential() runs
// particleManager_->onZoneEnter() (SEQ Step11) but that never re-loaded
// the atlas either (loadParticleAtlas() was only ever called from init(),
// which is a one-shot). The first subsequent frame that reaches
// renderUnified() calls atlasTexture_->getDriverTextureHandle() — a
// virtual call through the dangling pointer — SIGSEGV. This matches
// P33b's gdb backtrace (fault inside renderUnified(), disassembly showing
// a vtable read through atlasTexture_) and the "~1-2s after zone-in"
// timing (exactly the zone-load + first-render-frame window), and befallen
// -> qeynos2 hits it on literally the very first zone transition of any
// session, unconditionally — not a memory-pressure-dependent LRU eviction
// like most of this bug class, since clear() wipes the ENTIRE cache on
// every zone unload regardless of budget.
//
// Fix (two-layer pattern, matching this bug class's established idiom):
// layer 1 — ParticleManager now implements TextureEvictionListener and
// self-registers/deregisters in setConstrainedTextureCache()/~ParticleManager()
// (the AnimatedTextureManager/SkyRenderer/StormCloudLayer/DetailManager
// self-registering variant, not the scene-graph-scanning
// RegionMeshTextureGuard/EntityMeshTextureGuard variant — ParticleManager
// has no node collection to scan, just the one named cache entry plus one
// SMaterial texture slot, structurally identical to what
// AnimatedTextureManager already does for its own per-name texture
// pointers). onTextureEvicted() nulls atlasTexture_ and clears
// particleMaterial_'s texture slot the instant the pointer goes dangling —
// closing the UAF window entirely, independent of when/whether anything
// re-acquires it. Layer 2 — the existing defensive liveness check in
// renderUnified() (`if (!atlasTexture_) return;`, already present
// pre-P34) is now actually load-bearing instead of a no-op, since
// atlasTexture_ is guaranteed non-dangling whenever it's non-null.
// Re-acquisition (so particles keep working across zones instead of going
// dark after the first eviction) is added to onZoneEnter() — the one
// per-zone hook that already runs unconditionally every zone visit, after
// unloadZone()'s clear() has already run: if atlasTexture_ is null, reload
// it via the same loadParticleAtlas()/kAtlasTexturePath path init() used,
// and refresh particleMaterial_'s texture slot. This is the "clear +
// re-acquire via the existing per-zone hook" half of the plan's mandated
// pattern; it does not invent a new lifetime scheme — loadParticleAtlas()
// already existed and is simply now callable more than once.
void ParticleManager::onTextureEvicted(const std::string& name, irr::video::ITexture* texture) {
    (void)name;
    if (!texture) return;

    // T417: the sprite sheet is a second cached texture with the same
    // dangling-pointer exposure, and the same per-zone re-acquisition.
    if (texture == spellSpriteAtlasTexture_) {
        spellSpriteAtlasTexture_ = nullptr;
        LOG_DEBUG(MOD_GRAPHICS,
                  "ParticleManager: spell sprite atlas evicted, cleared dangling "
                  "reference (will reload on next onZoneEnter)");
    }

    if (texture != atlasTexture_) return;
    atlasTexture_ = nullptr;
    particleMaterial_.setTexture(0, nullptr);
    LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: atlas texture evicted, cleared dangling reference "
                             "(will reload on next onZoneEnter)");
}

bool ParticleManager::init(const std::string& eqClientPath) {
    if (initialized_) {
        return true;
    }

    // T397 / board B221: both literals below are CWD-relative, as ~40 shipped
    // data paths across src/client are (the census is board B235).
    // SpellEffectsConfig::load() now resolves its argument through
    // eqt::ResolveDataPath() and returns false -- loudly -- when the file is
    // missing, so this call works from any working directory.
    // T399 closed B235: EnvironmentEffectsConfig is routed too (T396 has
    // merged, so the weather/ambient files are no longer another unit's).
    EnvironmentEffectsConfig::instance().load(eqt::ResolveDataPath("config/environment_effects.json"));

    if (!SpellEffectsConfig::instance().load(eqt::ResolveDataPath("config/spell_effects.json"))) {
        LOG_ERROR(MOD_GRAPHICS,
                  "ParticleManager: spell effect presets are the built-in fallback table, "
                  "not the shipped config -- spell visuals will differ from the shipped effect");
    }

    // Load particle atlas texture via constrained cache
    std::string atlasPath = eqt::ResolveDataPath(kAtlasTexturePath);
    if (!loadParticleAtlas(atlasPath)) {
        LOG_FATAL(MOD_GRAPHICS, "ParticleManager: Required texture {} not found — run generate_textures first", atlasPath);
        std::exit(1);
    }

    // T417 (board B300): the spell sprite sheet. Unlike the procedural
    // atlas this is NOT fatal if missing -- every procedural particle still
    // draws, and only real-sprite spell effects are lost -- but it is loud,
    // because a silently missing sheet looks exactly like the approximation
    // this unit removed.
    std::string spriteAtlasPath = eqt::ResolveDataPath(kSpellSpriteAtlasPath);
    if (!loadSpellSpriteAtlas(spriteAtlasPath)) {
        LOG_ERROR(MOD_GRAPHICS,
                  "ParticleManager: spell sprite atlas {} not found -- spell "
                  "effects that name a real sprite will not draw. Run "
                  "scripts/build_textures.sh.",
                  spriteAtlasPath);
    }

    // Set up particle material (T400: see configureParticleMaterial()'s
    // doc comment for why this is alpha-blend and what that demands of the
    // atlas's alpha channel).
    configureParticleMaterial(particleMaterial_, atlasTexture_);

    initialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "ParticleManager initialized");
    return true;
}

void ParticleManager::update(float deltaTime) {
    if (!enabled_ || !initialized_ || quality_ == EffectQuality::Off) {
        return;
    }

    // Update all emitters
    for (auto& emitter : emitters_) {
        if (emitter && isTypeEnabled(emitter->getType())) {
            emitter->update(deltaTime, envState_);
        }
    }
}

void ParticleManager::render() {
    if (!enabled_ || !initialized_ || quality_ == EffectQuality::Off || !driver_) {
        return;
    }

    // Get camera info for billboard orientation
    irr::scene::ICameraSceneNode* camera = smgr_->getActiveCamera();
    if (!camera) {
        return;
    }

    irr::core::vector3df cameraPos = camera->getAbsolutePosition();
    irr::core::vector3df cameraUp = camera->getUpVector();

    // Set up rendering state
    driver_->setMaterial(particleMaterial_);
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

    // Render all particles from all emitters
    for (const auto& emitter : emitters_) {
        if (!emitter || !isTypeEnabled(emitter->getType())) {
            continue;
        }

        for (const Particle& p : emitter->getParticles()) {
            if (p.isAlive()) {
                // Distance culling
                glm::vec3 diff = p.position - envState_.playerPosition;
                float distSq = glm::dot(diff, diff);
                if (distSq > budget_.cullDistance * budget_.cullDistance) {
                    continue;
                }

                if (nearClipped(p.position, glm::vec3(cameraPos.X, cameraPos.Y, cameraPos.Z),
                                knobs_[static_cast<size_t>(ParticleFamily::Environment)].nearClip)) {
                    continue;  // B995
                }
                renderBillboard(p, cameraPos, cameraUp);
            }
        }
    }

    // Render external emitters (e.g., weather effects)
    for (const auto* emitter : externalEmitters_) {
        if (!emitter || !emitter->isEnabled()) {
            continue;
        }

        for (const Particle& p : emitter->getParticles()) {
            if (p.isAlive()) {
                // Distance culling
                glm::vec3 diff = p.position - envState_.playerPosition;
                float distSq = glm::dot(diff, diff);
                if (distSq > budget_.cullDistance * budget_.cullDistance) {
                    continue;
                }

                if (nearClipped(p.position, glm::vec3(cameraPos.X, cameraPos.Y, cameraPos.Z),
                                knobs_[static_cast<size_t>(ParticleFamily::Environment)].nearClip)) {
                    continue;  // B995
                }
                renderBillboard(p, cameraPos, cameraUp);
            }
        }
    }
}

void ParticleManager::onZoneEnter(const std::string& zoneName, ZoneBiome biome) {
    LOG_INFO(MOD_GRAPHICS, "ParticleManager: Entering zone '{}' with biome {}",
             zoneName, static_cast<int>(biome));

    // P34: re-acquire the atlas texture if a prior zone unload's
    // ConstrainedTextureCache::clear() evicted it (see onTextureEvicted()'s
    // doc comment above). onZoneEnter() runs on every zone visit, always
    // after unloadZone()'s clear() has already executed (P17 synchronous
    // unload), making this the correct per-zone re-acquisition point.
    if (initialized_ && !atlasTexture_) {
        if (loadParticleAtlas(eqt::ResolveDataPath(kAtlasTexturePath))) {
            particleMaterial_.setTexture(0, atlasTexture_);
            LOG_INFO(MOD_GRAPHICS, "ParticleManager: re-acquired atlas texture for zone '{}'", zoneName);
        } else {
            LOG_ERROR(MOD_GRAPHICS, "ParticleManager: failed to re-acquire atlas texture for zone '{}' "
                                     "— particles will not render this zone visit", zoneName);
        }
    }

    // T417: the same re-acquisition for the sprite sheet.
    if (initialized_ && !spellSpriteAtlasTexture_) {
        if (loadSpellSpriteAtlas(eqt::ResolveDataPath(kSpellSpriteAtlasPath))) {
            LOG_INFO(MOD_GRAPHICS,
                     "ParticleManager: re-acquired spell sprite atlas for zone '{}'",
                     zoneName);
        } else {
            LOG_ERROR(MOD_GRAPHICS,
                      "ParticleManager: failed to re-acquire spell sprite atlas for "
                      "zone '{}' -- real-sprite spell effects will not draw", zoneName);
        }
    }

    currentZoneName_ = zoneName;
    currentBiome_ = biome;

    // Clear existing emitters and set up new ones for this biome
    clearEmitters();
    setupEmittersForBiome(biome);

    // Notify all emitters of zone entry
    for (auto& emitter : emitters_) {
        if (emitter) {
            emitter->onZoneEnter(zoneName, biome);
        }
    }
}

void ParticleManager::onZoneLeave() {
    LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Leaving zone '{}'", currentZoneName_);

    // Notify all emitters
    for (auto& emitter : emitters_) {
        if (emitter) {
            emitter->onZoneLeave();
        }
    }

    // Clear unified fire emitters
    clearUnifiedEmitters();

    currentZoneName_.clear();
    currentBiome_ = ZoneBiome::Unknown;
}

void ParticleManager::setQuality(EffectQuality quality) {
    quality_ = quality;
    budget_ = ParticleBudget::fromQuality(quality);

    LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Quality set to {}, budget={}",
              static_cast<int>(quality), budget_.maxTotal);
}

void ParticleManager::setDensity(float density) {
    userDensity_ = glm::clamp(density, 0.0f, 1.0f);

    // Update all emitters with new density
    float effectiveDensity = userDensity_ * budget_.densityMult * envDensity();  // B995
    for (auto& emitter : emitters_) {
        if (emitter) {
            emitter->setDensityMultiplier(effectiveDensity);
        }
    }
}

void ParticleManager::setTypeEnabled(ParticleType type, bool enabled) {
    if (type < ParticleType::Count) {
        typeEnabled_[static_cast<size_t>(type)] = enabled;

        // Update emitters of this type
        for (auto& emitter : emitters_) {
            if (emitter && emitter->getType() == type) {
                emitter->setEnabled(enabled);
            }
        }
    }
}

bool ParticleManager::isTypeEnabled(ParticleType type) const {
    if (type < ParticleType::Count) {
        return typeEnabled_[static_cast<size_t>(type)];
    }
    return false;
}

void ParticleManager::setTimeOfDay(float hour) {
    envState_.timeOfDay = std::fmod(hour, 24.0f);
    if (envState_.timeOfDay < 0.0f) {
        envState_.timeOfDay += 24.0f;
    }
}

void ParticleManager::setWeather(WeatherType weather) {
    envState_.weather = weather;
}

void ParticleManager::setWind(const glm::vec3& direction, float strength) {
    envState_.windDirection = glm::length(direction) > 0.0f ?
        glm::normalize(direction) : glm::vec3(1.0f, 0.0f, 0.0f);
    envState_.windStrength = glm::clamp(strength, 0.0f, 1.0f);
}

void ParticleManager::setPlayerPosition(const glm::vec3& pos, float heading) {
    envState_.playerPosition = pos;
    envState_.playerHeading = heading;
}

int ParticleManager::getTotalActiveParticles() const {
    int total = 0;
    for (const auto& emitter : emitters_) {
        if (emitter) {
            total += emitter->getActiveCount();
        }
    }
    return total;
}

std::string ParticleManager::getDebugInfo() const {
    std::ostringstream ss;
    ss << "Particles: " << getTotalActiveParticles() << "/" << budget_.maxTotal;
    ss << " | Quality: ";
    switch (quality_) {
        case EffectQuality::Off: ss << "Off"; break;
        case EffectQuality::Low: ss << "Low"; break;
        case EffectQuality::Medium: ss << "Med"; break;
        case EffectQuality::High: ss << "High"; break;
    }
    ss << " | Biome: " << static_cast<int>(currentBiome_);
    ss << " | Time: " << static_cast<int>(envState_.timeOfDay) << ":00";
    return ss.str();
}

void ParticleManager::reloadSettings() {
    // Reload the config file
    if (!EnvironmentEffectsConfig::instance().reload()) {
        LOG_WARN(MOD_GRAPHICS, "ParticleManager: Failed to reload config");
        return;
    }

    // Update all emitters with new settings
    for (auto& emitter : emitters_) {
        if (emitter) {
            emitter->reloadSettings();
        }
    }

    LOG_INFO(MOD_GRAPHICS, "ParticleManager: Reloaded settings for {} emitters", emitters_.size());
}

void ParticleManager::setSurfaceMap(const Detail::SurfaceMap* surfaceMap) {
    surfaceMap_ = surfaceMap;

    // Propagate to all emitters that need terrain data
    for (auto& emitter : emitters_) {
        if (emitter) {
            emitter->setSurfaceMap(surfaceMap);
        }
    }

    if (surfaceMap) {
        LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Set surface map, propagated to {} emitters",
                  emitters_.size());
    }
}

void ParticleManager::registerExternalEmitter(ParticleEmitter* emitter) {
    if (!emitter) return;

    // Check if already registered
    auto it = std::find(externalEmitters_.begin(), externalEmitters_.end(), emitter);
    if (it == externalEmitters_.end()) {
        externalEmitters_.push_back(emitter);
        LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Registered external emitter");
    }
}

void ParticleManager::unregisterExternalEmitter(ParticleEmitter* emitter) {
    if (!emitter) return;

    auto it = std::find(externalEmitters_.begin(), externalEmitters_.end(), emitter);
    if (it != externalEmitters_.end()) {
        externalEmitters_.erase(it);
        LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Unregistered external emitter");
    }
}

void ParticleManager::setFireSources(const std::vector<glm::vec3>& positions) {
    for (auto& emitter : emitters_) {
        if (emitter->getType() == ParticleType::Ember) {
            static_cast<EmberEmitter*>(emitter.get())->setFireSources(positions);
        } else if (emitter->getType() == ParticleType::Smoke) {
            static_cast<SmokeEmitter*>(emitter.get())->setFireSources(positions);
        }
    }
    LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Set {} fire sources for ember/smoke emitters", positions.size());
}

void ParticleManager::setupEmittersForBiome(ZoneBiome biome) {
    LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Setting up emitters for biome {}",
              static_cast<int>(biome));

    // Calculate effective density
    float effectiveDensity = userDensity_ * budget_.densityMult * envDensity();  // B995

    // Check zone properties for water, etc.
    const auto& detector = ZoneBiomeDetector::instance();
    bool zoneHasWater = detector.hasWater(currentZoneName_);

    // Helper to add an emitter with common setup
    auto addEmitter = [&](std::unique_ptr<ParticleEmitter> emitter) {
        if (emitter) {
            emitter->setDensityMultiplier(effectiveDensity);
            emitter->setEnabled(isTypeEnabled(emitter->getType()));
            emitters_.push_back(std::move(emitter));
        }
    };

    // Set up emitters based on biome
    switch (biome) {
        case ZoneBiome::Forest:
            // Forests: pollen (day), fireflies (night), dust motes
            addEmitter(std::make_unique<PollenEmitter>());
            addEmitter(std::make_unique<FireflyEmitter>());
            addEmitter(std::make_unique<DustMoteEmitter>());
            if (zoneHasWater) {
                addEmitter(std::make_unique<MistEmitter>());
            }
            break;

        case ZoneBiome::Swamp:
            // Swamps: heavy mist, fireflies, pollen
            addEmitter(std::make_unique<MistEmitter>());
            addEmitter(std::make_unique<FireflyEmitter>());
            addEmitter(std::make_unique<PollenEmitter>());
            break;

        case ZoneBiome::Desert:
            // Deserts: blowing sand, dust motes
            addEmitter(std::make_unique<SandDustEmitter>());
            addEmitter(std::make_unique<DustMoteEmitter>());
            break;

        case ZoneBiome::Plains:
            // Plains: pollen, dust motes, occasional mist near water
            addEmitter(std::make_unique<PollenEmitter>());
            addEmitter(std::make_unique<DustMoteEmitter>());
            if (zoneHasWater) {
                addEmitter(std::make_unique<MistEmitter>());
                addEmitter(std::make_unique<FireflyEmitter>());
            }
            break;

        case ZoneBiome::Ocean:
            // Ocean/coastal: mist, dust motes
            addEmitter(std::make_unique<MistEmitter>());
            addEmitter(std::make_unique<DustMoteEmitter>());
            break;

        case ZoneBiome::Dungeon:
            // Dungeons: dust motes (primary atmospheric effect)
            addEmitter(std::make_unique<DustMoteEmitter>());
            break;

        case ZoneBiome::Cave:
            // Caves: dust motes, mist in some areas
            addEmitter(std::make_unique<DustMoteEmitter>());
            addEmitter(std::make_unique<MistEmitter>());
            break;

        case ZoneBiome::Urban:
            // Cities: dust motes
            addEmitter(std::make_unique<DustMoteEmitter>());
            break;

        case ZoneBiome::Snow:
            // Snow zones: dust motes for now (snowflake emitter future)
            addEmitter(std::make_unique<DustMoteEmitter>());
            break;

        case ZoneBiome::Volcanic:
            // Volcanic zones: dust motes (ember emitter future)
            addEmitter(std::make_unique<DustMoteEmitter>());
            break;

        default:
            // Unknown biome: just dust motes
            addEmitter(std::make_unique<DustMoteEmitter>());
            break;
    }

    // Fire emitters (embers + smoke) - added to all biomes, activated when fire sources are set
    addEmitter(std::make_unique<EmberEmitter>());
    addEmitter(std::make_unique<SmokeEmitter>());

    LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Created {} emitters", emitters_.size());
}

void ParticleManager::configureParticleMaterial(irr::video::SMaterial& material,
                                                irr::video::ITexture* atlasTexture) {
    material.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL;
    material.setTexture(0, atlasTexture);
    material.Lighting = false;
    material.ZWriteEnable = false;
    material.BackfaceCulling = false;
    material.FogEnable = false;
}

void ParticleManager::setEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;

    if (!enabled) {
        clearEmitters();
        LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Disabled, emitters released");
    } else if (!currentZoneName_.empty()) {
        setupEmittersForBiome(currentBiome_);
        for (auto& emitter : emitters_) {
            if (emitter) emitter->onZoneEnter(currentZoneName_, currentBiome_);
        }
        LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Re-enabled, {} emitters recreated", emitters_.size());
    }
}

void ParticleManager::clearEmitters() {
    emitters_.clear();
}

bool ParticleManager::loadParticleAtlas(const std::string& path) {
    if (!driver_ || !constrainedCache_) return false;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> data(fileSize);
    if (!file.read(data.data(), fileSize)) return false;

    const TextureOrigin origin = TextureOrigin::plainFile(path);  // B947: restore origin
    atlasTexture_ = constrainedCache_->getOrLoad("particle_atlas", data, "uncategorized", "", false, &origin);
    if (!atlasTexture_) atlasTexture_ = constrainedCache_->uploadTexture("particle_atlas");
    return atlasTexture_ != nullptr;
}

// T417 (board B300). Same shape as loadParticleAtlas() above, one cache key
// down: "spell_sprite_atlas" is a pack entry like any other, so it arrives
// ETC1 with its alpha in an `_alpha` companion and the point-sprite shader
// samples that on unit 1 (T411).
bool ParticleManager::loadSpellSpriteAtlas(const std::string& path) {
    if (!driver_ || !constrainedCache_) return false;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> data(fileSize);
    if (!file.read(data.data(), fileSize)) return false;

    const TextureOrigin origin = TextureOrigin::plainFile(path);  // B947: restore origin
    spellSpriteAtlasTexture_ =
        constrainedCache_->getOrLoad("spell_sprite_atlas", data, "uncategorized", "", false, &origin);
    if (!spellSpriteAtlasTexture_) {
        spellSpriteAtlasTexture_ =
            constrainedCache_->uploadTexture("spell_sprite_atlas");
    }
    return spellSpriteAtlasTexture_ != nullptr;
}

void ParticleManager::renderBillboard(const Particle& p,
                                       const irr::core::vector3df& cameraPos,
                                       const irr::core::vector3df& cameraUp)
{
    // Convert particle position to Irrlicht coordinates (EQ uses Z-up, Irrlicht uses Y-up)
    irr::core::vector3df pos(p.position.x, p.position.z, p.position.y);

    irr::core::vector3df right, up;
    irr::core::vector3df toCamera = cameraPos - pos;
    toCamera.normalize();

    if (p.velocityAligned && glm::length(p.velocity) > 0.1f) {
        // Velocity-aligned billboard (for rain streaks, etc.)
        // Convert velocity to Irrlicht coordinates
        irr::core::vector3df vel(p.velocity.x, p.velocity.z, p.velocity.y);
        vel.normalize();

        // Up direction is the velocity direction (particles fall downward)
        up = vel;

        // Right is perpendicular to both velocity and camera direction
        right = up.crossProduct(toCamera);
        right.normalize();

        // Ensure right is perpendicular
        if (right.getLength() < 0.001f) {
            // Velocity is pointing at camera, use world up
            right = irr::core::vector3df(1, 0, 0);
        }
    } else {
        // Standard camera-facing billboard
        right = cameraUp.crossProduct(toCamera);
        right.normalize();

        up = toCamera.crossProduct(right);
        up.normalize();

        // Apply rotation
        if (std::abs(p.rotation) > 0.001f) {
            float cosR = std::cos(p.rotation);
            float sinR = std::sin(p.rotation);
            irr::core::vector3df newRight = right * cosR + up * sinR;
            irr::core::vector3df newUp = up * cosR - right * sinR;
            right = newRight;
            up = newUp;
        }
    }

    // Calculate quad vertices with stretch support
    float halfWidth = p.size * 0.5f;
    float halfHeight = p.size * p.stretch * 0.5f;
    irr::core::vector3df v0 = pos + (-right * halfWidth + up * halfHeight);
    irr::core::vector3df v1 = pos + (right * halfWidth + up * halfHeight);
    irr::core::vector3df v2 = pos + (right * halfWidth - up * halfHeight);
    irr::core::vector3df v3 = pos + (-right * halfWidth - up * halfHeight);

    // Get UV coordinates for this particle's texture tile
    float u0, v0Uv, u1, v1Uv;
    getAtlasUVs(p.textureIndex, u0, v0Uv, u1, v1Uv);

    // Calculate final color with alpha
    // B995: the Env page's opacity (1.0 unless the player lowered it).
    const float optionOpacity = knobs_[static_cast<size_t>(ParticleFamily::Environment)].opacity;
    irr::u32 alpha = static_cast<irr::u32>(p.alpha * p.color.a * optionOpacity * 255.0f);
    irr::u32 r = static_cast<irr::u32>(p.color.r * 255.0f);
    irr::u32 g = static_cast<irr::u32>(p.color.g * 255.0f);
    irr::u32 b = static_cast<irr::u32>(p.color.b * 255.0f);
    irr::video::SColor color(alpha, r, g, b);

    // Create vertices
    irr::video::S3DVertex vertices[4];
    vertices[0] = irr::video::S3DVertex(v0, toCamera, color, irr::core::vector2df(u0, v0Uv));
    vertices[1] = irr::video::S3DVertex(v1, toCamera, color, irr::core::vector2df(u1, v0Uv));
    vertices[2] = irr::video::S3DVertex(v2, toCamera, color, irr::core::vector2df(u1, v1Uv));
    vertices[3] = irr::video::S3DVertex(v3, toCamera, color, irr::core::vector2df(u0, v1Uv));

    // Create indices for two triangles
    irr::u16 indices[6] = {0, 1, 2, 0, 2, 3};

    // Draw the quad
    driver_->drawVertexPrimitiveList(vertices, 4, indices, 2,
                                      irr::video::EVT_STANDARD,
                                      irr::scene::EPT_TRIANGLES,
                                      irr::video::EIT_16BIT);
}

void ParticleManager::getAtlasUVs(uint8_t tileIndex, float& u0, float& v0, float& u1, float& v1) const {
    // T417 (board B300): this is the LEGACY CPU billboard path, whose
    // material is bound to the procedural atlas alone -- it has no way to
    // reach the spell sprite sheet. Only the billboard EMITTERS (weather,
    // fire, dust) come through here, and none of them names a spells.eff
    // sprite, so a sprite index arriving is a wiring mistake rather than a
    // drawing decision. Return tile 0 rather than `index % 4`, which would
    // silently paint an unrelated procedural tile.
    if (tileIndex >= SpellSpriteAtlas::kBase) {
        tileIndex = ParticleAtlas::SoftCircle;
    }

    // Calculate tile position in atlas
    int col = tileIndex % ParticleAtlas::AtlasColumns;
    int row = tileIndex / ParticleAtlas::AtlasColumns;

    float tileWidth = 1.0f / ParticleAtlas::AtlasColumns;
    float tileHeight = 1.0f / ParticleAtlas::AtlasRows;

    u0 = col * tileWidth;
    v0 = row * tileHeight;
    u1 = u0 + tileWidth;
    v1 = v0 + tileHeight;
}

// =============================================================================
// Unified Particle System (GLES2 point sprites)
// =============================================================================

bool ParticleManager::initUnifiedRenderer(int poolSize) {
#ifdef EQT_HAS_GLES2
    if (unifiedRendererInitialized_) return true;

    // Pool is now owned by SimulationWorker — just init the GL renderer here.

    // Create and init the GLES2 renderer (self-contained, uses raw GL calls)
    unifiedRenderer_ = std::make_unique<UnifiedParticleRenderer>();
    // B972: the driver's GL context, so the renderer's bounded VBO can enter
    // the driver's logical buffer tally (gles2AdjustRawBufferBytes).
    if (!unifiedRenderer_->init(driver_)) {
        LOG_WARN(MOD_GRAPHICS, "ParticleManager: Failed to init unified particle renderer");
        unifiedRenderer_.reset();
        return false;
    }

    unifiedRendererInitialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "ParticleManager: Unified particle renderer initialized (pool owned by worker)");
    return true;
#else
    return false;
#endif
}

void ParticleManager::updateUnified(float deltaTime, const glm::vec3& cameraPos) {
    // Physics moved to SimulationWorker — nothing to do here.
    // Billboard emitters still updated via update() separately.
    (void)deltaTime;
    (void)cameraPos;
}

void ParticleManager::renderUnified(const irr::core::matrix4& viewMatrix,
                                     const irr::core::matrix4& projMatrix,
                                     const irr::core::vector3df& cameraPos,
                                     float fogStart, float fogEnd, const float* fogColor,
                                     float screenHeight,
                                     const std::vector<UnifiedParticle>* externalRenderBuffer) {
#ifdef EQT_HAS_GLES2
    if (!unifiedRendererInitialized_ || !unifiedRenderer_) return;
    if (!atlasTexture_) return;

    // Use external render buffer from SimulationWorker output
    if (!externalRenderBuffer || externalRenderBuffer->empty()) return;

    // Get GL texture handle via the patched ITexture::getDriverTextureHandle()
    GLuint atlasGL = static_cast<GLuint>(atlasTexture_->getDriverTextureHandle());
    if (atlasGL == 0) return;

    // T411 (board B261): the atlas's ETC1 `_alpha` companion, if the pack
    // shipped one. 0 when the payload carries alpha in band (RGBA8888) or the
    // atlas came from a raw BMP decode -- the shader then samples a 1x1 white
    // texture and the multiply is a no-op. This renderer talks raw GL, so it
    // cannot go through the driver's material path that binds unit 1.
    GLuint alphaGL = 0;
    if (constrainedCache_) {
        if (irr::video::ITexture* alphaTex = constrainedCache_->getAlphaTexture(atlasTexture_)) {
            alphaGL = static_cast<GLuint>(alphaTex->getDriverTextureHandle());
        }
    }

    // T417 (board B300): the spell sprite sheet and its own companion. 0
    // when the sheet failed to load -- the renderer then SKIPS sprite-sheet
    // particles rather than drawing them with the procedural atlas's UVs.
    GLuint spriteGL = 0;
    GLuint spriteAlphaGL = 0;
    if (spellSpriteAtlasTexture_) {
        spriteGL = static_cast<GLuint>(spellSpriteAtlasTexture_->getDriverTextureHandle());
        if (constrainedCache_) {
            if (irr::video::ITexture* a =
                    constrainedCache_->getAlphaTexture(spellSpriteAtlasTexture_)) {
                spriteAlphaGL = static_cast<GLuint>(a->getDriverTextureHandle());
            }
        }
    }

    // Periodic debug logging (every ~5 seconds at 50fps = every 250 frames)
    static int frameCounter = 0;
    if (++frameCounter >= 250) {
        frameCounter = 0;
        const auto& firstParticle = (*externalRenderBuffer)[0];
        LOG_DEBUG(MOD_GRAPHICS,
                  "UnifiedParticles: rendering {} alive, atlasGL={}, "
                  "first pos=({:.1f},{:.1f},{:.1f}) size={:.2f} color=({:.2f},{:.2f},{:.2f},{:.2f})",
                  externalRenderBuffer->size(), atlasGL,
                  firstParticle.position.x, firstParticle.position.y, firstParticle.position.z,
                  firstParticle.size,
                  firstParticle.color.r, firstParticle.color.g, firstParticle.color.b, firstParticle.color.a);
    }

    // Pass View and Projection separately — shader multiplies in GLSL
    // (same convention as built-in COGLES2 shaders)
    unifiedRenderer_->setParticleKnobs(knobs_, glm::vec3(cameraPos.X, cameraPos.Y, cameraPos.Z));  // B995
    unifiedRenderer_->render(externalRenderBuffer->data(),
                             static_cast<int>(externalRenderBuffer->size()),
                             viewMatrix.pointer(), projMatrix.pointer(),
                             fogStart, fogEnd, fogColor,
                             screenHeight, atlasGL, alphaGL, spriteGL, spriteAlphaGL);
#endif
    (void)cameraPos;
}

// B995: the Options pages' knobs. Density reaches the ambient billboard
// emitters through the same multiplier setDensity() feeds (never above what
// the preset gives); opacity and near clip are applied per particle at draw.
void ParticleManager::setParticleKnobs(const ParticleKnobs& knobs) {
    const float before = envDensity();
    knobs_ = knobs;
    if (envDensity() == before) return;
    const float effectiveDensity = userDensity_ * budget_.densityMult * envDensity();
    for (auto& emitter : emitters_) {
        if (emitter) emitter->setDensityMultiplier(effectiveDensity);
    }
}

float ParticleManager::envDensity() const {
    return glm::clamp(knobs_[static_cast<size_t>(ParticleFamily::Environment)].density, 0.0f, 1.0f);
}

void ParticleManager::createFireEmitters(const std::vector<glm::vec3>& positions,
                                          const std::vector<float>& lightRadii) {
    if (positions.empty()) return;

    ParticleCommandData cmd;
    cmd.type = ParticleCommand::CreateFireEmitters;
    cmd.firePositions = positions;
    cmd.fireRadii = lightRadii;
    pendingCommands_.push_back(std::move(cmd));

    LOG_INFO(MOD_GRAPHICS, "ParticleManager: Enqueued CreateFireEmitters for {} sources", positions.size());
}

void ParticleManager::clearUnifiedEmitters() {
    ParticleCommandData cmd;
    cmd.type = ParticleCommand::ClearUnifiedEmitters;
    pendingCommands_.push_back(std::move(cmd));
    weatherActive_ = false;
}

void ParticleManager::activateWeatherParticles(uint8_t type, uint8_t intensity) {
    ParticleCommandData cmd;
    cmd.type = ParticleCommand::ActivateWeather;
    cmd.weatherType = type;
    cmd.weatherIntensity = intensity;
    pendingCommands_.push_back(std::move(cmd));
    weatherActive_ = true;

    LOG_INFO(MOD_GRAPHICS, "ParticleManager: Enqueued ActivateWeather type={} intensity={}", type, intensity);
}

void ParticleManager::deactivateWeatherParticles() {
    if (!weatherActive_) return;

    ParticleCommandData cmd;
    cmd.type = ParticleCommand::DeactivateWeather;
    pendingCommands_.push_back(std::move(cmd));
    weatherActive_ = false;

    LOG_INFO(MOD_GRAPHICS, "ParticleManager: Enqueued DeactivateWeather");
}

// spawnWeatherParticle moved to SimulationWorker

bool ParticleManager::resolveEntityPosition(uint16_t entityID, glm::vec3& outPos) const {
    const auto anchor = fxAnchors_.find(entityID);
    if (anchor != fxAnchors_.end()) {
        outPos = anchor->second;
        return true;
    }
    if (entityPosCallback_) return entityPosCallback_(entityID, outPos);
    return false;
}

// B293 (#124): the direction half of the same seam. The renderer's
// applySimulationOutput() direction arm calls this once per entity the
// worker's directionRequestEntities named, exactly as the position arm does
// above for resolveEntityPosition().
bool ParticleManager::resolveEntityDirection(uint16_t entityID, glm::vec3& outDir) const {
    if (entityDirCallback_) return entityDirCallback_(entityID, outDir);
    return false;
}

// =============================================================================
// Spell Effect API — thin facade, enqueues commands for SimulationWorker
// =============================================================================

uint32_t ParticleManager::createSpellEffect(const SpellEffectDef& def,
                                             uint16_t casterID, uint16_t targetID,
                                             float duration,
                                             bool useDynamicDir,
                                             float projectileTravelDuration) {
    if (!unifiedRendererInitialized_) return 0;

    // Pre-assign ID from local counter so we can return it immediately
    uint32_t effectID = nextLocalSpellEffectID_++;

    ParticleCommandData cmd;
    cmd.type = ParticleCommand::CreateSpellEffect;
    cmd.spellDef = def;
    cmd.casterID = casterID;
    cmd.targetID = targetID;
    cmd.duration = duration;
    cmd.useDynamicDir = useDynamicDir;
    cmd.projectileTravelDuration = projectileTravelDuration;
    cmd.preAssignedEffectID = effectID;
    pendingCommands_.push_back(std::move(cmd));

    LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Enqueued CreateSpellEffect '{}' id={} caster={} target={}",
              def.name, effectID, casterID, targetID);

    return effectID;
}

// T397 Leg B, board B220: removeSpellEffect(uint32_t) was deleted from here.
// See particle_manager.h for why.
//
// The same wrong-field bug lived in the survivor below, and there it was NOT
// harmless. It wrote `targetID`, leaving `entityID` at its default 0, and the
// worker's RemoveSpellEffectsEntity arm matches
// `casterEntityID == cmd.entityID || targetEntityID == cmd.entityID`. Every
// cast glow is created as createSpellEffect(def, caster, 0) and every impact
// as createSpellEffect(def, 0, target), so 0 matched ALL of them: any call --
// and SpellVisualFX::createCastGlow() makes three before every single cast --
// behaved as clearAllSpellEffects() while removing nothing it was asked to.
void ParticleManager::removeSpellEffectsForEntity(uint16_t entityID) {
    removeSpellEffectsForEntity(entityID, {});
}

void ParticleManager::removeSpellEffectsForEntity(uint16_t entityID,
                                                  std::vector<std::string> styles) {
    ParticleCommandData cmd;
    cmd.type = ParticleCommand::RemoveSpellEffectsEntity;
    cmd.entityID = entityID;
    cmd.styleFilter = std::move(styles);
    pendingCommands_.push_back(std::move(cmd));
}

void ParticleManager::releaseSpellEffectsForEntity(uint16_t entityID,
                                                   std::vector<std::string> styles,
                                                   float seconds) {
    ParticleCommandData cmd;
    cmd.type = ParticleCommand::ReleaseSpellEffectsEntity;
    cmd.entityID = entityID;
    cmd.styleFilter = std::move(styles);
    cmd.duration = seconds;
    pendingCommands_.push_back(std::move(cmd));
}

void ParticleManager::clearAllSpellEffects() {
    ParticleCommandData cmd;
    cmd.type = ParticleCommand::ClearAllSpellEffects;
    pendingCommands_.push_back(std::move(cmd));
}

void ParticleManager::toggleUnifiedFire() {
    unifiedFireEnabled_ = !unifiedFireEnabled_;

    ParticleCommandData cmd;
    cmd.type = ParticleCommand::ToggleFire;
    pendingCommands_.push_back(std::move(cmd));

    LOG_INFO(MOD_GRAPHICS, "ParticleManager: Enqueued ToggleFire (local state now {})",
             unifiedFireEnabled_ ? "ON" : "OFF");
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
