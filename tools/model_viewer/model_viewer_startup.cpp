// tools/model_viewer/model_viewer_startup.cpp -- everything main() used to do
// before the render loop: the command line, the usage banner, direct
// race-code loading (S3D archives, body/head variants, the character texture
// merge), Irrlicht device creation, and subsystem wiring.
// Bodies moved from tools/model_viewer.cpp by T385 (board B167): main()
// alone was 933 lines. Only the mechanical changes are made here --
// locals that crossed a new function boundary became ViewerArgs fields or
// out-parameters, and `return 1` on a fatal path became `return false`.

#include "model_viewer_state.h"
#include "model_viewer_fx_harness.h"
#include "client/graphics/gles2_shader_cache.h"


void parseCommandLine(int argc, char* argv[], ViewerArgs& args) {
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--entities" && i + 1 < argc) {
            args.entityFile = argv[++i];
        } else if (arg == "--entity" && i + 1 < argc) {
            args.entityName = argv[++i];
        } else if (arg == "--client" && i + 1 < argc) {
            clientPath = argv[++i];
            if (clientPath.back() != '/') clientPath += '/';
        } else if (arg == "--spell-test") {
            spellTestMode = true;
        // B1138: the scripted FX harness (model_viewer_fx_harness.h).
        } else if (arg == "--plan" && i + 1 < argc) {
            harnessOpts.planPath = argv[++i];
        } else if (arg == "--frames-dir" && i + 1 < argc) {
            harnessOpts.framesDir = argv[++i];
        } else if (arg == "--fps" && i + 1 < argc) {
            harnessOpts.fps = std::atoi(argv[++i]);
        } else if (arg == "--seconds" && i + 1 < argc) {
            harnessOpts.seconds = std::atof(argv[++i]);
        } else if (arg == "--caster-px" && i + 1 < argc) {
            harnessOpts.casterPx = static_cast<float>(std::atof(argv[++i]));
            harnessOpts.casterPxGiven = true;
        } else if (arg == "--sim-hz" && i + 1 < argc) {
            harnessOpts.simHz = std::atoi(argv[++i]);
        } else if (arg == "--no-ground") {
            harnessOpts.ground = false;
        } else if (arg == "--headless") {
            harnessOpts.headless = true;
        } else if (arg == "--camera" && i + 1 < argc) {
            harnessOpts.cameraPreset = argv[++i];
        } else if (arg == "--backdrop" && i + 1 < argc) {
            harnessOpts.backdropPath = argv[++i];
        } else if (arg == "--eqe1" && i + 1 < argc) {
            harnessOpts.eqe1Path = argv[++i];
        } else if (arg == "--eqe1-dir" && i + 1 < argc) {
            harnessOpts.eqe1Dir = argv[++i];
        } else if (arg == "--caster-level" && i + 1 < argc) {
            harnessOpts.casterLevel = std::atoi(argv[++i]);
        } else if ((arg == "--fx-seed" || arg == "--seed") && i + 1 < argc) {
            // B1169 (#488): the particle RNG seed (default 1, fixed); 0 = draw
            // one from std::random_device (the client's own unseeded path).
            harnessOpts.seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--ui-occluder" && i + 1 < argc) {
            // B1139: x0,y0,x1,y1 screen px, repeatable
            HarnessOptions::Rect r{};
            if (std::sscanf(argv[++i], "%d,%d,%d,%d", &r.x0, &r.y0, &r.x1, &r.y1) == 4)
                harnessOpts.uiOccluders.push_back(r);
            harnessOpts.uiOccludersGiven = true;
        } else if (arg == "--no-ui-occluder") {
            harnessOpts.uiOccluders.clear();
            harnessOpts.uiOccludersGiven = true;
        } else if (arg == "--target-death" && i + 1 < argc) {
            harnessOpts.targetDeathAfterAction = std::atof(argv[++i]);
        } else if (arg == "--target-despawn" && i + 1 < argc) {
            harnessOpts.targetDespawnAfterAction = std::atof(argv[++i]);
        } else if (arg == "--caster-appearance" && i + 1 < argc) {
            // B1139: face,texture:e0,e1,...,e8 (the Spawn_Struct TextureProfile)
            unsigned f = 0, t = 0;
            uint32_t* e = harnessOpts.casterEquipment;
            if (std::sscanf(argv[++i], "%u,%u:%u,%u,%u,%u,%u,%u,%u,%u,%u", &f, &t, &e[0], &e[1], &e[2], &e[3],
                            &e[4], &e[5], &e[6], &e[7], &e[8]) == 11) {
                harnessOpts.casterFace = static_cast<uint8_t>(f);
                harnessOpts.casterTexture = static_cast<uint8_t>(t);
                harnessOpts.casterAppearanceSet = true;
            }
            harnessOpts.casterGiven = true;
        } else if (arg == "--spell-particle-density" && i + 1 < argc) {
            harnessOpts.spellFx.density = static_cast<float>(std::atof(argv[++i]));  // B1165
        } else if (arg == "--spell-particle-opacity" && i + 1 < argc) {
            harnessOpts.spellFx.opacity = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--spell-particle-nearclip" && i + 1 < argc) {
            harnessOpts.spellFx.nearClip = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--spell-particle-filter" && i + 1 < argc) {
            harnessOpts.spellFx.castFilter = std::atoi(argv[++i]);
        } else if (arg == "--show-spell-effects" && i + 1 < argc) {
            harnessOpts.spellFx.showSpellEffects = std::atoi(argv[++i]) != 0;
        } else if (arg == "--particle-options") {
            harnessOpts.particleOptions = true;
        } else if (arg == "--worker-latency" && i + 1 < argc) {
            harnessOpts.workerLatency = std::atoi(argv[++i]) > 0 ? 1 : 0;
        } else if (arg == "--caster" && i + 1 < argc) {
            // race:gender[:size]
            std::sscanf(argv[++i], "%hu:%hhu:%f", &harnessOpts.casterRace,
                        &harnessOpts.casterGender, &harnessOpts.casterSize);
            harnessOpts.casterGiven = true;
        } else if (arg == "--target" && i + 1 < argc) {
            std::sscanf(argv[++i], "%hu:%hhu:%f", &harnessOpts.targetRace,
                        &harnessOpts.targetGender, &harnessOpts.targetSize);
            harnessOpts.targetGiven = true;
        } else if (arg == "--gl") {
            useGLRenderer = true;
        } else if (arg == "--gles2") {
            useGLES2Renderer = true;
        } else if (arg == "--debug" || arg == "-d") {
            args.debugLevel = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                // Check if next arg is a number
                try {
                    args.debugLevel = std::stoi(argv[i + 1]);
                    ++i;
                } catch (...) {
                    // Not a number, keep args.debugLevel = 1
                }
            }
        } else if (arg[0] != '-') {
            // First non-flag arg is client path, second is race code
            if (clientPath == "/home/user/projects/claude/EverQuestP1999/") {
                clientPath = arg;
                if (clientPath.back() != '/') clientPath += '/';
            } else {
                args.raceCode = arg;
                std::transform(args.raceCode.begin(), args.raceCode.end(), args.raceCode.begin(),
                    [](unsigned char c) { return std::toupper(c); });
            }
        }
    }
}

