// tools/model_viewer/model_viewer_spell_test.cpp -- spell-test mode:
// class/category/resist naming, the filtered spell list, and the cast
// trigger/complete/cancel cycle. This is the production-API caller that
// board B160 survived on (setSpellDatabase); see board B166.
// Bodies moved verbatim from tools/model_viewer.cpp by T385
// (board B167 -- that TU was 3,246 lines, over the 3,000-line hard cap).

#include "model_viewer_state.h"

const char* getClassName(int classIndex) {
    switch (static_cast<EQ::PlayerClass>(classIndex)) {
        case EQ::PlayerClass::Warrior:     return "Warrior";
        case EQ::PlayerClass::Cleric:      return "Cleric";
        case EQ::PlayerClass::Paladin:     return "Paladin";
        case EQ::PlayerClass::Ranger:      return "Ranger";
        case EQ::PlayerClass::ShadowKnight:return "Shadow Knight";
        case EQ::PlayerClass::Druid:       return "Druid";
        case EQ::PlayerClass::Monk:        return "Monk";
        case EQ::PlayerClass::Bard:        return "Bard";
        case EQ::PlayerClass::Rogue:       return "Rogue";
        case EQ::PlayerClass::Shaman:      return "Shaman";
        case EQ::PlayerClass::Necromancer: return "Necromancer";
        case EQ::PlayerClass::Wizard:      return "Wizard";
        case EQ::PlayerClass::Magician:    return "Magician";
        case EQ::PlayerClass::Enchanter:   return "Enchanter";
        case EQ::PlayerClass::Beastlord:   return "Beastlord";
        case EQ::PlayerClass::Berserker:   return "Berserker";
        default: return "Unknown";
    }
}

const char* getResistTypeName(EQ::ResistType rt) {
    switch (rt) {
        case EQ::ResistType::None:       return "None";
        case EQ::ResistType::Magic:      return "Magic";
        case EQ::ResistType::Fire:       return "Fire";
        case EQ::ResistType::Cold:       return "Cold";
        case EQ::ResistType::Poison:     return "Poison";
        case EQ::ResistType::Disease:    return "Disease";
        case EQ::ResistType::Chromatic:  return "Chromatic";
        case EQ::ResistType::Prismatic:  return "Prismatic";
        case EQ::ResistType::Physical:   return "Physical";
        case EQ::ResistType::Corruption: return "Corruption";
        default: return "Unknown";
    }
}

// Filter spells by category
bool matchesCategory(const EQ::SpellData* spell, SpellTestCategory cat) {
    switch (cat) {
        case SpellTestCategory::DirectDamage:
            return spell->isDamageSpell() && !spell->hasDuration() && !spell->isAESpell() && !spell->isBoltSpell();
        case SpellTestCategory::Bolt:
            return spell->isBoltSpell();
        case SpellTestCategory::DoT:
            return spell->isDoTSpell();
        case SpellTestCategory::AEDamage:
            return spell->isDamageSpell() && spell->isAESpell() && !spell->isPBAE();
        case SpellTestCategory::PBAE:
            return spell->isPBAE();
        case SpellTestCategory::AEBuff:
            return spell->isAESpell() && !spell->isDamageSpell() && !spell->isPBAE();
        case SpellTestCategory::Heal:
            return spell->isHealSpell();
        case SpellTestCategory::Buff:
            return spell->isBuffSpell() && !spell->isAESpell();
        case SpellTestCategory::Debuff:
            return !spell->is_beneficial && spell->hasDuration() && !spell->isDamageSpell();
        case SpellTestCategory::Utility:
            // Everything else
            return !spell->isDamageSpell() && !spell->isHealSpell() &&
                   !spell->isBuffSpell() && !(spell->hasDuration() && !spell->is_beneficial);
        default:
            return false;
    }
}

