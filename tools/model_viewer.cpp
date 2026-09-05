// Standalone EQ Character Model Viewer
// For debugging model/texture rendering issues
// Keys:
//   Arrow keys: Rotate model (Left/Right = Y axis, Up/Down = X axis)
//   Q/E: Rotate model on Z axis
//   +/-: Zoom in/out
//   V: Cycle body variant (static mode)
//   H: Cycle head variant (static mode)
//   B/N: Cycle body texture (prev/next) - 0=naked, 1=leather, 2=chain, 3=plate, 10+=robes
//   G/J: Cycle helm texture (prev/next)
//   A: Cycle animations (next)
//   Z: Cycle animations (previous)
//   Space: Pause/Resume animation
//   [/]: Decrease/Increase animation speed
//   W: Toggle wireframe
//   U: Toggle texture U flip (X axis mirror)
//   R: Reset rotation
//   S: Toggle static/animated mesh mode
//   ,/.: Cycle entities (prev/next)
//   F12: Screenshot (saves to model_viewer.png)
//   ESC: Quit
//
// T385 (2026-09-05, board B167): this TU was 3,246 lines -- over the 3,000-line
// hard cap -- and invisible to check_file_sizes.sh, which did not scan tools/.
// It is now just main(): the phases live in tools/model_viewer/*.cpp behind
// model_viewer_state.h. See refactor/T385_tools_ceiling_model_viewer.md.

#include "model_viewer/model_viewer_state.h"

int main(int argc, char* argv[]) {
    // T270: race codes/scales/archives are served only from WilleqDatabase
    // (race_codes.cpp's pre-JSON hardcoded switch is deleted), so the viewer
    // has to open the shipped db like the client does -- see tools/tool_race_db.h.
    openWilleqRaceDatabase();

    ViewerArgs args;
    parseCommandLine(argc, argv, args);

    // Set the global debug level
    SetDebugLevel(args.debugLevel);

    printUsageBanner(args);

    // Load entity file if specified
    bool usingEntityMode = false;
    if (!args.entityFile.empty()) {
        loadedEntities = parseEntityFile(args.entityFile);
        if (!loadedEntities.empty()) {
            usingEntityMode = true;
            std::cout << "Entities available:" << std::endl;
            for (size_t i = 0; i < loadedEntities.size(); i++) {
                std::cout << "  [" << i << "] " << loadedEntities[i].name
                << " (race=" << loadedEntities[i].race_id
                << ", gender=" << (int)loadedEntities[i].gender << ")" << std::endl;
            }
            std::cout << std::endl;
        }
    }
    // For entity mode, skip upfront S3D loading - RaceModelLoader handles it lazily
    std::shared_ptr<CharacterModel> character = nullptr;
    if (!usingEntityMode && !spellTestMode) {
        if (!loadDirectRaceMode(args, character)) {
            return 1;
        }
    }

    if (!createViewerDevice()) {
        return 1;
    }
    initViewerSubsystems();

    if (spellTestMode) {
        initSpellTestMode();
    }

    // If we have loaded entities and a specific entity was requested, load it
    bool loadedEntityFromFile = spellTestMode;  // Spell test mode already created entities
    if (!spellTestMode && !loadedEntities.empty()) {
        int entityIdx = -1;
        if (!args.entityName.empty()) {
            entityIdx = findEntityByName(args.entityName);
            if (entityIdx >= 0) {
                std::cout << "Found entity '" << args.entityName << "' at index " << entityIdx << std::endl;
            } else {
                std::cout << "Entity '" << args.entityName << "' not found, loading first entity" << std::endl;
                entityIdx = 0;
            }
        } else {
            // Load first entity by default
            entityIdx = 0;
        }

        loadEntityModel(entityIdx);
        loadedEntityFromFile = true;
    }

    // If no entity was loaded, use the default behavior
    if (!loadedEntityFromFile) {
        // Start in animated mode if animations are available
        if (character->animatedSkeleton && !character->animatedSkeleton->animations.empty()) {
            useAnimatedMesh = true;
            toggleAnimationMode();  // This will create the animated node
        } else {
            // Fall back to static mesh
            useAnimatedMesh = false;
            scene::IMesh* mesh = buildMesh(*characterGeometry, swapXY);
            if (!mesh) {
                LOG_ERROR(MOD_MAIN, "Failed to build mesh");
                spellFX.clearAllEffects();  // Clear before device destruction
                device->drop();
                return 1;
            }

            modelNode = smgr->addMeshSceneNode(mesh);
            if (modelNode) {
                modelNode->setScale(core::vector3df(10, 10, 10));
                modelNode->setPosition(core::vector3df(0, 0, 0));
                modelNode->setRotation(core::vector3df(0, 90, 0));  // Rotate to face camera

                for (u32 i = 0; i < modelNode->getMaterialCount(); ++i) {
                    modelNode->getMaterial(i).BackfaceCulling = false;
                    modelNode->getMaterial(i).Lighting = false;
                }
            }
            mesh->drop();
        }
    }

    // Add camera
    if (spellTestMode) {
        // Camera looking at midpoint between caster (0,0,0) and target (5,0,0)
        camera = smgr->addCameraSceneNode(
            nullptr, core::vector3df(2.5f, 3.0f, -10.0f), core::vector3df(2.5f, 1.5f, 0));
    } else {
        camera = smgr->addCameraSceneNode(
            nullptr, core::vector3df(0, 5, -cameraDistance), core::vector3df(0, 0, 0));
    }
    runViewerLoop();

    // Clean up spell effects before device destruction to avoid segfault
    spellFX.clearAllEffects();
    if (spellVisualFX) {
        spellVisualFX->clearAllEffects();
        spellVisualFX.reset();
    }
    #ifdef EQT_HAS_GLES2
    particleManager.reset();
    #endif
    spellDatabase.reset();

    device->drop();
    return 0;
    device->drop();
    return 0;
}
