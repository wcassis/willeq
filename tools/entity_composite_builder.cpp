// Entity Composite Texture Builder
// Offline tool that builds composite texture sheets for character models.
// Each race/gender/variant produces a single texture sheet containing all body-part
// textures, pre-tiled ("unrolled") to eliminate GL_REPEAT dependency.
//
// This enables single-draw-call entity rendering on Mali 400, where draw calls
// are the bottleneck (~92us CPU each). Composite textures give +98% FPS vs unbatched.
//
// Phase 1: Offline composite generation + UV remapping metadata.
// Phase 2 (future): Runtime mesh builder consumes these outputs.
//
// Usage:
//   entity_composite_builder --eq-path <path> --output <dir> --race HUM
//   entity_composite_builder --eq-path <path> --output <dir> --all --verbose
//   entity_composite_builder --eq-path <path> --output <dir> --all --eqe1
//
// T385 (2026-09-05, board B167): this TU was 1,861 lines. The builder's three
// stages now live in tools/entity_composite/ behind composite_builder.h; what
// is left here is the usage text and main()'s argument handling and driving.

#include "entity_composite/composite_builder.h"

using namespace ecb;

// ── Usage / main ────────────────────────────────────────────────────────────

static void printUsage(const char* prog) {
    std::cout << "Entity Composite Texture Builder\n\n"
              << "Usage: " << prog << " --eq-path <path> --output <dir> [options]\n\n"
              << "Options:\n"
              << "  --eq-path <path>     Path to EverQuest client directory\n"
              << "  --output <dir>       Output directory for composite textures\n"
              << "  --race <NAME>        Process single race (e.g., HUM, ELF, DWF)\n"
              << "  --all                Process all races found in global chr archives\n"
              << "  --variant <N>        Specific variant (default: 0)\n"
              << "  --all-variants       Process variants 0-4\n"
              << "  --max-tiles <N>      Skip a texture if its LOGICAL UV extent (the actual\n"
              << "                       repeat count the geometry uses) exceeds this (default: 8)\n"
              << "  --sheet-size <N>     Max composite sheet dimension (default: 1024)\n"
              << "  --eqe1               Also output ETC1-compressed .eqe1 files\n"
              << "  --verbose            Per-texture stats\n"
              << "\n"
              << "P16 sheet-quality options:\n"
              << "  --tile-cap <N>       Cap PHYSICAL unroll copies per axis (default: 1).\n"
              << "                       Repeats beyond this rely on the seam gutter's wrap\n"
              << "                       padding + the runtime's fmod-based UV wrap (see\n"
              << "                       mesh_building.cpp) instead of physically duplicating\n"
              << "                       pixel data — this is the unroll-inflation fix.\n"
              << "  --seam-pad <N>       UV inset width in texels: the outer N texels of each\n"
              << "                       placed texture are edge-duplicated in place (no sheet\n"
              << "                       growth) and excluded from the sampled UV range, to\n"
              << "                       stop bilinear bleed at tile boundaries (default: 2)\n"
              << "\n"
              << "P08f zone-archive options:\n"
              << "  --zone <shortname>   Build sheets for one zone's {shortname}_chr.s3d\n"
              << "                       archive (e.g., befallen, qeynos2, gfaydark)\n"
              << "  --all-zones          Build sheets for every zone in --zones-json\n"
              << "  --zones-json <path>  docs/reference/pre_luclin_zones.json (required\n"
              << "                       with --all-zones)\n"
              << "  --max-variant <N>    Highest variant number to probe for existence\n"
              << "                       when discovering a zone model's variant set\n"
              << "                       (default: 9)\n"
              << "  --report-json <path> Write a structured build report (per-zone\n"
              << "                       model status: built/duplicate/collision/\n"
              << "                       animated-skip/failed) to this path\n"
              << std::endl;
}

