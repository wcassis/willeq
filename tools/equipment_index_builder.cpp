// equipment_index_builder — Build equipment model index from *equip.s3d archives
//
// Phase 1 of what WAS the equipment data pipeline:
//   Phase 1: Scan all *equip.s3d WLD files, extract IT model actor defs → JSON
//   Phase 2 (RETIRED, T456): willeq_db_builder used to import the JSON into
//   an `equipment_models` table; that table was never read and was dropped.
//   EquipmentModelLoader::buildEquipmentIndex() indexes the same archives
//   live at zone load. This tool's output feeds nothing -- board B480.
//
// Output JSON format:
// {
//   "models": {
//     "10728": {
//       "archive": "gatesequip.s3d",
//       "wld": "gatesequip.wld",
//       "actor": "IT10728",
//       "textures": ["it10728_throw_hatchet.dds"]
//     },
//     ...
//   },
//   "textures": {
//     "it10728_throw_hatchet.dds": {
//       "archive": "gatesequip.s3d",
//       "entry": "it10728_throw_hatchet.dds"
//     },
//     ...
//   }
// }
//
// Usage:
//   ./equipment_index_builder --eq-path /path/to/EQ --output data/equipment_models.json
//   ./equipment_index_builder --eq-path /path/to/EQ --output data/equipment_models.json --verbose

#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <string>
#include <vector>

using namespace EQT::Graphics;

// Minimal logging stubs — this tool runs offline, no need for full logging system
namespace EQT {
    int g_logLevel = 0;
    int g_logModuleMask = 0;
}

static bool g_verbose = false;

// Parse IT model ID from actor name (e.g., "IT156" -> 156)
static int parseModelId(const std::string& actorName) {
    std::regex pattern("IT(\\d+)");
    std::smatch match;
    if (std::regex_match(actorName, match, pattern)) {
        return std::stoi(match[1].str());
    }
    return -1;
}

struct ModelEntry {
    int modelId;
    std::string archive;    // filename only, e.g. "gatesequip.s3d"
    std::string wld;        // e.g. "gatesequip.wld"
    std::string actor;      // e.g. "IT10728"
    std::vector<std::string> textures;
};

struct TextureEntry {
    std::string archive;    // filename only
    std::string entry;      // original filename in archive
};

static bool indexArchive(const std::string& archivePath, const std::string& archiveName,
                         std::map<int, ModelEntry>& models,
                         std::map<std::string, TextureEntry>& textures) {
    PfsArchive archive;
    if (!archive.open(archivePath)) {
        if (g_verbose) fprintf(stderr, "  SKIP (not found): %s\n", archivePath.c_str());
        return false;
    }

    // Index texture filenames
    size_t texCount = 0;
    std::vector<std::string> texFiles;
    archive.getFilenames(".bmp", texFiles);

    std::vector<std::string> ddsFiles;
    if (archive.getFilenames(".dds", ddsFiles)) {
        texFiles.insert(texFiles.end(), ddsFiles.begin(), ddsFiles.end());
    }

    for (const auto& texName : texFiles) {
        std::string lowerName = texName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        if (textures.find(lowerName) == textures.end()) {
            textures[lowerName] = {archiveName, texName};
            texCount++;
        }
    }

    // Find and parse WLD files
    std::vector<std::string> wldFiles;
    archive.getFilenames(".wld", wldFiles);

    if (wldFiles.empty()) {
        if (g_verbose) fprintf(stderr, "  SKIP (no WLD): %s\n", archiveName.c_str());
        return false;
    }

    size_t modelCount = 0;
    for (const auto& wldName : wldFiles) {
        WldLoader wldLoader;
        if (!wldLoader.parseFromArchive(archivePath, wldName)) {
            fprintf(stderr, "  WARNING: Failed to parse WLD %s in %s\n", wldName.c_str(), archiveName.c_str());
            continue;
        }

        const auto& objectDefs = wldLoader.getObjectDefs();
        const auto& geometries = wldLoader.getGeometries();

        for (const auto& [actorName, objDef] : objectDefs) {
            int modelId = parseModelId(actorName);
            if (modelId < 0) continue;
            if (models.find(modelId) != models.end()) continue;  // first archive wins

            std::string prefix = "IT" + std::to_string(modelId);
            std::vector<std::string> modelTexNames;
            bool hasGeometry = false;

            for (const auto& geom : geometries) {
                if (geom && !geom->name.empty()) {
                    std::string geomName = geom->name;
                    std::transform(geomName.begin(), geomName.end(), geomName.begin(),
                                  [](unsigned char c) { return std::toupper(c); });
                    if (geomName.find(prefix + "_") == 0 || geomName.find(prefix + "_") != std::string::npos) {
                        hasGeometry = true;
                        for (const auto& texName : geom->textureNames()) {
                            bool found = false;
                            for (const auto& existing : modelTexNames) {
                                if (existing == texName) { found = true; break; }
                            }
                            if (!found) {
                                modelTexNames.push_back(texName);
                            }
                        }
                    }
                }
            }

            if (!hasGeometry) continue;

            models[modelId] = {modelId, archiveName, wldName, actorName, modelTexNames};
            modelCount++;
        }
    }

    printf("  %-25s  %4zu models, %4zu textures\n", archiveName.c_str(), modelCount, texCount);
    return true;
}

