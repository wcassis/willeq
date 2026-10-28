#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/spell_fx_bone_anchor.h"  // B1138: hand/head anchors, shared with model_viewer
#include "client/spell/spell_fx_attach_scale.h"  // B1164: the attach scale s
#include "client/graphics/light_source.h"
#include "client/graphics/renderer_zone_merge.h"  // ForwardingTextureGuard (T124)
#include "client/graphics/ui/character_model_view.h"  // T294: markDirty() on popup open
#include "client/graphics/ui/inventory_layout.h"  // T393: the purse denominations
#include "common/logging.h"

#ifdef EQT_HAS_GLES2
#include <glm/glm.hpp>
#endif

namespace EQT {
namespace Graphics {

// --- Entity façade (forwards to entityRenderer_) ---

bool IrrlichtRenderer::createEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                                     float x, float y, float z, float heading, bool isPlayer,
                                     uint8_t gender, const EntityAppearance& appearance, bool isNPC,
                                     bool isCorpse, float serverSize) {
    if (!entityRenderer_) {
        return false;
    }
    bool result = entityRenderer_->createEntity(spawnId, raceId, name, x, y, z, heading, isPlayer, gender, appearance, isNPC, isCorpse, serverSize);

    // If this is the player entity, handle visibility based on current mode
    if (result && isPlayer) {
        // Set player race for vision-based lighting
        playerCamera_->setPlayerRace(raceId);

        // T300: re-apply first-person bookkeeping (name tag) if the player
        // entity is (re)created while already in FirstPerson camera mode
        // (e.g. zoning in with first person already selected). The model
        // itself draws normally; the near plane hides it.
        bool firstPerson = (PlayerCamera().cameraMode() == CameraMode::FirstPerson);
        entityRenderer_->setPlayerFirstPersonMode(firstPerson);
        if (firstPerson) {
            playerCamera_->repositionFirstPersonCamera();
        }

        // Player entity creation is the final step - mark zone as ready
        // This happens after all network packets are processed and other entities are loaded
        if (ZoneLifecycle().networkReady()) {
            setLoadingProgress(1.0f, L"Zone ready!");
            zoneReady_ = true;
            LOG_INFO(MOD_GRAPHICS, "Zone ready - player entity created and camera initialized");
        }
    }

    // Track entity loading for loading screen progress
    if (result) {
        ZoneLifecycle().loadedEntityCount()++;
        LOG_TRACE(MOD_GRAPHICS, "Entity created: {} (ID: {}), loaded count: {}", name, spawnId, ZoneLifecycle().loadedEntityCount());
    }

    return result;
}

bool IrrlichtRenderer::registerEntity(uint16_t spawnId, uint16_t raceId, const std::string& name,
                                       float x, float y, float z, float heading, bool isPlayer,
                                       uint8_t gender, const EntityAppearance& appearance, bool isNPC,
                                       bool isCorpse, float serverSize, uint8_t entityLevel) {
    if (isLoading()) return false;
    if (!entityRenderer_) {
        return false;
    }
    bool result = entityRenderer_->registerEntity(spawnId, raceId, name, x, y, z, heading, isPlayer, gender, appearance, isNPC, isCorpse, serverSize, entityLevel);

    // T348 (board B056): the RACIAL VISION FLOOR, on the path that actually
    // runs. createEntity() above sets it too, but createEntity() has ZERO
    // production callers -- grepped tree-wide in T348: the live zone load
    // registers every entity through registerEntity() (application_zone_loading.cpp,
    // twice) and post-load spawns come through the EntitySpawned bridge arm
    // (bridge/irrlicht_bridge.cpp), both of which land here. That is why
    // "Player race N -> base vision" appears in none of T345's five live
    // session logs although the player entity is created in every one, and
    // why a Dark Elf saw exactly as well in the dark as a Human.
    if (result && isPlayer && playerCamera_) {
        playerCamera_->setPlayerRace(raceId);
    }

    if (result) {
        ZoneLifecycle().loadedEntityCount()++;
        // Re-enable progressive loading so the multi-frame pipeline picks up
        // this entity (progressiveLoadingActive_ may have been set to false
        // after initial zone load completed all entity builds)
        if (!progressiveLoadingActive_ && entityPrepReady_) {
            progressiveLoadingActive_ = true;
            LOG_DEBUG(MOD_GRAPHICS, "Progressive loading re-enabled for post-load entity: {} ({})",
                      name, spawnId);
        }
    }

    return result;
}

void IrrlichtRenderer::updateEntity(uint16_t spawnId, float x, float y, float z, float heading,
                                     float dx, float dy, float dz, uint32_t animation) {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->updateEntity(spawnId, x, y, z, heading, dx, dy, dz, animation);
    }
}

