// S3D Archive Content Dumper
// Outputs a comprehensive tree view of all contents in an S3D archive
// Usage: s3d_dump [archive.s3d] [output.md]

#include <iostream>
#include <fstream>
#include <memory>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <iomanip>
#include <cstring>
#include <sstream>
#include <functional>

#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include "common/logging.h"

using namespace EQT::Graphics;

// Fragment type names
std::map<uint32_t, std::string> fragmentNames = {
    {0x03, "BitmapName"},
    {0x04, "BitmapInfo"},
    {0x05, "BitmapInfoRef"},
    {0x10, "SkeletonHierarchy"},
    {0x11, "SkeletonHierarchyRef"},
    {0x12, "TrackDef"},
    {0x13, "TrackDefRef"},
    {0x14, "Actor"},
    {0x15, "ActorInstance"},
    {0x2C, "LegacyMesh"},
    {0x2D, "MeshReference"},
    {0x30, "Material"},
    {0x31, "MaterialList"},
    {0x36, "Mesh"},
};

// Output stream (can be cout or file)
std::ostream* out = &std::cout;

std::string getFragmentName(uint32_t id) {
    auto it = fragmentNames.find(id);
    if (it != fragmentNames.end()) {
        return it->second;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "Unknown_0x%02X", id);
    return buf;
}

// Use the WldHeader and WldFragmentHeader from wld_loader.h
using EQT::Graphics::WldHeader;
using EQT::Graphics::WldFragmentHeader;

static const uint8_t HASH_KEY[] = { 0x95, 0x3A, 0xC5, 0x2A, 0x95, 0x7A, 0x95, 0x6A };

void decodeHash(char* str, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        str[i] ^= HASH_KEY[i % 8];
    }
}

struct FragmentInfo {
    uint32_t index;
    uint32_t type;
    std::string name;
    uint32_t size;
    std::vector<uint32_t> references;  // Other fragments this one references
};

