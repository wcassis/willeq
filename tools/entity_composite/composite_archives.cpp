// tools/entity_composite/composite_archives.cpp -- global and zone chr archive discovery, the
// cross-zone signature/collision registry and the composite index merge.
// Moved from tools/entity_composite_builder.cpp by T385 (board B167).
// Bodies are verbatim; the only edits are the ones the split forces --
// `static` dropped from the functions that now cross a TU boundary (they
// are declared in composite_builder.h) and the whole file wrapped in
// namespace ecb.

#include "entity_composite/composite_builder.h"

namespace ecb {

// ── Archive scanning ────────────────────────────────────────────────────────

// Global chr archive names
static const char* GLOBAL_CHR_ARCHIVES[] = {
    "global_chr.s3d",
    "global2_chr.s3d",
    "global3_chr.s3d",
    "global4_chr.s3d",
    "global5_chr.s3d",
    "global6_chr.s3d",
    "global7_chr.s3d",
    nullptr
};

// struct ArchiveModels moved to entity_composite/composite_builder.h

 std::vector<ArchiveModels> loadGlobalArchives(const std::string& eqPath,
                                                      const std::string& filterRace,
                                                      bool verbose) {
    std::vector<ArchiveModels> results;

    for (int i = 0; GLOBAL_CHR_ARCHIVES[i]; ++i) {
        std::string archiveName = GLOBAL_CHR_ARCHIVES[i];
        std::string archivePath = eqPath + "/" + archiveName;

        if (!fs::exists(archivePath)) continue;

        if (verbose) std::cerr << "Loading " << archiveName << "..." << std::endl;

        S3DLoader loader;
        if (!loader.loadZone(archivePath)) {
            if (verbose) std::cerr << "  Could not load " << archiveName << std::endl;
            continue;
        }

        auto zone = loader.getZone();
        if (!zone || zone->characters.empty()) continue;

        ArchiveModels am;
        am.archiveName = archiveName;
        am.archivePath = archivePath;

        for (const auto& character : zone->characters) {
            if (!character) continue;

            // Filter by race if specified
            if (!filterRace.empty()) {
                std::string upperName = toUpper(character->name);
                std::string upperFilter = toUpper(filterRace);
                // Model names are like "HUM", "ELF", "DWF", etc.
                // Match if the model name starts with the filter
                if (upperName.find(upperFilter) == std::string::npos) continue;
            }

            // Only include models that have actual geometry parts
            bool hasParts = false;
            for (const auto& part : character->parts) {
                if (part && !part->vertices.empty()) { hasParts = true; break; }
            }
            if (!hasParts) continue;

            am.models.push_back(character);
        }

        if (!am.models.empty()) {
            if (verbose) {
                std::cerr << "  Found " << am.models.size() << " models" << std::endl;
            }
            results.push_back(std::move(am));
        }
    }

    return results;
}

// ── P08f: zone-archive support ──────────────────────────────────────────────
//
// entity_composite_builder originally saw only the 7 hardcoded global*_chr.s3d
// archives (GLOBAL_CHR_ARCHIVES above), so zone-native NPC models (the
// CAZ/INN/BEA-shaped majority of a typical dungeon/city roster) were
// structurally unbuildable — not a missing sweep, the tool literally never
// opened the file. This mirrors RaceModelLoader's own zone-archive access
// (RaceModelLoader::loadZoneModels, model_loading.cpp:140 — a SINGLE
// `{zone}_chr.s3d` file, no numbered/sibling archives for zone-specific
// content; confirmed by reading the runtime loader rather than assumed) —
// zone models live in exactly one file per zone.
//
// Sheet naming/keying is intentionally UNCHANGED from the global path
// (`{racename}_0_{variant}`, no zone qualifier) because that is what the
// runtime looks up: RaceModelLoader::preloadModelData
// (race_model_loader.cpp:807-810) builds `compositeBaseName = modelName +
// "_0_" + textureVariant` from the model's race name alone and never
// consults the zone name — confirmed by reading the runtime code, not
// assumed. This is exactly why name collisions between a zone model and a
// global (or another zone's) model of the same race name matter: writing
// both would mean one silently clobbers the other's sheet on disk.

// Loads character models from a SINGLE archive file (global or zone). Returns
// std::nullopt if the archive doesn't exist or has no usable characters —
// callers treat that as "not present for this zone", not an error.
static std::optional<ArchiveModels> tryLoadArchiveModels(const std::string& archivePath,
                                                           const std::string& archiveName,
                                                           const std::string& filterRace,
                                                           bool verbose) {
    if (!fs::exists(archivePath)) return std::nullopt;

    if (verbose) std::cerr << "Loading " << archiveName << "..." << std::endl;

    S3DLoader loader;
    if (!loader.loadZone(archivePath)) {
        if (verbose) std::cerr << "  Could not load " << archiveName << std::endl;
        return std::nullopt;
    }

    auto zone = loader.getZone();
    if (!zone || zone->characters.empty()) return std::nullopt;

    ArchiveModels am;
    am.archiveName = archiveName;
    am.archivePath = archivePath;

    for (const auto& character : zone->characters) {
        if (!character) continue;

        if (!filterRace.empty()) {
            std::string upperName = toUpper(character->name);
            std::string upperFilter = toUpper(filterRace);
            if (upperName.find(upperFilter) == std::string::npos) continue;
        }

        bool hasParts = false;
        for (const auto& part : character->parts) {
            if (part && !part->vertices.empty()) { hasParts = true; break; }
        }
        if (!hasParts) continue;

        am.models.push_back(character);
    }

    if (am.models.empty()) return std::nullopt;

    if (verbose) std::cerr << "  Found " << am.models.size() << " models" << std::endl;
    return am;
}

// Opens the single zone-specific character archive, mirroring
// RaceModelLoader::loadZoneModels exactly (one file, `{zone}_chr.s3d`, no
// numbered siblings — zone chr archives are never split like the globals are).
 std::optional<ArchiveModels> loadZoneArchive(const std::string& eqPath,
                                                     const std::string& zoneShortName,
                                                     bool verbose) {
    std::string archiveName = zoneShortName + "_chr.s3d";
    std::string archivePath = eqPath + "/" + archiveName;
    return tryLoadArchiveModels(archivePath, archiveName, "", verbose);
}

// Builds a per-model "content signature" — the sorted set of body-part
// texture names a model's triangles actually reference (pre-variant,
// pre-helm-filter — deliberately the rawest available discriminator so two
// models are only judged identical if their geometry/texture usage truly
// matches). Used for A2's collision detection: two archives producing a
// model with the SAME race name are the same model only if this matches.
 std::vector<std::string> collectSignature(const std::shared_ptr<CharacterModel>& model) {
    std::set<std::string> names;
    for (const auto& part : model->parts) {
        if (!part || part->vertices.empty() || part->triangles.empty()) continue;
        const auto& texNames = part->textureNames();
        for (const auto& tri : part->triangles) {
            if (tri.textureIndex >= texNames.size()) continue;
            names.insert(toLower(texNames[tri.textureIndex]));
        }
    }
    return std::vector<std::string>(names.begin(), names.end());
}

// struct RegisteredModel moved to entity_composite/composite_builder.h

// Registers every model from `archives` into `registry` keyed by uppercase
// race name — used to seed the collision registry with the global set before
// any zone archive is processed (plan A2).
 void registerArchives(const std::vector<ArchiveModels>& archives,
                              std::map<std::string, RegisteredModel>& registry) {
    for (const auto& am : archives) {
        for (const auto& model : am.models) {
            std::string key = toUpper(model->name);
            if (registry.count(key)) continue;  // first writer wins within this pass
            registry[key] = RegisteredModel{am.archiveName, collectSignature(model)};
        }
    }
}

// P08f plan A4: discover the texture-variant set that actually exists for a
// model in its archive, instead of a fixed 0-4 sweep (which — for zone
// models not curated the way the 42 global defs were — would silently
// rebuild N duplicate copies of variant 0 whenever a higher variant number
// isn't actually present; applyVariant() falls back to the base texture
// when the variant file is missing, so a naive sweep can't tell the
// difference between "distinct variant" and "no such variant").
// Variant v is considered to exist if ANY of the model's resolved body-part
// textures has a distinct v-suffixed file in the archive.
 std::vector<int> discoverVariants(PfsArchive& archive,
                                          const std::shared_ptr<CharacterModel>& model,
                                          int maxVariant) {
    std::set<std::string> baseTexNames;
    for (const auto& part : model->parts) {
        if (!part || part->vertices.empty() || part->triangles.empty()) continue;
        const auto& texNames = part->textureNames();
        for (const auto& tri : part->triangles) {
            if (tri.textureIndex >= texNames.size()) continue;
            baseTexNames.insert(toLower(texNames[tri.textureIndex]));
        }
    }

    std::vector<int> variants = {0};
    for (int v = 1; v <= maxVariant; ++v) {
        bool found = false;
        for (const auto& name : baseTexNames) {
            std::string variantName = applyVariant(name, v);
            if (variantName == name) continue;  // applyVariant no-op (name too short etc.)
            std::vector<char> testData;
            if (archive.get(variantName, testData)) { found = true; break; }
        }
        if (found) variants.push_back(v);
    }
    return variants;
}

// Whole-model animated-texture probe, checked once per model (independent of
// variant — the animation flag lives on the base material, not the variant
// substitution) so the zone-mode driver can skip a model's entire variant
// set in one shot instead of re-discovering it per variant.
 bool detectAnimatedTexture(const std::shared_ptr<CharacterModel>& model,
                                   std::string& outTextureName) {
    for (const auto& part : model->parts) {
        if (!part || part->vertices.empty() || part->triangles.empty()) continue;
        const auto& texNames = part->textureNames();
        const auto& texAnims = part->textureAnimations();
        for (const auto& tri : part->triangles) {
            if (tri.textureIndex >= texNames.size()) continue;
            if (tri.textureIndex < texAnims.size() && texAnims[tri.textureIndex].isAnimated) {
                outTextureName = toLower(texNames[tri.textureIndex]);
                return true;
            }
        }
    }
    return false;
}

// Merges newly-built index entries into any pre-existing composite_index.json
// in outputDir, WITHOUT touching existing keys (the 210 global entries this
// unit must leave byte-identical) — zone-mode additions are always new keys
// by construction (the collision registry guarantees no key this function
// writes was already registered under a matching or colliding signature).
 void mergeCompositeIndex(const std::string& outputDir,
                                 const std::vector<ModelResult>& newResults) {
    std::string indexPath = outputDir + "/composite_index.json";
    Json::Value index(Json::objectValue);

    std::ifstream existing(indexPath);
    if (existing.is_open()) {
        Json::CharReaderBuilder rb;
        std::string errs;
        if (!Json::parseFromStream(rb, existing, &index, &errs)) {
            std::cerr << "WARNING: could not parse existing " << indexPath
                      << " (" << errs << ") — starting a fresh index" << std::endl;
            index = Json::Value(Json::objectValue);
        }
    }

    for (const auto& r : newResults) {
        std::string key = toLower(r.raceName) + "_" + std::to_string(r.gender)
                        + "_" + std::to_string(r.variant);
        if (index.isMember(key)) continue;  // never overwrite (collision registry should prevent this)
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

    std::ofstream indexOut(indexPath);
    if (indexOut.is_open()) {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
        writer->write(index, &indexOut);
        indexOut << std::endl;
    }
}

// Persists the zone-sourced portion of the collision registry to a sidecar
// file in outputDir, and loads it back on a later run. This closes the gap
// a purely in-memory, single-process registry would otherwise leave: two
// SEPARATE tool invocations (e.g. golden zones today, --all-zones next
// week) must still detect a collision between a model built in an earlier
// run and one found in a later run — A2 requires this regardless of how
// many processes it takes to build the full roster.
static const char* SIGNATURE_REGISTRY_FILENAME = "p08f_zone_signature_registry.json";

 void loadPersistedRegistry(const std::string& outputDir,
                                   std::map<std::string, RegisteredModel>& registry) {
    std::string path = outputDir + "/" + SIGNATURE_REGISTRY_FILENAME;
    std::ifstream in(path);
    if (!in.is_open()) return;

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    if (!Json::parseFromStream(rb, in, &root, &errs)) {
        std::cerr << "WARNING: could not parse " << path << " (" << errs
                  << ") — ignoring persisted collision registry" << std::endl;
        return;
    }

    for (const auto& key : root.getMemberNames()) {
        if (registry.count(key)) continue;  // global entries always win
        RegisteredModel rm;
        rm.sourceArchive = root[key]["source"].asString();
        for (const auto& t : root[key]["signature"]) rm.signature.push_back(t.asString());
        registry[key] = rm;
    }
}

 void savePersistedRegistry(const std::string& outputDir,
                                   const std::map<std::string, RegisteredModel>& registry,
                                   const std::map<std::string, RegisteredModel>& globalOnly) {
    std::string path = outputDir + "/" + SIGNATURE_REGISTRY_FILENAME;
    Json::Value root(Json::objectValue);
    for (const auto& [key, rm] : registry) {
        if (globalOnly.count(key)) continue;  // global entries are reproducible, don't persist
        Json::Value entry;
        entry["source"] = rm.sourceArchive;
        Json::Value sig(Json::arrayValue);
        for (const auto& t : rm.signature) sig.append(t);
        entry["signature"] = sig;
        root[key] = entry;
    }
    std::ofstream out(path);
    if (out.is_open()) {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
        writer->write(root, &out);
        out << std::endl;
    }
}

// Reads docs/reference/pre_luclin_zones.json's flattened list of short_name
// values (all expansions' zones concatenated) — the --all-zones roster.
 std::vector<std::string> loadZoneShortNames(const std::string& zonesJsonPath) {
    std::vector<std::string> names;
    std::ifstream in(zonesJsonPath);
    if (!in.is_open()) return names;

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    if (!Json::parseFromStream(rb, in, &root, &errs)) {
        std::cerr << "ERROR: could not parse " << zonesJsonPath << ": " << errs << std::endl;
        return names;
    }

    const Json::Value& expansions = root["expansions"];
    for (const auto& expName : expansions.getMemberNames()) {
        const Json::Value& zones = expansions[expName]["zones"];
        for (const auto& z : zones) {
            if (z.isMember("short_name")) names.push_back(z["short_name"].asString());
        }
    }
    return names;
}


}  // namespace ecb