void IrrlichtRenderer::removeEntity(uint16_t spawnId) {
    if (isLoading()) return;
    // B1145 (#465): OP_DeleteSpawn deletes the spawn's actor and with it every
    // spell emitter attached to it -- 3.5 s later for a corpse (the client's
    // corpse-fade list; SpellVisualFX::onSpawnDeleted).
    if (auto& fx = SimBridge().spellVisualFX(); fx && entityRenderer_) {
        const auto& ents = entityRenderer_->getEntities();
        const auto it = ents.find(spawnId);
        const bool corpse = it != ents.end() && it->second.isCorpse;
        fx->onSpawnDeleted(spawnId, corpse, it != ents.end() ? it->second.raceId : 0);
    }
    // B1148 (#468): the model too -- a corpse (race not 216/472) goes on the
    // client's corpse-fade list (alpha from 1.3 s, removed past 3499 ms,
    // corpse_fade.h); every other spawn's model goes at the packet.
    if (entityRenderer_ && !entityRenderer_->beginCorpseFade(spawnId)) {
        entityRenderer_->removeEntity(spawnId);
    }
}

void IrrlichtRenderer::startCorpseDecay(uint16_t spawnId) {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->startCorpseDecay(spawnId);
    }
}

void IrrlichtRenderer::setEntitySize(uint16_t spawnId, float serverSize) {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->setEntitySize(spawnId, serverSize);
    }
}

void IrrlichtRenderer::setEntityLight(uint16_t spawnId, uint8_t lightLevel) {
    if (isLoading()) return;
    // Handle player light specially - it's always highest priority in light pool
    if (spawnId == PlayerCamera().playerSpawnId() && PlayerCamera().playerSpawnId() != 0) {
        SimBridge().playerLightLevel() = lightLevel;

        if (lightLevel == 0) {
            // Remove player light
            if (Diagnostics().playerLightNode()) {
                Diagnostics().playerLightNode()->remove();
                Diagnostics().playerLightNode() = nullptr;
                LOG_DEBUG(MOD_GRAPHICS, "Removed player light");
            }
            return;
        }

        // Calculate light properties based on level
        // Server sends light TYPE (0-15), convert to level (0-10) for intensity
        uint8_t level = lightsource::TypeToLevel(lightLevel);
        float intensity = level / 10.0f;  // 0-10 scale to 0.0-1.0
        float radius = 20.0f + (level / 10.0f) * 80.0f;  // 20-100 range

        // Warm light color (slightly yellow/orange like torchlight)
        float r = std::min(1.0f, 0.9f + intensity * 0.1f);
        float g = std::min(1.0f, 0.7f + intensity * 0.2f);
        float b = std::min(1.0f, 0.4f + intensity * 0.2f);

        // Position at player location (Irrlicht Y-up coordinates)
        irr::core::vector3df lightPos(playerX_, playerZ_ + 3.0f, playerY_);

        if (Diagnostics().playerLightNode()) {
            // Update existing light
            irr::video::SLight& lightData = Diagnostics().playerLightNode()->getLightData();
            lightData.DiffuseColor = irr::video::SColorf(r * intensity, g * intensity, b * intensity, 1.0f);
            lightData.Radius = radius;
            float quad = 19.0f / (radius * radius);
            lightData.Attenuation = irr::core::vector3df(1.0f, 0.0f, quad);
            Diagnostics().playerLightNode()->setPosition(lightPos);
        } else if (smgr_) {
            // Create new light
            Diagnostics().playerLightNode() = smgr_->addLightSceneNode(
                nullptr,
                lightPos,
                irr::video::SColorf(r * intensity, g * intensity, b * intensity, 1.0f),
                radius
            );

            if (Diagnostics().playerLightNode()) {
                irr::video::SLight& lightData = Diagnostics().playerLightNode()->getLightData();
                lightData.Type = irr::video::ELT_POINT;
                // Attenuation: 1/(constant + quadratic*d²)
                // Tuned so attenuation ≈ 5% at the light's radius, enabling an
                // efficient distance-based early-out in the per-pixel FS.
                // quad = 19/(R²) gives 1/(1+19)=5% at d=R, 1/(1+76)=1.3% at d=2R.
                float quad = 19.0f / (radius * radius);
                lightData.Attenuation = irr::core::vector3df(1.0f, 0.0f, quad);
                // Start hidden - updateObjectLights will enable it
                Diagnostics().playerLightNode()->setVisible(false);
                LOG_INFO(MOD_GRAPHICS, "Created player light: level={}, radius={:.1f}", lightLevel, radius);
            }
        }
        return;
    }

    // Non-player entity lights
    if (entityRenderer_) {
        entityRenderer_->setEntityLight(spawnId, lightLevel);
    }
}