// Rebuild the filtered spell list for current class + category
void rebuildSpellTestList() {
    spellTestSpellList.clear();
    if (!spellDatabase || !spellDatabase->isLoaded()) return;

    auto pc = static_cast<EQ::PlayerClass>(spellTestClassIndex);
    auto cat = static_cast<SpellTestCategory>(spellTestCategoryIndex);

    // Scan spells by ID for this class (on-demand queries)
    // TODO: Add SQL-based class filtering to WilleqDatabase for efficiency
    for (uint32_t id = 1; id <= 8000; ++id) {
        const auto* spell = spellDatabase->getSpell(id);
        if (!spell) continue;
        // Check if this class can use it (class_levels not in SQLite yet — accept all)
        if (matchesCategory(spell, cat)) {
            spellTestSpellList.push_back(spell);
        }
    }

    // Sort by level then name
    std::sort(spellTestSpellList.begin(), spellTestSpellList.end(),
        [pc](const EQ::SpellData* a, const EQ::SpellData* b) {
            uint8_t la = a->getClassLevel(pc);
            uint8_t lb = b->getClassLevel(pc);
            if (la != lb) return la < lb;
            return a->name < b->name;
        });

    spellTestSpellIndex = 0;

    std::cout << "Spell list: " << getClassName(spellTestClassIndex) << " / "
              << spellTestCategoryNames[spellTestCategoryIndex] << " -> "
              << spellTestSpellList.size() << " spells" << std::endl;
}

// Cycle through EDD effect definitions
void cycleEffect(int direction) {
    auto& effectDB = spellFX.getSpellEffectDB();
    size_t effectCount = effectDB.getEmitterCount();

    if (effectCount == 0) {
        std::cout << "No effect definitions loaded" << std::endl;
        return;
    }

    currentEffectIndex += direction;

    // Wrap around with -1 as "default"
    if (currentEffectIndex < -1) {
        currentEffectIndex = static_cast<int>(effectCount) - 1;
    } else if (currentEffectIndex >= static_cast<int>(effectCount)) {
        currentEffectIndex = -1;
    }

    if (currentEffectIndex == -1) {
        std::cout << "Effect: DEFAULT (category-based)" << std::endl;
    } else {
        const auto* emitter = effectDB.getEmitterByIndex(currentEffectIndex);
        if (emitter) {
            std::cout << "Effect [" << currentEffectIndex << "/" << effectCount << "]: "
                      << emitter->name << " (tex: " << emitter->texture << ")" << std::endl;
        }
    }
}

// ============================================================================
// Spell Test Mode: Cast trigger and cancel
// ============================================================================
void triggerSpellTestCast() {
    if (spellTestSpellList.empty() || !spellVisualFX) return;

    const auto* spell = spellTestSpellList[spellTestSpellIndex];
    spellTestCurrentSpell = spell;
    // T381 (B160): SpellVisualFX holds no SpellDatabase* any more; feed it the
    // vocabulary the way the client does (off the SpellCastVisual* payload).
    spellVisualFX->noteSpellVocabulary(spell->id, spell->visualVocabulary());
    spellTestCastDuration = spell->cast_time_ms / 1000.0f;
    if (spellTestCastDuration < 0.5f) spellTestCastDuration = 0.5f;  // Min 0.5s for visual
    spellTestCastElapsed = 0;
    spellTestCasting = true;

    // Play idle animation looping during cast channeling
    if (animatedNode && useAnimatedMesh) {
        animatedNode->playAnimation("o01", true, false);
    }

    std::cout << "Casting: " << spell->name << " (ID " << spell->id
              << ", " << spellTestCastDuration << "s, "
              << getResistTypeName(spell->resist_type) << ")" << std::endl;

    uint32_t castMs = static_cast<uint32_t>(spellTestCastDuration * 1000);
    bool hasHandBones = (casterRightHandBoneIdx >= 0 || casterLeftHandBoneIdx >= 0);

#ifdef EQT_HAS_GLES2
    // GLES2: create particle effects at each hand bone position
    if (hasHandBones && particleManager) {
        using namespace EQT::Graphics::Environment;
        auto scolor = spellVisualFX->getSpellColorForSpell(spell->id);
        glm::vec4 c(scolor.getRed() / 255.0f, scolor.getGreen() / 255.0f,
                     scolor.getBlue() / 255.0f, 1.0f);

        bool isSpray = spellVisualFX->isSprayCastStyle(spell->id);
        auto def = isSpray ? SpellPresets::CastSpray(c, spell->id) : SpellPresets::CastGlow(c, spell->id);
        float durSec = spellTestCastDuration;
        for (auto& e : def.emitters) {
            e.config.emitterLifetime = durSec;
            if (!isSpray) {
                e.config.orbitalRadius = 0.4f;     // Tight orbit around hand
            }
            e.positionOffset = glm::vec3(0.0f); // Bone position is already accurate
        }
        if (casterRightHandBoneIdx >= 0) {
            particleManager->createSpellEffect(def, casterRightHandEntityId, 0, durSec, isSpray);
        }
        if (casterLeftHandBoneIdx >= 0) {
            particleManager->createSpellEffect(def, casterLeftHandEntityId, 0, durSec, isSpray);
        }
    } else
#endif
    {
        // Non-GLES2 or no hand bones: glow at entity center
        spellVisualFX->createCastGlow(casterEntityId, spell->id, castMs);
    }
}