void printUsageBanner(const ViewerArgs& args) {
    std::cout << "EQ Model Viewer" << std::endl;
    std::cout << "Client path: " << clientPath << std::endl;
    std::cout << "Debug level: " << args.debugLevel << std::endl;
    std::cout << "Usage: model_viewer [client_path] [race_code]" << std::endl;
    std::cout << "       model_viewer --entities <file.json> [--entity <name>]" << std::endl;
    std::cout << "       model_viewer --spell-test [--gles2] --client <path>" << std::endl;
    std::cout << "       model_viewer --spell-test --client <path> --plan <tsv> --frames-dir <dir>" << std::endl;
    std::cout << "                    --fps 30 --seconds N --headless [--camera b1118] [--backdrop <png>]" << std::endl;
    std::cout << "                    [--ui-occluder x0,y0,x1,y1]... [--no-ui-occluder] [--target-death <s>]" << std::endl;
    std::cout << "                    [--target-despawn <s>] [--worker-latency 0|1]" << std::endl;
    std::cout << "                    [--caster-appearance face,texture:e0,...,e8] [--sim-hz <hz>]" << std::endl;
    std::cout << "                    [--caster race:gender[:size]] [--target race:gender[:size]]" << std::endl;
    std::cout << "                    [--eqe1 <willeq.eqe1>] [--eqe1-dir <dir>] [--fx-seed N] [--no-ground] [--caster-px 235] [--caster-level 60]   (B1138 FX harness)" << std::endl;
    std::cout << "                    [--spell-particle-density 1] [--spell-particle-opacity 1] [--spell-particle-nearclip 2]" << std::endl;
    std::cout << "                    [--spell-particle-filter 24] [--show-spell-effects 1] [--particle-options]   (B1165; exit 65 off the capture ini)" << std::endl;
    std::cout << "       --debug, -d [level]  Enable debug output (default level 1)" << std::endl;
    std::cout << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  Arrow keys: Rotate (Y/X axis)" << std::endl;
    std::cout << "  Q/E: Rotate Z axis" << std::endl;
    std::cout << "  +/-: Zoom in/out" << std::endl;
    std::cout << "  S: Toggle static/animated mode" << std::endl;
    std::cout << "  V/Shift+V: Cycle body variant next/prev" << std::endl;
    std::cout << "  H/Shift+H: Cycle head variant next/prev" << std::endl;
    std::cout << "  B: Toggle equipment textures on/off" << std::endl;
    std::cout << "  N/Shift+N: Cycle body texture next/prev (when equipment textures on)" << std::endl;
    std::cout << "  A/Shift+A: Cycle animations next/prev" << std::endl;
    std::cout << "  Space: Pause/Resume animation" << std::endl;
    std::cout << "  [/]: Decrease/Increase animation speed" << std::endl;
    std::cout << "  W: Toggle wireframe" << std::endl;
    std::cout << "  R: Reset rotation" << std::endl;
    std::cout << "  ,/.: Cycle entities (prev/next)" << std::endl;
    std::cout << "  F7/Shift+F7: Cycle spell effects (next/prev)" << std::endl;
    std::cout << "  1-0: Cast spell from slot" << std::endl;
    std::cout << "  ESC: Quit" << std::endl;
    std::cout << std::endl;
}