void IrrlichtRenderer::clearEntities() {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->clearEntities();
    }
}

// --- Entity façade (animation forwards to entityRenderer_) ---

void IrrlichtRenderer::playEntityDeathAnimation(uint16_t spawnId) {
    if (isLoading()) return;
    if (entityRenderer_) {
        // Mark entity as corpse and play death animation
        entityRenderer_->markEntityAsCorpse(spawnId);
        LOG_DEBUG(MOD_ENTITY, "Entity {} marked as corpse with death animation", spawnId);
    }
}

bool IrrlichtRenderer::setEntityAnimation(uint16_t spawnId, const std::string& animCode, bool loop, bool playThrough) {
    if (isLoading()) return false;
    if (entityRenderer_) {
        return entityRenderer_->setEntityAnimation(spawnId, animCode, loop, playThrough);
    }
    return false;
}

void IrrlichtRenderer::setEntityPoseState(uint16_t spawnId, EntityPoseState pose) {
    if (isLoading()) return;
    if (entityRenderer_) {
        // Convert from IrrlichtRenderer::EntityPoseState to EntityVisual::PoseState
        EntityVisual::PoseState internalPose;
        switch (pose) {
            case EntityPoseState::Sitting:   internalPose = EntityVisual::PoseState::Sitting; break;
            case EntityPoseState::Crouching: internalPose = EntityVisual::PoseState::Crouching; break;
            case EntityPoseState::Lying:     internalPose = EntityVisual::PoseState::Lying; break;
            default:                         internalPose = EntityVisual::PoseState::Standing; break;
        }
        entityRenderer_->setEntityPoseState(spawnId, internalPose);
    }
}

// --- Inventory UI Methods ---

