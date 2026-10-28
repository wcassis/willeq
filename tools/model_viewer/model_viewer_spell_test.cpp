// tools/model_viewer/model_viewer_spell_test.cpp -- spell-test mode:
// class/category/resist naming, the filtered spell list, and the cast
// trigger/complete/cancel cycle. This is the production-API caller that
// board B160 survived on (setSpellDatabase); see board B166.
// Bodies moved verbatim from tools/model_viewer.cpp by T385
// (board B167 -- that TU was 3,246 lines, over the 3,000-line hard cap).

#include "model_viewer_state.h"
#include "model_viewer_fx_harness.h"
#include "client/graphics/spell_cast_animation.h"
#include <cstdlib>

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
// Spell Test Mode: Cast trigger and cancel.
//
// B1138 (#458): the three steps publish the client's own SpellCastVisual*
// payloads through eqt::bridge::ApplySpellCast*Fx (spell_cast_fx.cpp, the body
// IrrlichtBridge runs), so what the viewer draws is the production sequence:
// BeginCast -> createCastGlow, OP_Action (both packets) -> releaseCastGlow +
// createLanding, cancel -> removeCastGlow. The pre-B1131 path this replaced
// picked createImpact/createRainEffect/createGroundCircle/createBuffAura/...
// from its own isBoltSpell()/isAESpell() heuristics, which the client never
// did; those four FX entry points were deleted with it.
//
// B1157 (#476): the caster model does what the client does on OP_BeginCast through
// the client's own code (spell_cast_animation.h, the same functions
// EntityRenderer::startEntityCast / completeEntityCast / cancelEntityCast
// run): the spell's CastingAnim track, started at BeginCast, held through the
// channel, ended at Action / interrupt. The pre-B1157 rule -- an o01 idle
// looping during the channel and a guessed t04/t05/t06 played once at
// completion -- is gone; the 18 unattributed Source motion-signature rows of
// B1151 were the idle caster this replaced.
// ============================================================================
void triggerSpellTestCast() {
    if (spellTestSpellList.empty() || !spellVisualFX) return;

    const auto* spell = spellTestSpellList[spellTestSpellIndex];
    spellTestCurrentSpell = spell;
    // spells_us.txt field 13 (SpellData::cast_time_ms), as the server sends it.
    spellTestCastDuration = spell->cast_time_ms / 1000.0f;
    spellTestCastElapsed = 0;
    spellTestCasting = true;

    // The client's cast pose: the spell's own CastingAnim track (the o01 idle
    // this used to play during the channel is gone).
    spellTestCastTrack.clear();
    if (animatedNode && useAnimatedMesh) {
        spellTestCastTrack = playCastAnim(animatedNode, spell->cast_anim);
        std::cout << "Caster cast-anim: "
                  << (spellTestCastTrack.empty() ? "(no track; current stands)" : spellTestCastTrack)
                  << " (CastingAnim " << spell->cast_anim << ")" << std::endl;
    }

    std::cout << "Casting: " << spell->name << " (ID " << spell->id
              << ", " << spellTestCastDuration << "s, "
              << getResistTypeName(spell->resist_type) << ")" << std::endl;

    placeHarnessTarget(spellTestSelfMode, harnessOpts.interactiveDistance);
    harnessBeginCast(*spell, spellTestSelfMode, spell->cast_time_ms, harnessOpts.casterLevel,
                     eqt::bridge::CastGlowRoute::Clock::now());
}

void completeSpellTestCast() {
    if (!spellTestCurrentSpell || !spellVisualFX) return;

    const auto* spell = spellTestCurrentSpell;
    std::cout << "Cast complete: " << spell->name << std::endl;

    harnessAction(*spell, spellTestSelfMode, harnessOpts.casterLevel, eqt::bridge::CastGlowRoute::Clock::now());

    // OP_Action: the cast state ends, the model takes the idle (the guessed
    // t04/t05/t06 played here before is gone). Only when the trigger started
    // the track -- the client's guard.
    if (!spellTestCastTrack.empty() && animatedNode && useAnimatedMesh) {
        endCastAnim(animatedNode, "p01");
    }
    spellTestCastTrack.clear();

    spellTestCasting = false;
    spellTestCurrentSpell = nullptr;
}