void completeSpellTestCast() {
    if (!spellTestCurrentSpell || !spellVisualFX) return;

    const auto* spell = spellTestCurrentSpell;
    std::cout << "Cast complete: " << spell->name << std::endl;

    // Remove cast glows from hands and fallback entity center
#ifdef EQT_HAS_GLES2
    if (particleManager) {
        particleManager->removeSpellEffectsForEntity(casterRightHandEntityId);
        particleManager->removeSpellEffectsForEntity(casterLeftHandEntityId);
    }
#endif
    spellVisualFX->removeCastGlow(casterEntityId);

    // Create spell completion effect on caster (at midpoint)
    spellVisualFX->createSpellComplete(casterEntityId, spell->id);

    // Play cast animation once, then return to idle
    if (animatedNode && useAnimatedMesh) {
        std::string castAnim = "t06";  // Default: offensive
        if (spell->is_beneficial) castAnim = "t04";  // Beneficial
        if (spell->isHealSpell()) castAnim = "t05";  // Heal
        animatedNode->playAnimation(castAnim, false, false);  // loop=false: play once
    }

    // Create appropriate effects based on spell type
    uint16_t effectTarget = spellTestSelfMode ? casterEntityId : targetEntityId;

    if (spell->isBoltSpell()) {
        // Bolt: projectile from caster to target, impact on arrival
        if (!spellTestSelfMode) {
            spellVisualFX->createProjectile(casterEntityId, targetEntityId, spell->id);
        }
        spellVisualFX->createImpact(effectTarget, spell->id);
    } else if (spell->isDamageSpell() && !spell->isAESpell()) {
        // Single target direct damage: instant impact on target
        spellVisualFX->createImpact(effectTarget, spell->id);
    } else if (spell->isPBAE()) {
        // PBAE: impact at caster position
        irr::core::vector3df casterPos;
        if (animatedNode) casterPos = animatedNode->getAbsolutePosition();
        spellVisualFX->createImpactAtPosition(casterPos, spell->id);
    } else if (spell->isAESpell()) {
        // Targeted AE: rain/ground circle at target's feet
        irr::core::vector3df targetPos;
        irr::scene::IAnimatedMeshSceneNode* aeNode = nullptr;
        if (targetAnimatedNode && !spellTestSelfMode) {
            aeNode = targetAnimatedNode;
        } else if (animatedNode) {
            aeNode = animatedNode;
        }
        if (aeNode) {
            targetPos = aeNode->getAbsolutePosition();
            // Model origin is at center — lower to feet using bounding box
            auto bbox = aeNode->getTransformedBoundingBox();
            float halfHeight = (bbox.MaxEdge.Y - bbox.MinEdge.Y) * 0.5f;
            targetPos.Y -= halfHeight;
        }
        float radius = spell->aoe_range > 0 ? spell->aoe_range * 0.1f : 5.0f;
        if (spell->isDamageSpell()) {
            spellVisualFX->createRainEffect(targetPos, radius, spell->id, 3.0f);
        } else {
            spellVisualFX->createGroundCircle(targetPos, radius, spell->id, 3.0f);
        }
    } else if (spell->isHealSpell()) {
        // Heal: impact on target (or self)
        spellVisualFX->createImpact(effectTarget, spell->id);
    } else if (spell->isBuffSpell()) {
        // Buff: aura on target
        spellVisualFX->createBuffAura(effectTarget, spell->id);
    } else if (spell->hasDuration() && !spell->is_beneficial) {
        // Debuff: projectile + impact on target
        if (!spellTestSelfMode) {
            spellVisualFX->createProjectile(casterEntityId, targetEntityId, spell->id);
        }
        spellVisualFX->createImpact(effectTarget, spell->id);
    } else {
        // Utility: just impact on target
        spellVisualFX->createImpact(effectTarget, spell->id);
    }

    spellTestCasting = false;
    spellTestCurrentSpell = nullptr;
}