void IrrlichtRenderer::initDeferredGraphics() {
    if (isLoading()) return;

    // WindowManager removed — apply initial render distance from cached settings
    {
        const auto& settings = cachedDisplaySettings_;
        setRenderDistance(settings.renderDistance);
        LOG_INFO(MOD_GRAPHICS, "Initial render distance set to {} from cached settings", settings.renderDistance);
    }

    // Create spell visual effects if not already created
    if (!SimBridge().spellVisualFX() && smgr_ && driver_) {
        SimBridge().spellVisualFX() = std::make_unique<EQ::SpellVisualFX>(smgr_, driver_, config_.eqClientPath);
        SimBridge().spellVisualFX()->setConstrainedTextureCache(constrainedTextureCache_);

        // Set up entity position callback for spell effects
        SimBridge().spellVisualFX()->setEntityPositionCallback(
            [this](uint16_t entity_id, irr::core::vector3df& out_pos) -> bool {
                // Hand bone pseudo-entity IDs (bit 15 set)
                if (EQ::SpellVisualFX::isHandBoneId(entity_id)) {
                    uint16_t realId = EQ::SpellVisualFX::realEntityId(entity_id);
                    bool rightHand = EQ::SpellVisualFX::isRightHand(entity_id);
                    if (!entityRenderer_) return false;
                    const auto& entities = entityRenderer_->getEntities();
                    auto it = entities.find(realId);
                    if (it == entities.end() || !it->second.isAnimated || !it->second.animatedNode) return false;
                    int boneIdx = rightHand
                        ? it->second.animatedNode->findRightHandBoneIndex()
                        : it->second.animatedNode->findLeftHandBoneIndex();
                    if (boneIdx < 0) return false;
                    return it->second.animatedNode->getBoneWorldPosition(boneIdx, out_pos);
                }
                // Normal entity lookup
                if (!entityRenderer_) return false;
                const auto& entities = entityRenderer_->getEntities();
                auto it = entities.find(entity_id);
                if (it == entities.end()) return false;

                const auto& visual = it->second;
                // Convert EQ coords (x, y, z) to Irrlicht coords (x, z, y)
                out_pos.X = visual.lastX;
                out_pos.Y = visual.lastZ + visual.modelYOffset;
                out_pos.Z = visual.lastY;
                return true;
            }
        );

        // B1148 (#468): the caster model's track for a CastingAnim code
        // (eqgame 0x47ddd4 reads the actor's own animation table).
        SimBridge().spellVisualFX()->setCastAnimationLengthCallback(
            [this](uint16_t entity_id, const char* code) -> int32_t {
                if (!entityRenderer_ || !code) return -1;
                const auto& entities = entityRenderer_->getEntities();
                auto it = entities.find(entity_id);
                if (it == entities.end() || !it->second.animatedNode) return -1;
                return CastAnimationTrackMs(it->second.animatedNode, code);
            });

        // B1163 (#482): the local player, for spell 859's removal gate.
        SimBridge().spellVisualFX()->setLocalPlayerIdCallback(
            [this]() -> uint16_t { return entityRenderer_ ? entityRenderer_->getPlayerSpawnId() : 0; });

        // B1164 (#483): the attach scale s of an entity's spell slots = the
        // client's actor scale from its race and Spawn_Struct size
        // (spell_fx_attach_scale.h). 0 = unknown entity (1.0 is used).
        SimBridge().spellVisualFX()->setEntityAttachScaleCallback(
            [this](uint16_t entity_id) -> float {
                if (!entityRenderer_) return 0.0f;
                const auto& entities = entityRenderer_->getEntities();
                auto it = entities.find(entity_id);
                if (it == entities.end()) return 0.0f;
                return EQ::SpellFxAttachScale(it->second.raceId, it->second.gender,
                                               it->second.serverSize);
            });

        LOG_DEBUG(MOD_GRAPHICS, "Spell visual effects initialized");

#ifdef EQT_HAS_GLES2
        if (particleManager_) {
            SimBridge().spellVisualFX()->setParticleManager(particleManager_.get());
            particleManager_->setEntityPositionCallback(
                [this](uint16_t entity_id, glm::vec3& out_pos) -> bool {
                    if (!entityRenderer_) return false;
                    const auto& entities = entityRenderer_->getEntities();
                    // Hand bone pseudo-entities (bit 15) and B1131's head bone
                    // (record 22's halo disc): spell_fx_bone_anchor.h, shared
                    // with tools/model_viewer's FX harness (B1138).
                    if (isSpellFxBoneId(entity_id)) {
                        auto it = entities.find(EQ::SpellVisualFX::realEntityId(entity_id));
                        if (it == entities.end() || !it->second.isAnimated || !it->second.animatedNode) return false;
                        irr::core::vector3df bonePos;
                        if (!resolveSpellFxBonePosition(it->second.animatedNode, entity_id, bonePos))
                            return false;
                        out_pos = glm::vec3(bonePos.X, bonePos.Y, bonePos.Z);
                        return true;
                    }
                    // Normal entity lookup
                    auto it = entities.find(entity_id);
                    if (it == entities.end()) return false;
                    const auto& visual = it->second;
                    out_pos.x = visual.lastX;
                    out_pos.y = visual.lastZ + visual.modelYOffset;
                    out_pos.z = visual.lastY;
                    return true;
                }
            );
            particleManager_->setEntityDirectionCallback(
                [this](uint16_t entity_id, glm::vec3& out_dir) -> bool {
                    // B1167 (#486): a hand id is its bone's direction (the
                    // spray), every other id its entity's facing -- the frame
                    // an attached spellsnew.edd emitter orients to
                    // (resolveSpellFxAttachDirection).
                    if (!entityRenderer_) return false;
                    const auto& entities = entityRenderer_->getEntities();
                    auto it = entities.find(EQ::SpellVisualFX::realEntityId(entity_id));
                    if (it == entities.end() || !it->second.isAnimated || !it->second.animatedNode) return false;
                    irr::core::vector3df dir;
                    if (!resolveSpellFxAttachDirection(it->second.animatedNode, entity_id, dir)) return false;
                    out_dir = glm::vec3(dir.X, dir.Y, dir.Z);
                    return true;
                }
            );
            LOG_DEBUG(MOD_GRAPHICS, "SpellVisualFX: GLES2 particle delegation enabled");
        }
#endif

        // Enable worker-driven mode for desktop GL path when SimulationWorker is running
        if (simulationWorker_ && simulationWorker_->isRunning()) {
            SimBridge().spellVisualFX()->setWorkerDriven(true);
            LOG_DEBUG(MOD_GRAPHICS, "SpellVisualFX: worker-driven mode enabled");
        }

        // T124: register the SpellVisualFX texture-eviction guard -- see
        // SpellVisualFX::swapEvictedTexture()'s doc comment (spell_visual_fx.h)
        // for the gap this closes. spellVisualFX_ is constructed at most once
        // (guarded by this same `if (!spellVisualFX_)`), so this registration
        // also runs at most once. Placeholder resolved ONCE here, same
        // non-reentrancy constraint as every other guard.
        if (constrainedTextureCache_) {
            EQ::SpellVisualFX* spellFX = SimBridge().spellVisualFX().get();
            ZoneStream().spellVisualFXTextureGuard() = std::make_unique<ForwardingTextureGuard>(
                [spellFX](irr::video::ITexture* from, irr::video::ITexture* to) {
                    spellFX->swapEvictedTexture(from, to);
                },
                constrainedTextureCache_->getPlaceholderTexture());
            constrainedTextureCache_->addEvictionListener(ZoneStream().spellVisualFXTextureGuard().get());
        }
    }
}