void dumpWldContents(const std::string& archivePath, const std::string& wldName,
                     const std::vector<char>& buffer) {
    if (buffer.size() < sizeof(WldHeader)) {
        std::cout << "    [Invalid WLD - too small]" << std::endl;
        return;
    }

    const WldHeader* header = reinterpret_cast<const WldHeader*>(buffer.data());

    if (header->magic != 0x54503d02) {
        std::cout << "    [Invalid WLD magic]" << std::endl;
        return;
    }

    bool isOldFormat = (header->version == 0x00015500);
    std::cout << "    Format: " << (isOldFormat ? "Old (0x00015500)" : "New (0x1000C800)") << std::endl;
    std::cout << "    Fragments: " << header->fragmentCount << std::endl;
    std::cout << "    String hash length: " << header->hashLength << std::endl;

    // Decode string hash
    size_t idx = sizeof(WldHeader);
    std::vector<char> hashBuffer(header->hashLength);
    memcpy(hashBuffer.data(), &buffer[idx], header->hashLength);
    decodeHash(hashBuffer.data(), header->hashLength);
    idx += header->hashLength;

    // Count fragment types
    std::map<uint32_t, int> fragmentCounts;
    std::vector<FragmentInfo> fragments;

    // Collect skeleton names
    std::set<std::string> skeletonNames;
    std::set<std::string> meshNames;
    std::set<std::string> actorNames;
    std::map<std::string, std::vector<std::string>> skeletonMeshes;  // skeleton -> meshes

    size_t fragIdx = idx;
    for (uint32_t i = 0; i < header->fragmentCount; ++i) {
        if (fragIdx + sizeof(WldFragmentHeader) > buffer.size()) break;

        const WldFragmentHeader* fragHeader = reinterpret_cast<const WldFragmentHeader*>(&buffer[fragIdx]);

        FragmentInfo info;
        info.index = i + 1;
        info.type = fragHeader->id;
        info.size = fragHeader->size;

        // Get fragment name from string hash
        // nameRef is the first 4 bytes of fragment data (after the 8-byte header)
        // It's negative, negate to get offset into string hash
        const char* fragData = &buffer[fragIdx + sizeof(WldFragmentHeader)];
        if (fragHeader->size >= 4) {
            int32_t nameRef = *reinterpret_cast<const int32_t*>(fragData);
            if (nameRef != 0) {
                int32_t nameOffset = -nameRef;
                if (nameOffset > 0 && static_cast<size_t>(nameOffset) < header->hashLength) {
                    info.name = std::string(&hashBuffer[nameOffset]);
                }
            }
        }

        fragmentCounts[fragHeader->id]++;
        fragments.push_back(info);

        // Track specific fragment types
        if (fragHeader->id == 0x10) {
            skeletonNames.insert(info.name.empty() ? "(unnamed)" : info.name);
        } else if ((fragHeader->id == 0x36 || fragHeader->id == 0x2C)) {
            meshNames.insert(info.name.empty() ? "(unnamed)" : info.name);
        } else if (fragHeader->id == 0x14) {
            actorNames.insert(info.name.empty() ? "(unnamed)" : info.name);
        }

        fragIdx += sizeof(WldFragmentHeader) + fragHeader->size;
    }

    // Print fragment type summary
    std::cout << std::endl;
    std::cout << "    Fragment Types:" << std::endl;
    for (const auto& [type, count] : fragmentCounts) {
        std::cout << "      0x" << std::hex << std::setw(2) << std::setfill('0') << type
                  << std::dec << " " << getFragmentName(type) << ": " << count << std::endl;
    }

    // Print skeletons (character models)
    std::cout << std::endl;
    std::cout << "    Skeletons (Character Models): " << skeletonNames.size() << std::endl;
    for (const auto& name : skeletonNames) {
        // Extract model base name (remove _HS_DEF suffix)
        std::string baseName = name;
        size_t pos = baseName.find("_HS_DEF");
        if (pos != std::string::npos) {
            baseName = baseName.substr(0, pos);
        }
        std::cout << "      " << name << " (base: " << baseName << ")" << std::endl;
    }

    // Print meshes
    if (!meshNames.empty()) {
        std::cout << std::endl;
        std::cout << "    Meshes (" << meshNames.size() << " total):" << std::endl;

        // Group meshes by prefix (first 3 chars usually indicate the model)
        std::map<std::string, std::vector<std::string>> meshesByPrefix;
        for (const auto& name : meshNames) {
            std::string prefix;
            // Find the model prefix - usually 3 chars before body part indicator
            size_t underscorePos = name.find("_DMSPRITEDEF");
            if (underscorePos != std::string::npos) {
                std::string meshPart = name.substr(0, underscorePos);
                // Try to extract 3-char race prefix
                if (meshPart.length() >= 3) {
                    prefix = meshPart.substr(0, 3);
                } else {
                    prefix = meshPart;
                }
            } else {
                prefix = name.substr(0, 3);
            }
            std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::toupper);
            meshesByPrefix[prefix].push_back(name);
        }

        for (const auto& [prefix, meshList] : meshesByPrefix) {
            std::cout << "      [" << prefix << "] (" << meshList.size() << " meshes)" << std::endl;
            // Show first few meshes
            int shown = 0;
            for (const auto& mesh : meshList) {
                if (shown++ < 5) {
                    std::cout << "        - " << mesh << std::endl;
                }
            }
            if (meshList.size() > 5) {
                std::cout << "        ... and " << (meshList.size() - 5) << " more" << std::endl;
            }
        }
    }

    // Print actors
    if (!actorNames.empty()) {
        std::cout << std::endl;
        std::cout << "    Actors:" << std::endl;
        for (const auto& name : actorNames) {
            std::cout << "      " << name << std::endl;
        }
    }
}

// Structure to hold model info for summary
struct ModelSummary {
    std::string name;
    int boneCount = 0;
    int meshCount = 0;
    size_t vertexCount = 0;
    size_t triangleCount = 0;
    std::set<std::string> textures;
    std::vector<std::string> boneNames;
};