void cancelSpellTestCast() {
    if (spellTestCasting && spellVisualFX && spellTestCurrentSpell) {
        harnessInterrupt(*spellTestCurrentSpell);
        spellTestCasting = false;
        spellTestCurrentSpell = nullptr;
        std::cout << "Cast cancelled" << std::endl;

        // Interrupt: the cast state ends, the model takes the idle (only
        // when the trigger started the track -- the client's guard).
        if (!spellTestCastTrack.empty() && animatedNode && useAnimatedMesh) {
            endCastAnim(animatedNode, "p01");
        }
        spellTestCastTrack.clear();
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

// Creates one spell-test entity: the client's scale rule (race scale times
// server size / 6, entityScaleForServerSize) and its material flags.
static EQAnimatedMeshSceneNode* createSpellTestEntity(uint16_t race, uint8_t gender, float size,
                                                      const core::vector3df& pos, float rotY, const char* idle,
                                                      const EntityAppearance* appearance = nullptr) {
    // B1139: with an appearance, the client's own builder (entity_mesh_build.cpp
    // calls createAnimatedNodeWithEquipment when any equipment is set).
    EQAnimatedMeshSceneNode* node =
        appearance ? raceModelLoader->createAnimatedNodeWithEquipment(race, gender, *appearance, nullptr, -1)
                   : raceModelLoader->createAnimatedNodeWithAppearance(race, gender, 0, 0, nullptr, -1);
    if (!node) return nullptr;
    const float scale = entityScaleForServerSize(raceModelLoader->getRaceScale(race), size);
    node->setScale(core::vector3df(scale, scale, scale));
    node->setPosition(pos);
    node->setRotation(core::vector3df(0, rotY, 0));
    // B1139: the client's idles -- the local player stands in p01
    // (renderer_player_physics.cpp), an idle NPC in o01 (entity_updates.cpp);
    // the lane #spawn stands feet apart (o01), not feet together (p01).
    node->playAnimation(node->hasAnimation(idle) ? idle : "p01", true, false);
    for (u32 i = 0; i < node->getMaterialCount(); ++i) {
        node->getMaterial(i).Lighting = false;
        node->getMaterial(i).BackfaceCulling = false;
    }
    node->updateAbsolutePosition();
    return node;
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

    // Create SpellVisualFX (spells.eff is read from the client directory)
    spellVisualFX = std::make_unique<EQ::SpellVisualFX>(smgr, driver, clientPath);
    if (!initHarnessTextureCache()) return false;

    // B1138: in a preset the caster (and the `#spawn` target beside her)
    // faces the camera; otherwise the caster faces the target. B1167 (#486):
    // every preset fits its camera on the actors AT their capture headings
    // (the orchestrator's ruling: b1130 on the back-turned caster; b1118 on
    // the caster with his back to the chase camera), then turnB1130Rig turns
    // the rig -- actors included -- by the capture's yaw, leaving each actor
    // at rotYForHeading(its capture heading) (fx_capture_pose.h
    // presetRotYDeg: the heading expressed against the fitted camera).
    const bool preset = !harnessOpts.cameraPreset.empty();
    const fxharness::CapturePose* capPose = preset ? fxharness::poseForPreset(harnessOpts.cameraPreset.c_str()) : nullptr;
    const float casterRotY = preset
                                 ? (capPose ? fxharness::presetRotYDeg(*capPose, capPose->casterHeading512)
                                            : kHarnessFaceOnRotY)
                                 : 90.0f;
    const float targetRotY = preset
                                 ? (capPose ? fxharness::presetRotYDeg(*capPose, capPose->targetHeading512)
                                            : kHarnessFaceOnRotY)
                                 : -90.0f;
    EntityAppearance casterLook;
    casterLook.face = harnessOpts.casterFace;
    casterLook.texture = harnessOpts.casterTexture;
    std::copy(harnessOpts.casterEquipment, harnessOpts.casterEquipment + 9, casterLook.equipment);
    animatedNode = createSpellTestEntity(harnessOpts.casterRace, harnessOpts.casterGender,
                                         harnessOpts.casterSize, core::vector3df(0, 0, 0),
                                         casterRotY, "p01",
                                         harnessOpts.casterAppearanceSet ? &casterLook : nullptr);
    if (animatedNode) {
        useAnimatedMesh = true;
        currentRaceId = harnessOpts.casterRace;
        casterRightHandBoneIdx = animatedNode->findRightHandBoneIndex();
        casterLeftHandBoneIdx = animatedNode->findLeftHandBoneIndex();
        std::cout << "Created caster entity (race " << harnessOpts.casterRace << " gender "
                  << int(harnessOpts.casterGender) << ") R_HAND=" << casterRightHandBoneIdx
                  << " L_HAND=" << casterLeftHandBoneIdx
                  << " HEAD=" << animatedNode->findHeadBoneIndexForFx() << std::endl;
        // The held items (equipment slots 7/8): the viewer's attach, placed on
        // their bones every frame by updateEquipmentPositions().
        if (harnessOpts.casterAppearanceSet &&
            (harnessOpts.casterEquipment[7] != 0 || harnessOpts.casterEquipment[8] != 0)) {
            currentRaceCode = RaceModelLoader::getRaceCode(harnessOpts.casterRace);  // the viewer's gender rule
            if (currentRaceCode.size() == 3 && currentRaceCode.back() == 'M' && harnessOpts.casterGender == 1)
                currentRaceCode.back() = 'F';
            attachEquipmentModels(harnessOpts.casterEquipment[7], harnessOpts.casterEquipment[8]);
        }
    }
    // B1155: the b1130/b1130-self target is B1130's stunned npc (#stun 120000),
    // a fixed pose: the level-1 human male the rig spawned (`#spawn TrollF
    // 1 1 0 30000` is eqemu's name/race/level/... arg order -- "TrollF" is
    // only a label, model_viewer_fx_harness.cpp). For the b1130 preset use
    // the client's player idle p01 (the caster's own, deterministic 209 px)
    // rather than the o01 "other" idle, so the target's pose -- and therefore
    // the solved camera -- is reproducible run to run. b1118 keeps o01 (its
    // B1139 feet-apart rule). B1166 (#485): the 50 px "collapsed" human
    // B1155 recorded was the bind pose the target fit measured before any
    // animation was applied; the fit now forces the p01 pose
    // (fitFixedNpcTarget) and the standing human renders 238 px at the
    // oracle's feet (refactor/B1166/README.md).
    const bool b1130Preset =
        harnessOpts.cameraPreset == "b1130" || harnessOpts.cameraPreset == "b1130-self";
    const char* targetIdle = b1130Preset ? "p01" : "o01";
    targetAnimatedNode = createSpellTestEntity(
        harnessOpts.targetRace, harnessOpts.targetGender, harnessOpts.targetSize,
        core::vector3df(harnessOpts.interactiveDistance, 0, 0), targetRotY,
        targetIdle);

    uploadHarnessTextures();
    if (!initHarnessParticles()) {
        std::cout << "ERROR: the spell FX chain did not start (see above)" << std::endl;
        return false;
    }

    // Build initial spell list
    rebuildSpellTestList();

    // Hide the old spell bar in spell test mode
    spellBar.hide();
    return true;
}