// Escape a string for JSON output
static std::string jsonEscape(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (c == '"') result += "\\\"";
        else if (c == '\\') result += "\\\\";
        else result += c;
    }
    return result;
}

static void writeJson(const std::string& outputPath,
                      const std::map<int, ModelEntry>& models,
                      const std::map<std::string, TextureEntry>& textures) {
    std::ofstream out(outputPath);
    if (!out.is_open()) {
        fprintf(stderr, "ERROR: Cannot write to %s\n", outputPath.c_str());
        return;
    }

    out << "{\n";

    // Models section
    out << "  \"models\": {\n";
    bool firstModel = true;
    for (const auto& [id, entry] : models) {
        if (!firstModel) out << ",\n";
        firstModel = false;

        out << "    \"" << id << "\": {\n";
        out << "      \"archive\": \"" << jsonEscape(entry.archive) << "\",\n";
        out << "      \"wld\": \"" << jsonEscape(entry.wld) << "\",\n";
        out << "      \"actor\": \"" << jsonEscape(entry.actor) << "\",\n";
        out << "      \"textures\": [";
        for (size_t i = 0; i < entry.textures.size(); i++) {
            if (i > 0) out << ", ";
            out << "\"" << jsonEscape(entry.textures[i]) << "\"";
        }
        out << "]\n";
        out << "    }";
    }
    out << "\n  },\n";

    // Textures section
    out << "  \"textures\": {\n";
    bool firstTex = true;
    for (const auto& [name, entry] : textures) {
        if (!firstTex) out << ",\n";
        firstTex = false;

        out << "    \"" << jsonEscape(name) << "\": {\n";
        out << "      \"archive\": \"" << jsonEscape(entry.archive) << "\",\n";
        out << "      \"entry\": \"" << jsonEscape(entry.entry) << "\"\n";
        out << "    }";
    }
    out << "\n  }\n";

    out << "}\n";
    out.close();
}

int main(int argc, char* argv[]) {
    std::string eqPath;
    std::string outputPath = "data/equipment_models.json";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--eq-path") == 0 && i + 1 < argc) {
            eqPath = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            g_verbose = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s --eq-path /path/to/EQ [--output file.json] [--verbose]\n", argv[0]);
            return 0;
        }
    }

    if (eqPath.empty()) {
        fprintf(stderr, "ERROR: --eq-path required\n");
        return 1;
    }

    if (!eqPath.empty() && eqPath.back() != '/') {
        eqPath += '/';
    }

    printf("Equipment Index Builder\n");
    printf("EQ path: %s\n", eqPath.c_str());
    printf("Output:  %s\n\n", outputPath.c_str());

    // All equipment archives — lower priority first, higher priority last for duplicates
    std::vector<std::string> archiveNames = {
        "obequip_lexit.s3d", "frogequip.s3d",
        "lgequip_amr2.s3d", "lgequip_amr.s3d",
        "loyequip.s3d", "vequip.s3d",
        "lgequip2.s3d", "lgequip.s3d",
        "ldonequip.s3d", "gatesequip.s3d",
        "gequip8.s3d", "gequip6.s3d", "gequip5.s3d", "gequip4.s3d",
        "gequip3.s3d", "gequip2.s3d", "gequip.s3d"
    };

    std::map<int, ModelEntry> models;
    std::map<std::string, TextureEntry> textures;

    printf("Scanning archives:\n");
    for (const auto& archiveName : archiveNames) {
        std::string archivePath = eqPath + archiveName;
        indexArchive(archivePath, archiveName, models, textures);
    }

    printf("\nTotal: %zu models, %zu textures\n", models.size(), textures.size());

    writeJson(outputPath, models, textures);
    printf("Written to %s\n", outputPath.c_str());

    return 0;
}