// Full model dump with bone hierarchy
void dumpModelWithHierarchy(const std::string& archivePath,
                            std::map<std::string, ModelSummary>& modelSummaries,
                            std::map<std::string, std::set<std::string>>& textureToModels) {
    S3DLoader loader;
    if (!loader.loadZone(archivePath)) {
        *out << "Failed to load S3D for model analysis\n";
        return;
    }

    const auto& characters = loader.getCharacters();
    *out << "\n## Character Models (" << characters.size() << " total)\n\n";

    for (const auto& model : characters) {
        if (!model) continue;

        ModelSummary summary;
        summary.name = model->name;

        *out << "### " << model->name << "\n\n";
        *out << "```\n";
        *out << model->name << "\n";

        if (model->skeleton) {
            // Print bone hierarchy with mesh attachments
            std::function<void(const std::shared_ptr<SkeletonBone>&, const std::string&, bool)> printBone;
            printBone = [&](const std::shared_ptr<SkeletonBone>& bone, const std::string& prefix, bool isLast) {
                if (!bone) return;

                summary.boneCount++;
                summary.boneNames.push_back(bone->name);

                std::string connector = isLast ? "└── " : "├── ";
                std::string childPrefix = prefix + (isLast ? "    " : "│   ");

                *out << prefix << connector << "BONE: " << bone->name;

                // Check for attached mesh
                if (bone->modelRef > 0) {
                    // Find the geometry for this bone
                    for (const auto& part : model->partsWithTransforms) {
                        if (part.geometry) {
                            // Match by checking if name matches
                            std::string boneNameUpper = bone->name;
                            std::transform(boneNameUpper.begin(), boneNameUpper.end(), boneNameUpper.begin(), ::toupper);

                            std::string geomName = part.geometry->name;
                            std::transform(geomName.begin(), geomName.end(), geomName.begin(), ::toupper);

                            if (geomName.find(boneNameUpper) != std::string::npos ||
                                boneNameUpper.find(geomName.substr(0, 5)) != std::string::npos) {
                                summary.meshCount++;
                                summary.vertexCount += part.geometry->vertices.size();
                                summary.triangleCount += part.geometry->triangles.size();

                                *out << "\n" << childPrefix << "└── MESH: " << part.geometry->name
                                     << " (V:" << part.geometry->vertices.size()
                                     << " T:" << part.geometry->triangles.size() << ")";

                                // List textures
                                for (const auto& tex : part.geometry->textureNames) {
                                    std::string texLower = tex;
                                    std::transform(texLower.begin(), texLower.end(), texLower.begin(), ::tolower);
                                    summary.textures.insert(texLower);
                                    textureToModels[texLower].insert(model->name);
                                }
                                break;
                            }
                        }
                    }
                }
                *out << "\n";

                for (size_t i = 0; i < bone->children.size(); ++i) {
                    printBone(bone->children[i], childPrefix, i == bone->children.size() - 1);
                }
            };

            for (size_t i = 0; i < model->skeleton->bones.size(); ++i) {
                printBone(model->skeleton->bones[i], "", i == model->skeleton->bones.size() - 1);
            }
        }

        // Also count from parts directly
        summary.meshCount = 0;
        summary.vertexCount = 0;
        summary.triangleCount = 0;
        for (const auto& part : model->parts) {
            if (part) {
                summary.meshCount++;
                summary.vertexCount += part->vertices.size();
                summary.triangleCount += part->triangles.size();
                for (const auto& tex : part->textureNames) {
                    std::string texLower = tex;
                    std::transform(texLower.begin(), texLower.end(), texLower.begin(), ::tolower);
                    summary.textures.insert(texLower);
                    textureToModels[texLower].insert(model->name);
                }
            }
        }

        *out << "```\n\n";
        *out << "- **Bones**: " << summary.boneCount << "\n";
        *out << "- **Mesh Parts**: " << summary.meshCount << "\n";
        *out << "- **Total Vertices**: " << summary.vertexCount << "\n";
        *out << "- **Total Triangles**: " << summary.triangleCount << "\n";
        *out << "- **Textures Used**: " << summary.textures.size() << "\n";

        if (!summary.textures.empty()) {
            *out << "  - ";
            bool first = true;
            for (const auto& t : summary.textures) {
                if (!first) *out << ", ";
                *out << "`" << t << "`";
                first = false;
            }
            *out << "\n";
        }
        *out << "\n";

        modelSummaries[model->name] = summary;
    }
}

