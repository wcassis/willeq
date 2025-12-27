// Analyze HUM (Human Male) model structure from global_chr.s3d
// No graphics required - just dumps model structure info

#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <iomanip>
#include <functional>

#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include "common/logging.h"

using namespace EQT::Graphics;

int main(int argc, char* argv[]) {
    std::string clientPath = "/home/user/projects/claude/EverQuestP1999/";
    std::string targetModel = "HUM";  // Human Male

    if (argc > 1) {
        clientPath = argv[1];
        if (clientPath.back() != '/') clientPath += '/';
    }
    if (argc > 2) {
        targetModel = argv[2];
    }

    std::string s3dPath = clientPath + "global_chr.s3d";
    std::cout << "=== Analyzing " << targetModel << " model from global_chr.s3d ===\n\n";

    // Open the archive directly to inspect WLD contents
    PfsArchive archive;
    if (!archive.open(s3dPath)) {
        LOG_ERROR(MOD_MAIN, "Failed to open {}", s3dPath);
        return 1;
    }

    // Load WLD file
    WldLoader wld;
    if (!wld.parseFromArchive(s3dPath, "global_chr.wld")) {
        LOG_ERROR(MOD_MAIN, "Failed to parse global_chr.wld");
        return 1;
    }

    // Get all data from WLD
    const auto& skeletons = wld.getSkeletonTracks();
    const auto& geometries = wld.getGeometries();
    const auto& modelRefs = wld.getModelRefs();

    std::cout << "WLD Contents:\n";
    std::cout << "  Skeletons: " << skeletons.size() << "\n";
    std::cout << "  Geometries (meshes): " << geometries.size() << "\n";
    std::cout << "  Model References (0x2D): " << modelRefs.size() << "\n\n";

    // Find the target skeleton
    std::shared_ptr<SkeletonTrack> targetSkeleton = nullptr;
    uint32_t targetFragIdx = 0;

    std::cout << "=== All Skeletons ===\n";
    for (const auto& [fragIdx, skel] : skeletons) {
        std::string skelName = skel->name;
        std::transform(skelName.begin(), skelName.end(), skelName.begin(), ::toupper);

        // Extract base name (remove _HS_DEF suffix)
        std::string baseName = skelName;
        size_t hsDefPos = baseName.find("_HS_DEF");
        if (hsDefPos != std::string::npos) {
            baseName = baseName.substr(0, hsDefPos);
        }

        std::cout << "  [" << fragIdx << "] " << skel->name << " (base: " << baseName << ")";

        if (baseName == targetModel) {
            targetSkeleton = skel;
            targetFragIdx = fragIdx;
            std::cout << " <-- TARGET";
        }
        std::cout << "\n";
    }
    std::cout << "\n";

    if (!targetSkeleton) {
        LOG_ERROR(MOD_MAIN, "Target skeleton '{}' not found!", targetModel);
        return 1;
    }

    // Analyze target skeleton's bone hierarchy
    std::cout << "=== " << targetSkeleton->name << " Bone Hierarchy ===\n";
    std::cout << "Total bones in allBones: " << targetSkeleton->allBones.size() << "\n";
    std::cout << "Root bones: " << targetSkeleton->bones.size() << "\n\n";

    // Track which model references are used by this skeleton
    std::set<uint32_t> usedModelRefs;

    std::function<void(const std::shared_ptr<SkeletonBone>&, int)> printBone;
    printBone = [&](const std::shared_ptr<SkeletonBone>& bone, int depth) {
        if (!bone) return;

        std::string indent(depth * 2, ' ');
        std::cout << indent << "├── " << bone->name;

        if (bone->modelRef > 0) {
            std::cout << " [modelRef=" << bone->modelRef << "]";
            usedModelRefs.insert(bone->modelRef);

            // Try to find what this modelRef points to
            auto it = modelRefs.find(bone->modelRef);
            if (it != modelRefs.end()) {
                std::cout << " -> geomFragRef=" << it->second.geometryFragRef;

                // Find the geometry
                auto geom = wld.getGeometryByFragmentIndex(it->second.geometryFragRef);
                if (geom) {
                    std::cout << " -> MESH: " << geom->name
                              << " (V:" << geom->vertices.size()
                              << " T:" << geom->triangles.size() << ")";
                }
            } else {
                // modelRef might be direct geometry reference
                auto geom = wld.getGeometryByFragmentIndex(bone->modelRef);
                if (geom) {
                    std::cout << " -> DIRECT MESH: " << geom->name
                              << " (V:" << geom->vertices.size()
                              << " T:" << geom->triangles.size() << ")";
                }
            }
        }
        std::cout << "\n";

        for (const auto& child : bone->children) {
            printBone(child, depth + 1);
        }
    };

    for (const auto& rootBone : targetSkeleton->bones) {
        printBone(rootBone, 0);
    }

    std::cout << "\nTotal modelRefs used by skeleton: " << usedModelRefs.size() << "\n\n";

    // List all geometries and check which ones match the target race code
    std::cout << "=== All Geometries (Meshes) ===\n";
    std::vector<std::shared_ptr<ZoneGeometry>> matchingGeoms;
    std::vector<std::shared_ptr<ZoneGeometry>> otherGeoms;

    for (const auto& geom : geometries) {
        if (!geom) continue;

        std::string geomName = geom->name;
        std::transform(geomName.begin(), geomName.end(), geomName.begin(), ::toupper);

        // Check if geometry name starts with target race code
        bool matches = geomName.find(targetModel) != std::string::npos;

        if (matches) {
            matchingGeoms.push_back(geom);
        } else {
            otherGeoms.push_back(geom);
        }
    }

    std::cout << "Geometries matching '" << targetModel << "': " << matchingGeoms.size() << "\n";
    for (const auto& geom : matchingGeoms) {
        std::cout << "  + " << geom->name
                  << " (V:" << geom->vertices.size()
                  << " T:" << geom->triangles.size();
        if (!geom->vertexPieces.empty()) {
            std::cout << " VP:" << geom->vertexPieces.size();
        }
        std::cout << ")\n";

        // Show textures used
        if (!geom->textureNames.empty()) {
            std::cout << "    Textures: ";
            for (size_t i = 0; i < std::min(geom->textureNames.size(), size_t(5)); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << geom->textureNames[i];
            }
            if (geom->textureNames.size() > 5) {
                std::cout << " ... (" << geom->textureNames.size() << " total)";
            }
            std::cout << "\n";
        }
    }

    std::cout << "\nOther geometries (not matching '" << targetModel << "'): " << otherGeoms.size() << "\n";

    // Group other geometries by prefix
    std::map<std::string, int> prefixCounts;
    for (const auto& geom : otherGeoms) {
        std::string name = geom->name;
        std::transform(name.begin(), name.end(), name.begin(), ::toupper);
        std::string prefix = name.length() >= 3 ? name.substr(0, 3) : name;
        prefixCounts[prefix]++;
    }

    std::cout << "  By prefix:\n";
    for (const auto& [prefix, count] : prefixCounts) {
        std::cout << "    " << prefix << ": " << count << " meshes\n";
    }

    // Now use S3DLoader to see what it actually loads
    std::cout << "\n=== S3DLoader Results ===\n";
    S3DLoader loader;
    if (!loader.loadZone(s3dPath)) {
        LOG_ERROR(MOD_MAIN, "S3DLoader failed: {}", loader.getError());
        return 1;
    }

    const auto& characters = loader.getCharacters();
    std::cout << "S3DLoader found " << characters.size() << " character models\n\n";

    for (const auto& ch : characters) {
        std::string chName = ch->name;
        std::transform(chName.begin(), chName.end(), chName.begin(), ::toupper);

        // Extract base name
        std::string baseName = chName;
        size_t hsDefPos = baseName.find("_HS_DEF");
        if (hsDefPos != std::string::npos) {
            baseName = baseName.substr(0, hsDefPos);
        }

        if (baseName == targetModel) {
            std::cout << "*** " << ch->name << " ***\n";
            std::cout << "  Parts (legacy): " << ch->parts.size() << "\n";
            std::cout << "  PartsWithTransforms: " << ch->partsWithTransforms.size() << "\n";

            if (!ch->parts.empty()) {
                std::cout << "  Parts list:\n";
                size_t totalVerts = 0, totalTris = 0;
                for (size_t i = 0; i < ch->parts.size(); ++i) {
                    const auto& part = ch->parts[i];
                    std::cout << "    [" << i << "] " << part->name
                              << " V:" << part->vertices.size()
                              << " T:" << part->triangles.size() << "\n";
                    totalVerts += part->vertices.size();
                    totalTris += part->triangles.size();
                }
                std::cout << "  TOTAL: " << totalVerts << " vertices, " << totalTris << " triangles\n";
            }
            std::cout << "\n";
        }
    }

    // Summary
    std::cout << "=== SUMMARY ===\n";
    std::cout << "The '" << targetModel << "' skeleton has " << usedModelRefs.size() << " bone->modelRef values\n";
    std::cout << "There are " << matchingGeoms.size() << " meshes with names containing '" << targetModel << "'\n";
    std::cout << "\n";

    if (usedModelRefs.empty() && !matchingGeoms.empty()) {
        std::cout << "ISSUE: Skeleton bones have NO modelRef values!\n";
        std::cout << "This means the loader falls back to adding ALL geometries.\n";
        std::cout << "FIX: Filter meshes by name prefix matching the race code.\n";
    } else if (usedModelRefs.empty() && matchingGeoms.empty()) {
        std::cout << "ISSUE: No modelRefs AND no matching meshes by name.\n";
        std::cout << "This model may use a different naming convention.\n";
    }

    return 0;
}