// T294: these four were left as no-op stubs when U07c2 (0f25cb57) deleted
// WindowManager, and were never re-pointed at the static-panel inventory
// popup that replaced it. toggleInventory() is NOT dead code — the `I`
// keybind reaches it (hotkeys.json "ToggleInventory": "I" ->
// HotkeyAction::ToggleInventory -> RendererAction::ToggleInventory ->
// renderer_frame.cpp's action drain), so `I` silently did nothing and the
// popup was reachable only via /inventory. This is now THE body; the /inventory
// handler calls straight into it so the two paths cannot drift.
void IrrlichtRenderer::toggleInventory() {
    if (isInventoryOpen()) {
        closeInventory();
    } else {
        openInventory();
    }
}

void IrrlichtRenderer::openInventory() {
    frame_->inventoryState_.activePopup = PopupType::Inventory;
    // T60: popup opened — mark the doll dirty so it re-renders even if the
    // appearance hasn't changed since it was last open (e.g. the RTT
    // texture is stale, or this is the first open of the session and
    // ensurePaperdollInitialized() hasn't run yet).
    if (Overlays().characterModelView()) {
        Overlays().characterModelView()->markDirty();
    }
    LOG_DEBUG(MOD_UI, "IrrlichtRenderer: inventory popup OPEN");
}

void IrrlichtRenderer::closeInventory() {
    if (frame_->inventoryState_.activePopup == PopupType::Inventory) {
        frame_->inventoryState_.activePopup = PopupType::None;
    }
    LOG_DEBUG(MOD_UI, "IrrlichtRenderer: inventory popup CLOSED");
}

void IrrlichtRenderer::showNoteWindow(const std::string& text, uint8_t type) {
    if (isLoading()) return;
    LOG_INFO(MOD_GRAPHICS, "Note (type {}): {}", type, text);
}

bool IrrlichtRenderer::isInventoryOpen() const {
    return frame_->inventoryState_.activePopup == PopupType::Inventory;
}

void IrrlichtRenderer::setCharacterInfo(const std::wstring& name, int level, const std::wstring& className) {
    // WindowManager removed — character info stored for future new UI
    (void)name; (void)level; (void)className;
}

void IrrlichtRenderer::setCharacterDeity(const std::wstring& deity) {
    (void)deity;  // WindowManager removed
}

void IrrlichtRenderer::setExpProgress(float progress) {
    (void)progress;  // WindowManager removed
}

void IrrlichtRenderer::updateCharacterStats(uint32_t curHp, uint32_t maxHp,
                                             uint32_t curMana, uint32_t maxMana,
                                             uint32_t curEnd, uint32_t maxEnd,
                                             int ac, int atk,
                                             int str, int sta, int agi, int dex, int wis, int intel, int cha,
                                             int pr, int mr, int dr, int fr, int cr,
                                             float weight, float maxWeight,
                                             uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper) {
    // WindowManager removed — stats stored for future new UI
    (void)curHp; (void)maxHp; (void)curMana; (void)maxMana; (void)curEnd; (void)maxEnd;
    (void)ac; (void)atk; (void)str; (void)sta; (void)agi; (void)dex; (void)wis; (void)intel; (void)cha;
    (void)pr; (void)mr; (void)dr; (void)fr; (void)cr; (void)weight; (void)maxWeight;

    // T393 (board B201): the four coin counts used to be discarded here
    // alongside the rest. They are not any more -- they go to the same
    // InventoryPanelState the CurrencyChanged bridge arm writes, so a stats
    // push and a currency event agree on what the purse holds.
    //
    // NOTE the reaching path is the BRIDGE ARM, not this function:
    // updateCharacterStats() has zero callers anywhere in the tree
    // (grepped src/ include/ tests/ tools/ benchmarks/ docker/ -- only the
    // declaration, the three OutputRenderer overrides and the test
    // fixture's empty one). It stays wired because a discard is what B201
    // was, and a future caller must not silently drop the purse again.
    frame_->inventoryState_.coins[InventoryLayout::COIN_PLATINUM] =
        static_cast<int32_t>(platinum);
    frame_->inventoryState_.coins[InventoryLayout::COIN_GOLD] =
        static_cast<int32_t>(gold);
    frame_->inventoryState_.coins[InventoryLayout::COIN_SILVER] =
        static_cast<int32_t>(silver);
    frame_->inventoryState_.coins[InventoryLayout::COIN_COPPER] =
        static_cast<int32_t>(copper);
}