int main(int argc, char* argv[]) {
    std::string archivePath = "/home/user/projects/claude/EverQuestP1999/global_chr.s3d";
    std::string outputPath = "";

    if (argc > 1) {
        archivePath = argv[1];
    }
    if (argc > 2) {
        outputPath = argv[2];
    }

    // Setup output stream
    std::ofstream fileOut;
    if (!outputPath.empty()) {
        fileOut.open(outputPath);
        if (fileOut) {
            out = &fileOut;
        } else {
            LOG_ERROR(MOD_MAIN, "Failed to open output file: {}", outputPath);
        }
    }

    // Extract archive name
    std::string archiveName = archivePath;
    size_t lastSlash = archivePath.rfind('/');
    if (lastSlash != std::string::npos) {
        archiveName = archivePath.substr(lastSlash + 1);
    }

    *out << "# " << archiveName << " - Complete Contents\n\n";
    *out << "Generated analysis of EverQuest S3D archive.\n\n";

    PfsArchive archive;
    if (!archive.open(archivePath)) {
        LOG_ERROR(MOD_MAIN, "Failed to open archive: {}", archivePath);
        return 1;
    }

    // Get all files in archive
    std::vector<std::string> wldFiles;
    std::vector<std::string> bmpFiles;
    std::vector<std::string> ddsFiles;
    std::vector<std::string> otherFiles;

    archive.getFilenames(".wld", wldFiles);
    archive.getFilenames(".bmp", bmpFiles);
    archive.getFilenames(".dds", ddsFiles);

    // Get all files and find others
    std::vector<std::string> allFiles;
    const auto& fileMap = archive.getFiles();
    for (const auto& [name, data] : fileMap) {
        allFiles.push_back(name);
    }
    std::sort(allFiles.begin(), allFiles.end());

    std::set<std::string> knownFiles;
    for (const auto& f : wldFiles) knownFiles.insert(f);
    for (const auto& f : bmpFiles) knownFiles.insert(f);
    for (const auto& f : ddsFiles) knownFiles.insert(f);

    for (const auto& f : allFiles) {
        if (knownFiles.find(f) == knownFiles.end()) {
            otherFiles.push_back(f);
        }
    }

    // Archive summary
    *out << "## Archive Summary\n\n";
    *out << "| Category | Count |\n";
    *out << "|----------|-------|\n";
    *out << "| WLD Files (Model Definitions) | " << wldFiles.size() << " |\n";
    *out << "| BMP Textures | " << bmpFiles.size() << " |\n";
    *out << "| DDS Textures | " << ddsFiles.size() << " |\n";
    *out << "| Other Files | " << otherFiles.size() << " |\n";
    *out << "| **Total Files** | **" << allFiles.size() << "** |\n\n";

    // WLD file analysis
    *out << "## WLD Files (Model Definitions)\n\n";
    for (const auto& wld : wldFiles) {
        *out << "### " << wld << "\n\n";

        std::vector<char> buffer;
        if (archive.get(wld, buffer)) {
            // Analyze WLD contents
            if (buffer.size() >= sizeof(WldHeader)) {
                const WldHeader* header = reinterpret_cast<const WldHeader*>(buffer.data());
                if (header->magic == 0x54503d02) {
                    bool isOldFormat = (header->version == 0x00015500);
                    *out << "- **Format**: " << (isOldFormat ? "Old (0x00015500)" : "New (0x1000C800)") << "\n";
                    *out << "- **Fragment Count**: " << header->fragmentCount << "\n";
                    *out << "- **String Hash Size**: " << header->hashLength << " bytes\n\n";

                    // Count fragment types
                    std::map<uint32_t, int> fragCounts;
                    size_t idx = sizeof(WldHeader) + header->hashLength;
                    for (uint32_t i = 0; i < header->fragmentCount && idx < buffer.size(); ++i) {
                        if (idx + sizeof(WldFragmentHeader) > buffer.size()) break;
                        const WldFragmentHeader* frag = reinterpret_cast<const WldFragmentHeader*>(&buffer[idx]);
                        fragCounts[frag->id]++;
                        idx += sizeof(WldFragmentHeader) + frag->size - 4;
                    }

                    *out << "**Fragment Types:**\n\n";
                    *out << "| Type | Name | Count |\n";
                    *out << "|------|------|-------|\n";
                    for (const auto& [type, count] : fragCounts) {
                        *out << "| 0x" << std::hex << std::setw(2) << std::setfill('0') << type << std::dec
                             << " | " << getFragmentName(type) << " | " << count << " |\n";
                    }
                    *out << "\n";
                }
            }
        }
    }

    // Model analysis with hierarchy
    std::map<std::string, ModelSummary> modelSummaries;
    std::map<std::string, std::set<std::string>> textureToModels;
    dumpModelWithHierarchy(archivePath, modelSummaries, textureToModels);

    // Model summary table
    if (!modelSummaries.empty()) {
        *out << "## Model Summary Table\n\n";
        *out << "| Model | Bones | Meshes | Vertices | Triangles | Textures |\n";
        *out << "|-------|-------|--------|----------|-----------|----------|\n";

        std::vector<std::string> modelNames;
        for (const auto& [name, _] : modelSummaries) {
            modelNames.push_back(name);
        }
        std::sort(modelNames.begin(), modelNames.end());

        size_t totalVerts = 0, totalTris = 0;
        for (const auto& name : modelNames) {
            const auto& m = modelSummaries[name];
            *out << "| " << m.name << " | " << m.boneCount << " | " << m.meshCount
                 << " | " << m.vertexCount << " | " << m.triangleCount
                 << " | " << m.textures.size() << " |\n";
            totalVerts += m.vertexCount;
            totalTris += m.triangleCount;
        }
        *out << "| **TOTAL** | - | - | **" << totalVerts << "** | **" << totalTris << "** | - |\n\n";
    }

    // Texture files
    *out << "## Texture Files\n\n";

    // Group textures by model prefix (first 3 chars)
    std::map<std::string, std::vector<std::string>> bmpByPrefix;
    std::map<std::string, std::vector<std::string>> ddsByPrefix;

    for (const auto& t : bmpFiles) {
        std::string prefix = t.length() >= 3 ? t.substr(0, 3) : t;
        std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::toupper);
        bmpByPrefix[prefix].push_back(t);
    }
    for (const auto& t : ddsFiles) {
        std::string prefix = t.length() >= 3 ? t.substr(0, 3) : t;
        std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::toupper);
        ddsByPrefix[prefix].push_back(t);
    }

    *out << "### BMP Textures (" << bmpFiles.size() << " total)\n\n";
    *out << "```\n";
    for (const auto& [prefix, texList] : bmpByPrefix) {
        *out << "├── [" << prefix << "] (" << texList.size() << " files)\n";
        for (size_t i = 0; i < texList.size(); ++i) {
            bool isLast = (i == texList.size() - 1);
            *out << "│   " << (isLast ? "└── " : "├── ") << texList[i];
            // Show which models use this texture
            if (textureToModels.count(texList[i]) && !textureToModels[texList[i]].empty()) {
                *out << " → used by: ";
                bool first = true;
                for (const auto& m : textureToModels[texList[i]]) {
                    if (!first) *out << ", ";
                    *out << m;
                    first = false;
                }
            }
            *out << "\n";
        }
    }
    *out << "```\n\n";

    if (!ddsFiles.empty()) {
        *out << "### DDS Textures (" << ddsFiles.size() << " total)\n\n";
        *out << "```\n";
        for (const auto& [prefix, texList] : ddsByPrefix) {
            *out << "├── [" << prefix << "] (" << texList.size() << " files)\n";
            for (size_t i = 0; i < texList.size(); ++i) {
                bool isLast = (i == texList.size() - 1);
                *out << "│   " << (isLast ? "└── " : "├── ") << texList[i];
                if (textureToModels.count(texList[i]) && !textureToModels[texList[i]].empty()) {
                    *out << " → used by: ";
                    bool first = true;
                    for (const auto& m : textureToModels[texList[i]]) {
                        if (!first) *out << ", ";
                        *out << m;
                        first = false;
                    }
                }
                *out << "\n";
            }
        }
        *out << "```\n\n";
    }

    // Find unused textures
    std::vector<std::string> unusedTextures;
    for (const auto& t : bmpFiles) {
        if (textureToModels.find(t) == textureToModels.end() || textureToModels[t].empty()) {
            unusedTextures.push_back(t);
        }
    }
    for (const auto& t : ddsFiles) {
        if (textureToModels.find(t) == textureToModels.end() || textureToModels[t].empty()) {
            unusedTextures.push_back(t);
        }
    }

    if (!unusedTextures.empty()) {
        *out << "### Unreferenced Textures (" << unusedTextures.size() << ")\n\n";
        *out << "These textures exist in the archive but weren't directly referenced by parsed models:\n\n";
        *out << "```\n";
        for (size_t i = 0; i < unusedTextures.size(); ++i) {
            bool isLast = (i == unusedTextures.size() - 1);
            *out << (isLast ? "└── " : "├── ") << unusedTextures[i] << "\n";
        }
        *out << "```\n\n";
    }

    // Full dependency tree
    *out << "## Full Dependency Tree\n\n";
    *out << "```\n";
    *out << archiveName << "\n";

    // WLD files with their models
    for (size_t w = 0; w < wldFiles.size(); ++w) {
        bool isLastWld = (w == wldFiles.size() - 1 && bmpFiles.empty() && ddsFiles.empty());
        *out << (isLastWld ? "└── " : "├── ") << wldFiles[w] << " [WLD]\n";

        std::string wldPrefix = isLastWld ? "    " : "│   ";

        // List models from this WLD
        std::vector<std::string> modelNames;
        for (const auto& [name, _] : modelSummaries) {
            modelNames.push_back(name);
        }
        std::sort(modelNames.begin(), modelNames.end());

        for (size_t m = 0; m < modelNames.size(); ++m) {
            const auto& summary = modelSummaries[modelNames[m]];
            bool isLastModel = (m == modelNames.size() - 1);
            *out << wldPrefix << (isLastModel ? "└── " : "├── ") << "MODEL: " << summary.name << "\n";

            std::string modelPrefix = wldPrefix + (isLastModel ? "    " : "│   ");

            // List textures for this model
            std::vector<std::string> texList(summary.textures.begin(), summary.textures.end());
            std::sort(texList.begin(), texList.end());

            for (size_t t = 0; t < texList.size(); ++t) {
                bool isLastTex = (t == texList.size() - 1);
                *out << modelPrefix << (isLastTex ? "└── " : "├── ") << texList[t] << "\n";
            }
        }
    }

    // Unreferenced textures at root level
    if (!unusedTextures.empty()) {
        *out << "│\n";
        *out << "└── UNREFERENCED TEXTURES (" << unusedTextures.size() << ")\n";
        for (size_t i = 0; i < unusedTextures.size(); ++i) {
            bool isLast = (i == unusedTextures.size() - 1);
            *out << "    " << (isLast ? "└── " : "├── ") << unusedTextures[i] << "\n";
        }
    }

    *out << "```\n";

    if (!outputPath.empty() && fileOut) {
        fileOut.close();
        std::cout << "Output written to: " << outputPath << std::endl;
    }

    return 0;
}