// Direct race-code mode: no entity JSON and no spell test, so the viewer loads
// global_chr.s3d up front and picks a character out of it. Returns false on a
// fatal load failure (main turns that into exit 1).
bool loadDirectRaceMode(const ViewerArgs& args, std::shared_ptr<CharacterModel>& character) {
    std::shared_ptr<S3DZone> zone = nullptr;
    // Direct race code mode - load global_chr.s3d
    std::string s3dPath = clientPath + "global_chr.s3d";
    std::cout << "Loading: " << s3dPath << std::endl;

    S3DLoader loader;
    if (!loader.loadZone(s3dPath)) {
        LOG_ERROR(MOD_MAIN, "Failed to load {}: {}", s3dPath, loader.getError());
        return false;
    }

    zone = loader.getZone();
    if (!zone || zone->characters.empty()) {
        LOG_ERROR(MOD_MAIN, "No character models found in {}", s3dPath);
        return false;
    }

    std::cout << "Found " << zone->characters.size() << " character models" << std::endl;

    // Find the requested model
    currentRaceCode = args.raceCode;

    for (const auto& ch : zone->characters) {
        // Extract model base from skeleton name (remove _HS_DEF suffix)
        std::string modelBase = ch->name;
        size_t hsDefPos = modelBase.find("_HS_DEF");
        if (hsDefPos != std::string::npos) {
            modelBase = modelBase.substr(0, hsDefPos);
        }
        // Convert to uppercase for comparison
        std::string upperBase = modelBase;
        std::transform(upperBase.begin(), upperBase.end(), upperBase.begin(),
            [](unsigned char c) { return std::toupper(c); });

        std::cout << "  Available: " << ch->name << " (base: " << upperBase
        << ", " << ch->parts.size() << " parts";
        if (ch->animatedSkeleton) {
            std::cout << ", " << ch->animatedSkeleton->animations.size() << " anims";
        }
        std::cout << ")" << std::endl;

        // Look for requested model
        if (upperBase == currentRaceCode) {
            character = ch;
            std::cout << "  -> Selected " << currentRaceCode << " model" << std::endl;
        }
    }

    // Fall back to first model if not found
    if (!character && !zone->characters.empty()) {
        character = zone->characters[0];
        std::cout << "  -> " << currentRaceCode << " not found, using first model" << std::endl;
    }

    if (!character) {
        LOG_ERROR(MOD_MAIN, "No character model found");
        return false;
    }
    std::cout << "Character: " << character->name << " with " << character->parts.size() << " parts" << std::endl;

    // Map race code to ID for animated mesh loading
    // This is a simplified mapping - could be expanded
    if (currentRaceCode == "HUM") currentRaceId = 1;
    else if (currentRaceCode == "BAM") currentRaceId = 2;
    else if (currentRaceCode == "ERM") currentRaceId = 3;
    else if (currentRaceCode == "ELM") currentRaceId = 4;
    else if (currentRaceCode == "HIM") currentRaceId = 5;
    else if (currentRaceCode == "DAM") currentRaceId = 6;
    else if (currentRaceCode == "HAM") currentRaceId = 7;
    else if (currentRaceCode == "DWM") currentRaceId = 8;
    else if (currentRaceCode == "TRM") currentRaceId = 9;
    else if (currentRaceCode == "OGM") currentRaceId = 10;
    else if (currentRaceCode == "HOM") currentRaceId = 11;
    else if (currentRaceCode == "GNM") currentRaceId = 12;
    else if (currentRaceCode == "ELE") currentRaceId = 75;  // Elemental
    else if (currentRaceCode == "SKE") currentRaceId = 21;  // Skeleton
    else currentRaceId = 1;  // Default to human

    // Extract race code from skeleton name for classifying parts
    std::string raceCode = character->name;
    size_t hsDefPos = raceCode.find("_HS_DEF");
    if (hsDefPos != std::string::npos) {
        raceCode = raceCode.substr(0, hsDefPos);
    }
    std::transform(raceCode.begin(), raceCode.end(), raceCode.begin(),
        [](unsigned char c) { return std::toupper(c); });

    // Categorize parts into body variants and head variants
    for (const auto& part : character->parts) {
        std::string partName = part->name;
        std::transform(partName.begin(), partName.end(), partName.begin(),
            [](unsigned char c) { return std::toupper(c); });

        // Check if it's a head variant (contains "HE" after race code)
        std::string headPattern = raceCode + "HE";
        if (partName.find(headPattern) == 0) {
            headVariants.push_back(part);
            std::cout << "  Head variant: " << part->name << " (" << part->vertices.size() << " verts)" << std::endl;
        } else if (partName.find(raceCode) == 0) {
            bodyVariants.push_back(part);
            std::cout << "  Body variant: " << part->name << " (" << part->vertices.size() << " verts)" << std::endl;
        }
    }

    std::cout << "Found " << bodyVariants.size() << " body variants, "
    << headVariants.size() << " head variants" << std::endl;

    // Start with first body and first head variant
    currentBodyVariant = 0;
    currentHeadVariant = 0;

    // Combine initial variants
    combineCurrentVariants();
    if (!characterGeometry || characterGeometry->vertices.empty()) {
        LOG_ERROR(MOD_MAIN, "Failed to combine character variants");
        return false;
    }

    std::cout << "Initial geometry: " << characterGeometry->vertices.size() << " vertices, "
    << characterGeometry->triangles.size() << " triangles" << std::endl;

    // Load textures from all S3D files (base textures first, then overrides)
    // Order: global_chr.s3d -> global2-7_chr.s3d -> zone_chr.s3d (zone takes precedence)
    if (args.debugLevel >= 1) {
        std::cout << "\n=== Loading Textures ===" << std::endl;
    }

    // 1. Start with textures from global_chr.s3d
    characterTextures = zone->characterTextures;
    if (args.debugLevel >= 1) {
        std::cout << "  [global_chr.s3d] Loaded " << characterTextures.size() << " textures" << std::endl;
        if (args.debugLevel >= 2) {
            for (const auto& [name, tex] : characterTextures) {
                std::cout << "    - " << name << " (" << (tex ? tex->data.size() : 0) << " bytes)" << std::endl;
            }
        }
    }

    // 2. Load textures from global2-7_chr.s3d files
    for (int globalNum = 2; globalNum <= 7; ++globalNum) {
        std::string globalPath = clientPath + "global" + std::to_string(globalNum) + "_chr.s3d";
        S3DLoader globalLoader;
        if (globalLoader.loadZone(globalPath)) {
            auto globalData = globalLoader.getZone();
            if (globalData && !globalData->characterTextures.empty()) {
                size_t addedCount = 0;
                for (const auto& [name, tex] : globalData->characterTextures) {
                    if (characterTextures.find(name) == characterTextures.end()) {
                        characterTextures[name] = tex;
                        addedCount++;
                    }
                }
                if (args.debugLevel >= 1) {
                    std::cout << "  [global" << globalNum << "_chr.s3d] Added " << addedCount
                    << " new textures (skipped " << (globalData->characterTextures.size() - addedCount)
                    << " duplicates)" << std::endl;
                }
            } else if (args.debugLevel >= 1) {
                std::cout << "  [global" << globalNum << "_chr.s3d] Loaded (no character textures)" << std::endl;
            }
        } else if (args.debugLevel >= 1) {
            std::cout << "  [global" << globalNum << "_chr.s3d] Not found or failed to load" << std::endl;
        }
    }

    // 3. Load zone-specific textures (these take precedence over global textures)
    if (!currentZoneName.empty()) {
        std::string zoneS3dPath = clientPath + currentZoneName + "_chr.s3d";
        S3DLoader zoneLoader;
        if (zoneLoader.loadZone(zoneS3dPath)) {
            auto zoneData = zoneLoader.getZone();
            if (zoneData && !zoneData->characterTextures.empty()) {
                size_t overrideCount = 0;
                size_t newCount = 0;
                for (const auto& [name, tex] : zoneData->characterTextures) {
                    if (characterTextures.find(name) != characterTextures.end()) {
                        overrideCount++;
                    } else {
                        newCount++;
                    }
                    characterTextures[name] = tex;  // Override or add
                }
                if (args.debugLevel >= 1) {
                    std::cout << "  [" << currentZoneName << "_chr.s3d] Added " << newCount
                    << " new textures, overrode " << overrideCount << " existing" << std::endl;
                }
                if (args.debugLevel >= 2) {
                    std::cout << "    Zone textures:" << std::endl;
                    for (const auto& [name, tex] : zoneData->characterTextures) {
                        std::cout << "      - " << name << " (" << (tex ? tex->data.size() : 0) << " bytes)" << std::endl;
                    }
                }
            }
        } else if (args.debugLevel >= 1) {
            std::cout << "  [" << currentZoneName << "_chr.s3d] Not found or failed to load" << std::endl;
        }
    }

    if (args.debugLevel >= 1) {
        std::cout << "  Total: " << characterTextures.size() << " textures loaded" << std::endl;
    }
    return true;
}

