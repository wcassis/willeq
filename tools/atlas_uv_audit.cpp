// Quick audit tool: parse qeynos2 zone WLD and report per-texture UV span statistics
// to show which textures pass/fail the atlas UV > 1.0 check.

#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <zone.s3d>\n", argv[0]);
        return 1;
    }

    std::string archivePath = argv[1];

    // Determine the WLD name from the archive name
    // e.g., qeynos2.s3d -> qeynos2.wld
    std::string baseName = archivePath;
    auto slash = baseName.rfind('/');
    if (slash != std::string::npos) baseName = baseName.substr(slash + 1);
    auto dot = baseName.rfind('.');
    if (dot != std::string::npos) baseName = baseName.substr(0, dot);
    std::string wldName = baseName + ".wld";

    EQT::Graphics::WldLoader wld;
    if (!wld.parseFromArchive(archivePath, wldName)) {
        fprintf(stderr, "Failed to parse %s from %s\n", wldName.c_str(), archivePath.c_str());
        return 1;
    }

    // Per-texture stats
    struct TexStats {
        std::string name;
        int totalTriangles = 0;
        int failTriangles = 0;   // UV span > 1.0
        float maxSpanU = 0;
        float maxSpanV = 0;
        bool isAnimated = false;
    };

    std::map<std::string, TexStats> stats;

    const auto& geometries = wld.getGeometries();
    printf("Zone has %zu geometry regions\n", geometries.size());

    int totalTris = 0;
    int totalFail = 0;

    for (const auto& geom : geometries) {
        if (!geom) continue;
        const auto& texNames = geom->textureNames();
        const auto& texAnims = geom->textureAnimations();
        const auto& verts = geom->vertices;
        const auto& tris = geom->triangles;

        for (size_t i = 0; i < tris.size(); ++i) {
            const auto& tri = tris[i];
            if (tri.textureIndex >= texNames.size()) continue;

            const std::string& texName = texNames[tri.textureIndex];
            auto& s = stats[texName];
            if (s.name.empty()) {
                s.name = texName;
                if (tri.textureIndex < texAnims.size())
                    s.isAnimated = texAnims[tri.textureIndex].isAnimated;
            }

            const auto& v0 = verts[tri.v1];
            const auto& v1 = verts[tri.v2];
            const auto& v2 = verts[tri.v3];

            float uMin = std::min(v0.u, std::min(v1.u, v2.u));
            float uMax = std::max(v0.u, std::max(v1.u, v2.u));
            float vMin = std::min(v0.v, std::min(v1.v, v2.v));
            float vMax = std::max(v0.v, std::max(v1.v, v2.v));

            float maxRelU = uMax - std::floor(uMin);
            float maxRelV = vMax - std::floor(vMin);

            s.totalTriangles++;
            totalTris++;

            s.maxSpanU = std::max(s.maxSpanU, maxRelU);
            s.maxSpanV = std::max(s.maxSpanV, maxRelV);

            if (maxRelU > 1.0f || maxRelV > 1.0f) {
                s.failTriangles++;
                totalFail++;
            }
        }
    }

    // Sort by name
    std::vector<TexStats> sorted;
    for (auto& [k, v] : stats) sorted.push_back(v);
    std::sort(sorted.begin(), sorted.end(), [](const TexStats& a, const TexStats& b) {
        return a.name < b.name;
    });

    printf("\n=== TEXTURES FULLY ATLASABLE (all triangles pass) ===\n");
    printf("%-30s %6s %10s %10s %s\n", "Texture", "Tris", "MaxSpanU", "MaxSpanV", "Animated?");
    printf("%-30s %6s %10s %10s %s\n", "-------", "----", "--------", "--------", "---------");
    int passCount = 0;
    for (const auto& s : sorted) {
        if (s.failTriangles == 0 && !s.isAnimated) {
            printf("%-30s %6d %10.3f %10.3f\n", s.name.c_str(), s.totalTriangles, s.maxSpanU, s.maxSpanV);
            passCount++;
        }
    }
    printf("  (%d textures)\n", passCount);

    printf("\n=== TEXTURES WITH SOME FAILING TRIANGLES (UV span > 1.0) ===\n");
    printf("%-30s %6s %6s %10s %10s %s\n", "Texture", "Total", "Fail", "MaxSpanU", "MaxSpanV", "Animated?");
    printf("%-30s %6s %6s %10s %10s %s\n", "-------", "-----", "----", "--------", "--------", "---------");
    int mixedCount = 0;
    for (const auto& s : sorted) {
        if (s.failTriangles > 0) {
            printf("%-30s %6d %6d %10.3f %10.3f %s\n",
                   s.name.c_str(), s.totalTriangles, s.failTriangles,
                   s.maxSpanU, s.maxSpanV,
                   s.isAnimated ? "ANIMATED" : "");
            mixedCount++;
        }
    }
    printf("  (%d textures)\n", mixedCount);

    printf("\n=== ANIMATED TEXTURES (excluded from atlas regardless of UV) ===\n");
    int animCount = 0;
    for (const auto& s : sorted) {
        if (s.isAnimated) {
            printf("  %-30s %6d tris, maxSpan U=%.3f V=%.3f\n",
                   s.name.c_str(), s.totalTriangles, s.maxSpanU, s.maxSpanV);
            animCount++;
        }
    }
    printf("  (%d textures)\n", animCount);

    printf("\n=== SUMMARY ===\n");
    printf("Total textures:    %zu\n", stats.size());
    printf("Fully atlasable:   %d\n", passCount);
    printf("Have failing tris: %d\n", mixedCount);
    printf("Animated:          %d\n", animCount);
    printf("Total triangles:   %d\n", totalTris);
    printf("Failing triangles: %d (%.1f%%)\n", totalFail, 100.0f * totalFail / totalTris);

    return 0;
}