void cancelSpellTestCast() {
    if (spellTestCasting && spellVisualFX) {
#ifdef EQT_HAS_GLES2
        // Remove hand bone particle effects created directly on PM
        if (particleManager) {
            particleManager->removeSpellEffectsForEntity(casterRightHandEntityId);
            particleManager->removeSpellEffectsForEntity(casterLeftHandEntityId);
        }
#endif
        spellVisualFX->removeCastGlow(casterEntityId);
        spellTestCasting = false;
        spellTestCurrentSpell = nullptr;
        std::cout << "Cast cancelled" << std::endl;

        // Return to idle
        if (animatedNode && useAnimatedMesh) {
            animatedNode->playAnimation("p01", true, false);
        }
    }
    // Also clear all lingering effects (Irrlicht scene nodes + GLES2 particles)
    if (spellVisualFX) {
        spellVisualFX->clearAllEffects();
        std::cout << "Effects cleared" << std::endl;
    }
#ifdef EQT_HAS_GLES2
    if (particleManager) {
        particleManager->clearAllSpellEffects();
    }
#endif
}

// ============================================================================
// Spell Test Mode initialization -- lifted out of main() (T385). Runs only
// when --spell-test was given; main() calls it after the device exists.
// ============================================================================
bool initSpellTestMode() {
    // Load spell database from SQLite
    spellDatabase = std::make_unique<EQ::SpellDatabase>();
    if (spellDatabase->initFromDatabase()) {
        std::cout << "Loaded " << spellDatabase->getSpellCount() << " spells from SQLite" << std::endl;
    } else {
        std::cout << "WARNING: Failed to load spells: " << spellDatabase->getLoadError() << std::endl;
    }

    // Create SpellVisualFX
    spellVisualFX = std::make_unique<EQ::SpellVisualFX>(smgr, driver, clientPath);

    // Set up entity position callback (handles real + virtual hand entity IDs)
    spellVisualFX->setEntityPositionCallback(
        [](uint16_t entity_id, irr::core::vector3df& out_pos) -> bool {
        // Hand bone virtual entities — track hand positions on caster
        if (entity_id == casterRightHandEntityId && animatedNode && casterRightHandBoneIdx >= 0) {
        return animatedNode->getBoneWorldPosition(casterRightHandBoneIdx, out_pos);
        }
        if (entity_id == casterLeftHandEntityId && animatedNode && casterLeftHandBoneIdx >= 0) {
        return animatedNode->getBoneWorldPosition(casterLeftHandBoneIdx, out_pos);
        }
        if (entity_id == casterEntityId && animatedNode) {
        out_pos = animatedNode->getAbsolutePosition();
        out_pos.Y += 1.0f;
        return true;
        }
        if (entity_id == targetEntityId && targetAnimatedNode) {
        out_pos = targetAnimatedNode->getAbsolutePosition();
        out_pos.Y += 1.0f;
        return true;
        }
        return false;
        });

    #ifdef EQT_HAS_GLES2
    if (useGLES2Renderer) {
        particleManager = std::make_unique<EQT::Graphics::Environment::ParticleManager>(smgr, driver);
        particleManager->init(clientPath);
        particleManager->initUnifiedRenderer();
        spellVisualFX->setParticleManager(particleManager.get());
        // Set entity position callback on ParticleManager (uses glm::vec3)
        particleManager->setEntityPositionCallback(
            [](uint16_t entity_id, glm::vec3& out_pos) -> bool {
            // Hand bone virtual entities
            if (entity_id == casterRightHandEntityId && animatedNode && casterRightHandBoneIdx >= 0) {
            irr::core::vector3df bonePos;
            if (animatedNode->getBoneWorldPosition(casterRightHandBoneIdx, bonePos)) {
            out_pos = glm::vec3(bonePos.X, bonePos.Y, bonePos.Z);
            return true;
            }
            }
            if (entity_id == casterLeftHandEntityId && animatedNode && casterLeftHandBoneIdx >= 0) {
            irr::core::vector3df bonePos;
            if (animatedNode->getBoneWorldPosition(casterLeftHandBoneIdx, bonePos)) {
            out_pos = glm::vec3(bonePos.X, bonePos.Y, bonePos.Z);
            return true;
            }
            }
            if (entity_id == casterEntityId && animatedNode) {
            auto p = animatedNode->getAbsolutePosition();
            out_pos = glm::vec3(p.X, p.Y + 1.0f, p.Z);
            return true;
            }
            if (entity_id == targetEntityId && targetAnimatedNode) {
            auto p = targetAnimatedNode->getAbsolutePosition();
            out_pos = glm::vec3(p.X, p.Y + 1.0f, p.Z);
            return true;
            }
            return false;
            });
        // Set direction callback for spray effects (bone forward vector)
        particleManager->setEntityDirectionCallback(
            [](uint16_t entity_id, glm::vec3& out_dir) -> bool {
            if (!animatedNode) return false;
            int boneIdx = -1;
            if (entity_id == casterRightHandEntityId) boneIdx = casterRightHandBoneIdx;
            else if (entity_id == casterLeftHandEntityId) boneIdx = casterLeftHandBoneIdx;
            if (boneIdx < 0) return false;
            irr::core::vector3df dir;
            if (animatedNode->getBoneWorldDirection(boneIdx, dir)) {
            out_dir = glm::vec3(dir.X, dir.Y, dir.Z);
            return true;
            }
            return false;
            });

        std::cout << "GLES2 ParticleManager + UnifiedRenderer initialized" << std::endl;
    }
    #endif

    // Create caster entity (Human male at origin) — 1x scale for correct particle sizing
    animatedNode = raceModelLoader->createAnimatedNodeWithAppearance(
        currentRaceId, currentGender, 0, 0, nullptr, -1);
    if (animatedNode) {
        animatedNode->setPosition(core::vector3df(0, 0, 0));
        animatedNode->setRotation(core::vector3df(0, 90, 0));
        animatedNode->playAnimation("p01", true, false);  // Idle
        for (u32 i = 0; i < animatedNode->getMaterialCount(); ++i) {
            animatedNode->getMaterial(i).Lighting = false;
            animatedNode->getMaterial(i).BackfaceCulling = false;
        }
        useAnimatedMesh = true;

        // Cache hand bone indices for spell particle emission
        casterRightHandBoneIdx = animatedNode->findRightHandBoneIndex();
        casterLeftHandBoneIdx = animatedNode->findLeftHandBoneIndex();
        std::cout << "Created caster entity (race " << currentRaceId
        << ") R_HAND=" << casterRightHandBoneIdx
        << " L_HAND=" << casterLeftHandBoneIdx << std::endl;
    }

    // Create target entity (Human male offset to right) — ~5 units apart at 1x scale
    targetAnimatedNode = raceModelLoader->createAnimatedNodeWithAppearance(
        1, 0, 0, 0, nullptr, -1);  // Human male
    if (targetAnimatedNode) {
        targetAnimatedNode->setPosition(core::vector3df(5, 0, 0));
        targetAnimatedNode->setRotation(core::vector3df(0, -90, 0));  // Face caster
        targetAnimatedNode->playAnimation("p01", true, false);
        for (u32 i = 0; i < targetAnimatedNode->getMaterialCount(); ++i) {
            targetAnimatedNode->getMaterial(i).Lighting = false;
            targetAnimatedNode->getMaterial(i).BackfaceCulling = false;
        }
        std::cout << "Created target entity at (5, 0, 0)" << std::endl;
    }

    // Build initial spell list
    rebuildSpellTestList();

    // Hide the old spell bar in spell test mode
    spellBar.hide();
    return true;
}