// Returns false if the device could not be created.
bool createViewerDevice() {
    video::E_DRIVER_TYPE driverType = video::EDT_SOFTWARE;
    if (useGLRenderer) {
        driverType = video::EDT_OPENGL;
        std::cout << "Using OpenGL renderer" << std::endl;
    }
    #ifdef EQT_HAS_GLES2
    else if (useGLES2Renderer) {
        driverType = video::EDT_OGLES2;
        std::cout << "Using GLES2 renderer" << std::endl;
    }
    #endif
    #ifdef EQT_HAS_GLES2
    else if (spellTestMode) {
        // B1138: the spell FX chain is the GLES2 one (ParticleManager +
        // UnifiedParticleRenderer); desktop GL is gone (P30). On x86 the
        // device falls back to surfaceless EGL (llvmpipe) with no display.
        driverType = video::EDT_OGLES2;
        useGLES2Renderer = true;
        std::cout << "Spell test mode: using GLES2 renderer" << std::endl;
    }
    #endif

    // Create Irrlicht device
    eqt::ConfigureGles2ShaderCache();  // B959: cache dir via ResolveCachePathForWrite
    if (spellTestMode) applyHarnessPresetDefaults();  // B1138: --camera b1118 is 1280x800
    device = createDevice(driverType,
        core::dimension2d<u32>(static_cast<u32>(harnessOpts.width), static_cast<u32>(harnessOpts.height)),
        32, false, false, false, &receiver);

    if (!device) {
        LOG_ERROR(MOD_MAIN, "Failed to create Irrlicht device");
        return false;
    }

    device->setWindowCaption(L"EQ Model Viewer - A/Z: animations, Space: pause, S: toggle mode");

    driver = device->getVideoDriver();
    smgr = device->getSceneManager();
    fileSystem = device->getFileSystem();
    return true;
}