int main(int argc, char** argv) {
    std::string eqPath;
    std::string outputDir;
    std::string filterRace;
    bool allRaces = false;
    int variant = 0;
    bool allVariants = false;
    int maxTiles = 8;
    int maxSheetSize = 1024;
    bool writeEqe1 = false;
    bool verbose = false;
    // P16: unroll discipline (physical tile cap) + seam-gutter width.
    // Default cap=1: mathematically exact (not an approximation) for EQ
    // character textures specifically, because the original engine only
    // ever samples these with GL_REPEAT wrap in the first place — that is
    // why geometry UVs exceed [0,1] at all. A single physical copy + the
    // fmod-based runtime wrap (mesh_building.cpp) + the seam inset above
    // reproduces the N-copy physical unroll pixel-for-pixel, at ~61.5%
    // less sheet bytes measured across the global race set (vs cap=2's
    // 16.8% — most textures already had tilesU/V <= 2 and were untouched
    // by a cap of 2, so it undershot P13's projected 50-65% reduction).
    int physicalTileCap = 1;
    int seamPad = 2;
    // P08f additions. --zone may be repeated (accumulates) — processing every
    // requested zone within ONE process run is what lets the in-memory
    // collision registry (A2) catch cross-zone collisions too, not just
    // zone-vs-global; two separate tool invocations would each start from a
    // fresh registry and could each independently overwrite the other's
    // same-named sheet without ever detecting it. --all-zones adds the full
    // pre_luclin_zones.json roster to whatever --zone(s) were also given.
    std::vector<std::string> zoneArgs;
    bool allZones = false;
    std::string zonesJsonPath;
    int maxVariantProbe = 9;
    std::string reportJsonPath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--eq-path" && i + 1 < argc) eqPath = argv[++i];
        else if (arg == "--output" && i + 1 < argc) outputDir = argv[++i];
        else if (arg == "--race" && i + 1 < argc) filterRace = argv[++i];
        else if (arg == "--all") allRaces = true;
        else if (arg == "--variant" && i + 1 < argc) variant = std::atoi(argv[++i]);
        else if (arg == "--all-variants") allVariants = true;
        else if (arg == "--max-tiles" && i + 1 < argc) maxTiles = std::atoi(argv[++i]);
        else if (arg == "--sheet-size" && i + 1 < argc) maxSheetSize = std::atoi(argv[++i]);
        else if (arg == "--eqe1") writeEqe1 = true;
        else if (arg == "--verbose") verbose = true;
        else if (arg == "--tile-cap" && i + 1 < argc) physicalTileCap = std::atoi(argv[++i]);
        else if (arg == "--seam-pad" && i + 1 < argc) seamPad = std::atoi(argv[++i]);
        else if (arg == "--zone" && i + 1 < argc) zoneArgs.push_back(argv[++i]);
        else if (arg == "--all-zones") allZones = true;
        else if (arg == "--zones-json" && i + 1 < argc) zonesJsonPath = argv[++i];
        else if (arg == "--max-variant" && i + 1 < argc) maxVariantProbe = std::atoi(argv[++i]);
        else if (arg == "--report-json" && i + 1 < argc) reportJsonPath = argv[++i];
        else if (arg == "--help" || arg == "-h") { printUsage(argv[0]); return 0; }
    }

    bool zoneMode = !zoneArgs.empty() || allZones;

    if (eqPath.empty() || outputDir.empty() ||
        (!zoneMode && !allRaces && filterRace.empty())) {
        printUsage(argv[0]);
        return 1;
    }
    if (allZones && zonesJsonPath.empty()) {
        std::cerr << "Error: --all-zones requires --zones-json <path to pre_luclin_zones.json>"
                  << std::endl;
        return 1;
    }

    if (!fs::exists(eqPath) || !fs::is_directory(eqPath)) {
        std::cerr << "Error: EQ path does not exist: " << eqPath << std::endl;
        return 1;
    }

    fs::create_directories(outputDir);

    if (zoneMode) {
        // ── P08f zone-archive build path (plan A1-A4, B5-B7) ────────────────
        // Deliberately a SEPARATE code path from the original global-only flow
        // below (not a refactor of it) so that no plain `--race`/`--all`
        // invocation's behavior or output changes at all — see plan A1's
        // "keep global behavior byte-identical" requirement.
        auto overallStart = std::chrono::steady_clock::now();

        std::vector<std::string> zoneNames = zoneArgs;
        if (allZones) {
            auto allNames = loadZoneShortNames(zonesJsonPath);
            for (auto& n : allNames) {
                if (std::find(zoneNames.begin(), zoneNames.end(), n) == zoneNames.end())
                    zoneNames.push_back(n);
            }
        }

        // Seed the collision registry with every global model (A2) — loaded
        // quietly, purely for name/signature comparison; global sheets are
        // NOT (re)written by this path.
        auto globalArchives = loadGlobalArchives(eqPath, "", false);
        std::map<std::string, RegisteredModel> registry;
        registerArchives(globalArchives, registry);
        std::map<std::string, RegisteredModel> globalOnly = registry;  // snapshot before zone entries
        std::cout << "Collision registry seeded with " << registry.size()
                  << " global model names" << std::endl;
        loadPersistedRegistry(outputDir, registry);
        std::cout << "Collision registry after loading prior-run zone entries: "
                  << registry.size() << " model names" << std::endl;

        // A REGISTERED model name (known signature, from a global archive or an
        // earlier zone in this registry) is NOT necessarily already BUILT — the
        // 210 pre-existing sheets cover only 42 of the ~59 distinct global race
        // defs (the rest were apparently never included in whatever invocation
        // produced the original set). Registry membership alone is therefore the
        // wrong signal for "skip, already covered": it must be cross-checked
        // against sheets that actually exist on disk, or a same-named,
        // same-signature model that has no sheet yet would be wrongly skipped
        // forever as a false "duplicate" (caught empirically: IMP_HS_DEF is a
        // real global2_chr.s3d model with no pre-existing sheet).
        std::set<std::string> existingSheetRaces;
        if (fs::exists(outputDir)) {
            for (const auto& entry : fs::directory_iterator(outputDir)) {
                if (!entry.is_regular_file()) continue;
                std::string fn = entry.path().filename().string();
                if (fn.size() < 4 || fn.substr(fn.size() - 4) != ".bmp") continue;
                std::string stem = fn.substr(0, fn.size() - 4);
                auto lastUnderscore = stem.rfind('_');
                if (lastUnderscore == std::string::npos) continue;
                auto secondLast = stem.rfind('_', lastUnderscore - 1);
                if (secondLast == std::string::npos) continue;
                existingSheetRaces.insert(stem.substr(0, secondLast));
            }
        }
        std::cout << "Found " << existingSheetRaces.size()
                  << " distinct race names with an existing sheet on disk in " << outputDir
                  << std::endl;

        Json::Value report(Json::objectValue);
        Json::Value zonesReport(Json::objectValue);
        std::vector<ModelResult> allNewResults;
        int totalBuilt = 0, totalDuplicate = 0, totalCollision = 0,
            totalAnimatedSkip = 0, totalFailed = 0, totalNoArchive = 0;

        for (const auto& zoneName : zoneNames) {
            auto zoneStart = std::chrono::steady_clock::now();
            Json::Value zoneReport(Json::objectValue);
            Json::Value modelsReport(Json::objectValue);

            auto archiveOpt = loadZoneArchive(eqPath, zoneName, verbose);
            if (!archiveOpt) {
                zoneReport["archiveFound"] = false;
                zonesReport[zoneName] = zoneReport;
                totalNoArchive++;
                std::cout << "[" << zoneName << "] no " << zoneName
                          << "_chr.s3d (or no usable characters) — skipped" << std::endl;
                continue;
            }
            const ArchiveModels& am = *archiveOpt;
            zoneReport["archiveFound"] = true;
            zoneReport["archiveName"] = am.archiveName;
            zoneReport["modelsTotal"] = static_cast<int>(am.models.size());

            PfsArchive archive;
            if (!archive.open(am.archivePath)) {
                std::cerr << "Cannot open archive: " << am.archivePath << std::endl;
                zoneReport["archiveOpenFailed"] = true;
                zonesReport[zoneName] = zoneReport;
                continue;
            }

            int zoneBuilt = 0;
            for (const auto& model : am.models) {
                std::string raceKey = toUpper(model->name);
                Json::Value modelReport(Json::objectValue);

                auto regIt = registry.find(raceKey);
                bool hasExistingSheet = existingSheetRaces.count(toLower(raceKey)) > 0;
                if (regIt != registry.end()) {
                    auto sig = collectSignature(model);
                    if (sig == regIt->second.signature) {
                        if (hasExistingSheet) {
                            // Genuinely already covered — a sheet for this exact
                            // model exists on disk (either the original 210 or an
                            // earlier zone/model in this same run).
                            modelReport["status"] = "duplicate";
                            modelReport["existingSource"] = regIt->second.sourceArchive;
                            modelsReport[raceKey] = modelReport;
                            totalDuplicate++;
                            continue;
                        }
                        // Known model, matching signature, but NO sheet actually
                        // built yet (e.g. a global race def outside the original
                        // 210) — fall through to the normal build path below
                        // instead of skipping.
                    } else {
                        modelReport["status"] = "collision";
                        modelReport["existingSource"] = regIt->second.sourceArchive;
                        modelReport["reason"] = "same race name, different texture signature — "
                                                 "not rebuilt to avoid silently overwriting the "
                                                 "existing sheet";
                        modelsReport[raceKey] = modelReport;
                        totalCollision++;
                        std::cerr << "COLLISION: " << raceKey << " in " << am.archiveName
                                  << " has a different texture set than the existing "
                                  << regIt->second.sourceArchive << " model of the same name — skipped"
                                  << std::endl;
                        continue;
                    }
                }

                std::string animTexName;
                if (detectAnimatedTexture(model, animTexName)) {
                    modelReport["status"] = "animatedSkip";
                    modelReport["animatedTexture"] = animTexName;
                    modelsReport[raceKey] = modelReport;
                    totalAnimatedSkip++;
                    std::cout << "  [" << raceKey << "] SKIP animated texture: "
                              << animTexName << std::endl;
                    // Still register the name (with its signature) so a LATER zone/global
                    // archive with the identical model is recognized as a duplicate, not
                    // re-flagged as a fresh animated-skip.
                    registry[raceKey] = RegisteredModel{am.archiveName, collectSignature(model)};
                    continue;
                }

                auto variantsToBuild = discoverVariants(archive, model, maxVariantProbe);
                Json::Value variantsBuiltArr(Json::arrayValue);
                Json::Value variantsDiscoveredArr(Json::arrayValue);
                for (int v : variantsToBuild) variantsDiscoveredArr.append(v);

                bool anyBuilt = false;
                for (int v : variantsToBuild) {
                    std::cout << "\n[" << model->name << " variant=" << v << "] "
                              << am.archiveName << " (zone=" << zoneName << ")" << std::endl;
                    auto result = processModel(eqPath, model, am.archivePath, archive,
                                                v, maxTiles, maxSheetSize, outputDir,
                                                writeEqe1, verbose, physicalTileCap, seamPad,
                                                /*checkAnimatedTextures=*/true);
                    if (result.success) {
                        variantsBuiltArr.append(v);
                        allNewResults.push_back(result);
                        anyBuilt = true;
                        zoneBuilt++;
                        totalBuilt++;
                        existingSheetRaces.insert(toLower(raceKey));
                    } else if (result.skippedAnimated) {
                        // Shouldn't normally trigger (whole-model probe already caught it),
                        // but a per-variant substituted texture could in principle differ —
                        // report defensively rather than silently drop.
                        std::cout << "  variant=" << v << " SKIP (animated texture "
                                  << result.animatedTextureName << ")" << std::endl;
                    } else {
                        std::cout << "  variant=" << v << " FAILED to build" << std::endl;
                        totalFailed++;
                    }
                }

                modelReport["status"] = anyBuilt ? "built" : "failed";
                modelReport["variantsDiscovered"] = variantsDiscoveredArr;
                modelReport["variantsBuilt"] = variantsBuiltArr;
                modelsReport[raceKey] = modelReport;

                registry[raceKey] = RegisteredModel{am.archiveName, collectSignature(model)};
            }

            auto zoneEnd = std::chrono::steady_clock::now();
            double zoneSecs = std::chrono::duration<double>(zoneEnd - zoneStart).count();
            zoneReport["sheetsBuilt"] = zoneBuilt;
            zoneReport["elapsedSeconds"] = zoneSecs;
            zoneReport["models"] = modelsReport;
            zonesReport[zoneName] = zoneReport;

            std::cout << "\n[" << zoneName << "] " << zoneBuilt << " sheets built in "
                      << zoneSecs << "s" << std::endl;
        }

        mergeCompositeIndex(outputDir, allNewResults);
        savePersistedRegistry(outputDir, registry, globalOnly);

        auto overallEnd = std::chrono::steady_clock::now();
        double overallSecs = std::chrono::duration<double>(overallEnd - overallStart).count();

        report["zones"] = zonesReport;
        report["summary"]["sheetsBuilt"] = totalBuilt;
        report["summary"]["duplicates"] = totalDuplicate;
        report["summary"]["collisions"] = totalCollision;
        report["summary"]["animatedSkips"] = totalAnimatedSkip;
        report["summary"]["buildFailures"] = totalFailed;
        report["summary"]["zonesWithNoArchive"] = totalNoArchive;
        report["summary"]["elapsedSeconds"] = overallSecs;

        std::cout << "\n=== P08f Zone Build Summary ===" << std::endl;
        std::cout << "  Sheets built: " << totalBuilt << std::endl;
        std::cout << "  Duplicates (already covered, skipped): " << totalDuplicate << std::endl;
        std::cout << "  Collisions (reported, NOT built): " << totalCollision << std::endl;
        std::cout << "  Animated-texture skips: " << totalAnimatedSkip << std::endl;
        std::cout << "  Build failures: " << totalFailed << std::endl;
        std::cout << "  Zones with no _chr.s3d: " << totalNoArchive << std::endl;
        std::cout << "  Elapsed: " << overallSecs << "s" << std::endl;

        if (!reportJsonPath.empty()) {
            std::ofstream reportOut(reportJsonPath);
            if (reportOut.is_open()) {
                Json::StreamWriterBuilder builder;
                builder["indentation"] = "  ";
                std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
                writer->write(report, &reportOut);
                reportOut << std::endl;
                std::cout << "Report: " << reportJsonPath << std::endl;
            } else {
                std::cerr << "WARNING: could not write report to " << reportJsonPath << std::endl;
            }
        }

        return 0;
    }

    // ── Original global-only build path (UNCHANGED — byte-identical output) ──

    // Determine variants to process
    std::vector<int> variants;
    if (allVariants) {
        for (int v = 0; v <= 4; ++v) variants.push_back(v);
    } else {
        variants.push_back(variant);
    }

    // Load character models from global chr archives
    auto archives = loadGlobalArchives(eqPath, filterRace, verbose);
    if (archives.empty()) {
        std::cerr << "No character models found" << std::endl;
        return 1;
    }

    int totalModels = 0;
    for (const auto& am : archives) totalModels += am.models.size();
    std::cout << "Found " << totalModels << " character models in "
              << archives.size() << " archives" << std::endl;

    // Process each model
    int successCount = 0, failCount = 0;
    std::vector<ModelResult> allResults;

    for (const auto& am : archives) {
        // Open raw archive for texture data access
        PfsArchive archive;
        if (!archive.open(am.archivePath)) {
            std::cerr << "Cannot open archive: " << am.archivePath << std::endl;
            continue;
        }

        for (const auto& model : am.models) {
            for (int v : variants) {
                std::cout << "\n[" << model->name << " variant=" << v << "] "
                          << am.archiveName << std::endl;

                auto result = processModel(eqPath, model, am.archivePath, archive,
                                            v, maxTiles, maxSheetSize, outputDir,
                                            writeEqe1, verbose, physicalTileCap, seamPad);

                if (result.success) {
                    successCount++;
                    allResults.push_back(result);
                } else {
                    failCount++;
                }
            }
        }
    }

    // Write composite index JSON
    if (!allResults.empty()) {
        Json::Value index(Json::objectValue);
        for (const auto& r : allResults) {
            std::string key = toLower(r.raceName) + "_" + std::to_string(r.gender)
                            + "_" + std::to_string(r.variant);
            Json::Value entry;
            entry["race"] = r.raceName;
            entry["gender"] = r.gender;
            entry["variant"] = r.variant;
            entry["sheetWidth"] = r.sheetW;
            entry["sheetHeight"] = r.sheetH;
            entry["textureCount"] = static_cast<int>(r.placements.size());
            entry["bmpFile"] = key + ".bmp";
            entry["jsonFile"] = key + ".json";
            index[key] = entry;
        }

        std::string indexPath = outputDir + "/composite_index.json";
        std::ofstream indexOut(indexPath);
        if (indexOut.is_open()) {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "  ";
            std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
            writer->write(index, &indexOut);
            indexOut << std::endl;
        }
        std::cout << "\nIndex: " << indexPath << std::endl;
    }

    // Summary
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "  Models processed: " << successCount << " succeeded, "
              << failCount << " failed" << std::endl;

    if (!allResults.empty()) {
        size_t totalSheetPixels = 0;
        for (const auto& r : allResults) {
            totalSheetPixels += r.sheetW * r.sheetH;
        }
        std::cout << "  Total sheet area: " << totalSheetPixels / 1024 << " Kpixels" << std::endl;
        std::cout << "  ETC1 estimate: " << totalSheetPixels / 2 / 1024 << " KB" << std::endl;
    }

    return failCount > 0 ? 1 : 0;
}