void IrrlichtRenderer::updatePlayerAppearance(uint16_t raceId, uint8_t gender,
                                               const EntityAppearance& appearance) {
    if (isLoading()) return;
    LOG_DEBUG(MOD_GRAPHICS, "IrrlichtRenderer::updatePlayerAppearance race={} gender={}", raceId, gender);
    (void)appearance;  // WindowManager removed

    // B448 (#29): the RACIAL VISION FLOOR for a race change on a player who is
    // already in the world. registerEntity() sets it at zone-in and nowhere
    // else, so an illusion or a GM `#race N` -- OP_Illusion for our own spawn
    // id, which EntitiesComponent::ZoneProcessIllusion() publishes as
    // EntityAppearanceChanged with isPlayer set -- reached this function and
    // stopped at the LOG_DEBUG above (MEASURED T428: `#race 330` confirmed in
    // chat, no "Player race 330 -> base vision" line, vision ratio 0.95-0.99).
    // The same arm carries every OP_WearChange / inventory re-publish of the
    // race the player already has, so only a CHANGE moves the floor.
    // Pinned by tests/test_self_race_vision.cpp.
    if (playerCamera_ && raceId != 0 && raceId != playerCamera_->playerRace()) {
        playerCamera_->setPlayerRace(raceId);
    }
}

void IrrlichtRenderer::updateEntityAppearance(uint16_t spawnId, uint16_t raceId, uint8_t gender,
                                               const EntityAppearance& appearance) {
    if (isLoading()) return;
    LOG_DEBUG(MOD_GRAPHICS, "IrrlichtRenderer::updateEntityAppearance spawn={} race={} gender={}",
              spawnId, raceId, gender);
    if (entityRenderer_) {
        entityRenderer_->updateEntityAppearance(spawnId, raceId, gender, appearance);
    }
}



void IrrlichtRenderer::setEntityWeaponSkills(uint16_t spawnId, uint8_t primaryWeaponSkill, uint8_t secondaryWeaponSkill) {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->setEntityWeaponSkills(spawnId, primaryWeaponSkill, secondaryWeaponSkill);
    }
}

uint8_t IrrlichtRenderer::getEntityPrimaryWeaponSkill(uint16_t spawnId) const {
    if (entityRenderer_) {
        return entityRenderer_->getEntityPrimaryWeaponSkill(spawnId);
    }
    return 0;
}

uint8_t IrrlichtRenderer::getEntitySecondaryWeaponSkill(uint16_t spawnId) const {
    if (entityRenderer_) {
        return entityRenderer_->getEntitySecondaryWeaponSkill(spawnId);
    }
    return 0;
}

void IrrlichtRenderer::queueCombatAnimation(uint16_t sourceId, uint16_t targetId,
                                             uint8_t weaponSkill, int32_t damage, float damagePercent) {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->queueCombatAnimation(sourceId, targetId, weaponSkill, damage, damagePercent);
    }
}

bool IrrlichtRenderer::hasEntityPendingCombatAnims(uint16_t spawnId) const {
    if (entityRenderer_) {
        return entityRenderer_->hasEntityPendingCombatAnims(spawnId);
    }
    return false;
}

void IrrlichtRenderer::queueReceivedDamageAnimation(uint16_t spawnId) {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->queueReceivedDamageAnimation(spawnId);
    }
}

void IrrlichtRenderer::queueSkillAnimation(uint16_t spawnId, const std::string& animCode) {
    if (isLoading()) return;
    if (entityRenderer_) {
        entityRenderer_->queueSkillAnimation(spawnId, animCode);
    }
}

} // namespace Graphics
} // namespace EQT