void initViewerSubsystems() {
    // Initialize spell bar for casting visualization
    spellBar.initialize(driver, device->getGUIEnvironment());
    spellBar.setScreenSize(800, 600);
    spellBar.setSpellClickCallback([](int index, const EQT::SpellBarEntry& spell) {
        // Don't start a new cast if already casting
        if (castingBar.isCasting()) {
        std::cout << "Already casting - cannot cast " << spell.name << std::endl;
        return;
        }

        // Create a modified spell with 2x cast time for testing
        EQT::SpellBarEntry testSpell = spell;
        testSpell.castTime = spell.castTime * 2.0f;  // Double cast time for testing

        std::cout << "Casting spell [" << (index + 1) << "]: " << testSpell.name
        << " (" << testSpell.getCastAnimation() << ", " << testSpell.castTime << "s)" << std::endl;

        // Start the cast with progress tracking
        castingBar.startCast(testSpell);

        // Play the casting animation if we have an animated node
        if (animatedNode && useAnimatedMesh) {
        std::string animCode = testSpell.getCastAnimation();
        if (animatedNode->playAnimation(animCode, true, false)) {  // loop=true while casting
        std::cout << "  Playing animation: " << animCode << std::endl;
        } else {
        std::cout << "  Animation not found: " << animCode << std::endl;
        }

        // Create casting particle effect using selected effect or default
        if (currentEffectIndex >= 0) {
        const auto* emitter = spellFX.getSpellEffectDB().getEmitterByIndex(currentEffectIndex);
        if (emitter) {
        spellFX.createCastingEffectFromEmitter(animatedNode, *emitter, testSpell);
        } else {
        spellFX.createCastingEffect(animatedNode, testSpell);
        }
        } else {
        spellFX.createCastingEffect(animatedNode, testSpell);
        }
        }
        });

    // Initialize casting bar
    castingBar.initialize(driver, device->getGUIEnvironment());
    castingBar.setScreenSize(800, 600);

    // Initialize spell particle effects
    spellFX.initialize(smgr, driver, clientPath);

    // Create race model loader for animated meshes
    raceModelLoader = std::make_unique<RaceModelLoader>(smgr, driver, fileSystem);
    raceModelLoader->setClientPath(clientPath);

    // Create equipment model loader for weapons/shields
    equipmentModelLoader = std::make_unique<EquipmentModelLoader>(smgr, driver, fileSystem);
    equipmentModelLoader->setClientPath(clientPath);
    // T62: wire a texture cache so equipped weapon/shield meshes actually
    // get textures -- see the declaration comment above.
    equipmentTextureCache = std::make_unique<ConstrainedTextureCache>(
        ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT));
    equipmentTextureCache->setDriver(driver);
    equipmentModelLoader->setConstrainedTextureCache(equipmentTextureCache.get());

    // B824: the item->model mapping is willeq.db-only
    // (getModelIdForItem() -> WilleqDatabase::getItemModel; the db is opened
    // by main() via openWilleqRaceDatabase()). The JSON map parsed here used
    // to be written and never read, so the call went.

    // Load equipment models from gequip archives
    if (equipmentModelLoader->loadEquipmentArchives()) {
        std::cout << "Loaded " << equipmentModelLoader->getLoadedModelCount() << " equipment models" << std::endl;
    } else {
        std::cout << "Warning: Failed to load equipment archives" << std::endl;
    }

    // Set current zone if loaded from entity file (enables zone-specific models)
    if (!currentZoneName.empty()) {
        std::cout << "Setting zone for model loading: " << currentZoneName << std::endl;
        raceModelLoader->setCurrentZone(currentZoneName);
    }
}

