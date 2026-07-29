// WLD Fragment 0x1B/0x28/0x2A/0x35 (Light) tests.
// Split from test_wld.cpp (T11, 2026-07-29) — pure mechanical move.

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <tuple>
#include <cmath>
#include <iostream>
#include <map>

#include "wld_test_fixtures.h"

class WldLightFragmentTest : public WldIntegrationTest {
protected:
    // Fragment 0x1B flags (matches eqsage LightFlags)
    static constexpr uint32_t LIGHT_FLAG_HAS_CURRENT_FRAME = 0x01;
    static constexpr uint32_t LIGHT_FLAG_HAS_SLEEP = 0x02;
    static constexpr uint32_t LIGHT_FLAG_HAS_LIGHT_LEVELS = 0x04;
    static constexpr uint32_t LIGHT_FLAG_SKIP_FRAMES = 0x08;
    static constexpr uint32_t LIGHT_FLAG_HAS_COLOR = 0x10;

    // Analyze Fragment 0x1B (Light Source Definition)
    struct LightSourceStats {
        int count = 0;
        int withCurrentFrame = 0;
        int withSleep = 0;
        int withLightLevels = 0;
        int withSkipFrames = 0;
        int withColor = 0;
        std::vector<uint32_t> allFlags;
        std::vector<uint32_t> frameCounts;
    };

    LightSourceStats analyzeLightSources(const std::vector<char>& wldBuffer) {
        LightSourceStats stats;

        if (wldBuffer.size() < sizeof(WldHeader)) {
            return stats;
        }

        const WldHeader* header = reinterpret_cast<const WldHeader*>(wldBuffer.data());
        size_t idx = sizeof(WldHeader) + header->hashLength;

        for (uint32_t i = 0; i < header->fragmentCount && idx < wldBuffer.size(); ++i) {
            if (idx + 8 > wldBuffer.size()) break;

            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx]);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx + 4]);
            idx += 8;

            if (fragType == 0x1B && fragSize >= 12) {
                const char* fragBody = &wldBuffer[idx + 4];  // Skip nameRef
                uint32_t flags = *reinterpret_cast<const uint32_t*>(fragBody);
                uint32_t frameCount = *reinterpret_cast<const uint32_t*>(fragBody + 4);

                stats.count++;
                stats.allFlags.push_back(flags);
                stats.frameCounts.push_back(frameCount);

                if (flags & LIGHT_FLAG_HAS_CURRENT_FRAME) stats.withCurrentFrame++;
                if (flags & LIGHT_FLAG_HAS_SLEEP) stats.withSleep++;
                if (flags & LIGHT_FLAG_HAS_LIGHT_LEVELS) stats.withLightLevels++;
                if (flags & LIGHT_FLAG_SKIP_FRAMES) stats.withSkipFrames++;
                if (flags & LIGHT_FLAG_HAS_COLOR) stats.withColor++;
            }

            idx += fragSize;
        }

        return stats;
    }

    // Analyze Fragment 0x28 (Light Instance)
    struct LightInstanceStats {
        int count = 0;
        std::vector<float> radii;
        float minRadius = 1e9f;
        float maxRadius = 0.0f;
    };

    LightInstanceStats analyzeLightInstances(const std::vector<char>& wldBuffer) {
        LightInstanceStats stats;

        if (wldBuffer.size() < sizeof(WldHeader)) {
            return stats;
        }

        const WldHeader* header = reinterpret_cast<const WldHeader*>(wldBuffer.data());
        size_t idx = sizeof(WldHeader) + header->hashLength;

        for (uint32_t i = 0; i < header->fragmentCount && idx < wldBuffer.size(); ++i) {
            if (idx + 8 > wldBuffer.size()) break;

            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx]);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx + 4]);
            idx += 8;

            if (fragType == 0x28 && fragSize >= 24) {
                // Structure: nameRef(4) + ref(4) + flags(4) + x(4) + y(4) + z(4) + radius(4)
                const char* fragBody = &wldBuffer[idx + 4];  // Skip nameRef
                // Skip ref(4) + flags(4) + x(4) + y(4) + z(4)
                float radius = *reinterpret_cast<const float*>(fragBody + 4 + 4 + 12);

                stats.count++;
                stats.radii.push_back(radius);
                stats.minRadius = std::min(stats.minRadius, radius);
                stats.maxRadius = std::max(stats.maxRadius, radius);
            }

            idx += fragSize;
        }

        return stats;
    }

    // Analyze Fragment 0x2A (Ambient Light Region)
    struct AmbientLightStats {
        int count = 0;
        int totalRegions = 0;
        std::vector<int> regionCounts;
    };

    AmbientLightStats analyzeAmbientLights(const std::vector<char>& wldBuffer) {
        AmbientLightStats stats;

        if (wldBuffer.size() < sizeof(WldHeader)) {
            return stats;
        }

        const WldHeader* header = reinterpret_cast<const WldHeader*>(wldBuffer.data());
        size_t idx = sizeof(WldHeader) + header->hashLength;

        for (uint32_t i = 0; i < header->fragmentCount && idx < wldBuffer.size(); ++i) {
            if (idx + 8 > wldBuffer.size()) break;

            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx]);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx + 4]);
            idx += 8;

            if (fragType == 0x2A && fragSize >= 12) {
                const char* fragBody = &wldBuffer[idx + 4];  // Skip nameRef
                // uint32_t flags = *reinterpret_cast<const uint32_t*>(fragBody);
                uint32_t regionCount = *reinterpret_cast<const uint32_t*>(fragBody + 4);

                stats.count++;
                stats.totalRegions += regionCount;
                stats.regionCounts.push_back(regionCount);
            }

            idx += fragSize;
        }

        return stats;
    }

    // Analyze Fragment 0x35 (Global Ambient Light)
    struct GlobalAmbientStats {
        int count = 0;
        std::vector<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>> colors;  // R, G, B, A
    };

    GlobalAmbientStats analyzeGlobalAmbient(const std::vector<char>& wldBuffer) {
        GlobalAmbientStats stats;

        if (wldBuffer.size() < sizeof(WldHeader)) {
            return stats;
        }

        const WldHeader* header = reinterpret_cast<const WldHeader*>(wldBuffer.data());
        size_t idx = sizeof(WldHeader) + header->hashLength;

        for (uint32_t i = 0; i < header->fragmentCount && idx < wldBuffer.size(); ++i) {
            if (idx + 8 > wldBuffer.size()) break;

            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx]);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx + 4]);
            idx += 8;

            if (fragType == 0x35 && fragSize >= 8) {
                const char* fragBody = &wldBuffer[idx + 4];  // Skip nameRef
                // eqsage reads BGRA order
                uint8_t b = static_cast<uint8_t>(fragBody[0]);
                uint8_t g = static_cast<uint8_t>(fragBody[1]);
                uint8_t r = static_cast<uint8_t>(fragBody[2]);
                uint8_t a = static_cast<uint8_t>(fragBody[3]);

                stats.count++;
                stats.colors.push_back({r, g, b, a});
            }

            idx += fragSize;
        }

        return stats;
    }
};

// Test Fragment 0x1B flag constants match eqsage
TEST_F(WldLightFragmentTest, Fragment1B_FlagConstants) {
    // eqsage LightFlags:
    // HasCurrentFrame = 0x01
    // HasSleep = 0x02
    // HasLightLevels = 0x04
    // SkipFrames = 0x08
    // HasColor = 0x10
    EXPECT_EQ(LIGHT_FLAG_HAS_CURRENT_FRAME, 0x01u);
    EXPECT_EQ(LIGHT_FLAG_HAS_SLEEP, 0x02u);
    EXPECT_EQ(LIGHT_FLAG_HAS_LIGHT_LEVELS, 0x04u);
    EXPECT_EQ(LIGHT_FLAG_SKIP_FRAMES, 0x08u);
    EXPECT_EQ(LIGHT_FLAG_HAS_COLOR, 0x10u);
}

// Test Fragment 0x1B structure
TEST_F(WldLightFragmentTest, Fragment1B_Structure) {
    // eqsage LightSource reads (after nameRef):
    // flags (4) + frameCount (4) = 8 bytes minimum header
    // [if hasCurrentFrame] currentFrame (4)
    // [if hasSleep] sleep (4)
    // [if hasLightLevels] frameCount x float32
    // [if hasColor] frameCount x 3 x float32

    // willeq's WldFragment1BHeader now correctly represents just the header
    // Variable fields (currentFrame, sleep, lightLevels, colors) are parsed separately
    EXPECT_EQ(sizeof(WldFragment1BHeader), 8u);  // flags + frameCount (minimal header)
}

// Test Fragment 0x28 structure
TEST_F(WldLightFragmentTest, Fragment28_Structure) {
    // eqsage LightInstance reads (after nameRef + ref):
    // flags (4) + position (12) + radius (4) = 20 bytes

    // willeq's WldFragment28Header
    EXPECT_EQ(sizeof(WldFragment28Header), 20u);  // flags + x + y + z + radius
}

// Test willeq uses wrong flag for color in Fragment 0x1B
TEST_F(WldLightFragmentTest, Fragment1B_WilleqFlagDifference) {
    // KEY DIFFERENCE IDENTIFIED:
    // willeq checks: flags & (1 << 3) = flags & 0x08
    // eqsage checks: flags & 0x10 (HasColor)
    //
    // willeq is checking the wrong flag!
    // 0x08 = SkipFrames in eqsage
    // 0x10 = HasColor in eqsage

    uint32_t willeqColorFlag = (1 << 3);  // 0x08
    uint32_t eqsageColorFlag = LIGHT_FLAG_HAS_COLOR;  // 0x10

    EXPECT_EQ(willeqColorFlag, 0x08u);
    EXPECT_EQ(eqsageColorFlag, 0x10u);
    EXPECT_NE(willeqColorFlag, eqsageColorFlag);  // They're different!
}

// Test Fragment 0x1B with real zone lights.wld
TEST_F(WldLightFragmentTest, Fragment1B_RealZoneLights) {
    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons"};

    for (const auto& zone : zones) {
        std::string zonePath = eqClientPath_ + "/" + zone + ".s3d";

        if (!fileExists(zonePath)) {
            continue;
        }

        // Lights are typically in lights.wld inside the zone archive
        std::vector<char> wldBuffer;
        if (!loadWldFromArchive(zonePath, "lights.wld", wldBuffer)) {
            // Some zones may not have lights.wld
            continue;
        }

        LightSourceStats stats = analyzeLightSources(wldBuffer);

        std::cout << "Fragment 0x1B Statistics for " << zone << "/lights.wld:\n";
        std::cout << "  Total light sources: " << stats.count << "\n";
        std::cout << "  With HasCurrentFrame (0x01): " << stats.withCurrentFrame << "\n";
        std::cout << "  With HasSleep (0x02): " << stats.withSleep << "\n";
        std::cout << "  With HasLightLevels (0x04): " << stats.withLightLevels << "\n";
        std::cout << "  With SkipFrames (0x08): " << stats.withSkipFrames << "\n";
        std::cout << "  With HasColor (0x10): " << stats.withColor << "\n";

        // Show unique flag values
        std::map<uint32_t, int> flagCounts;
        for (uint32_t f : stats.allFlags) {
            flagCounts[f]++;
        }
        std::cout << "  Unique flag values:\n";
        for (const auto& [flag, count] : flagCounts) {
            std::cout << "    0x" << std::hex << flag << std::dec << ": " << count << "\n";
        }

        // Verify willeq's flag assumption
        int withWilleqColorFlag = 0;
        for (uint32_t f : stats.allFlags) {
            if (f & 0x08) withWilleqColorFlag++;  // willeq's flag
        }
        if (stats.withColor != withWilleqColorFlag) {
            std::cout << "  ** FLAG MISMATCH: willeq would find " << withWilleqColorFlag
                      << " lights with color, eqsage finds " << stats.withColor << "\n";
        }
    }
}

// Test Fragment 0x28 with real zone lights.wld
TEST_F(WldLightFragmentTest, Fragment28_RealZoneLights) {
    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons"};

    for (const auto& zone : zones) {
        std::string zonePath = eqClientPath_ + "/" + zone + ".s3d";

        if (!fileExists(zonePath)) {
            continue;
        }

        std::vector<char> wldBuffer;
        if (!loadWldFromArchive(zonePath, "lights.wld", wldBuffer)) {
            continue;
        }

        LightInstanceStats stats = analyzeLightInstances(wldBuffer);

        std::cout << "Fragment 0x28 Statistics for " << zone << "/lights.wld:\n";
        std::cout << "  Total light instances: " << stats.count << "\n";
        if (stats.count > 0) {
            std::cout << "  Radius range: " << stats.minRadius << " - " << stats.maxRadius << "\n";
        }

        EXPECT_GE(stats.count, 0);  // Lights may or may not exist
    }
}

// Test Fragment 0x2A (Ambient Light Region) - not implemented in willeq
TEST_F(WldLightFragmentTest, Fragment2A_RealZoneLights) {
    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons"};

    for (const auto& zone : zones) {
        std::string zonePath = eqClientPath_ + "/" + zone + ".s3d";

        if (!fileExists(zonePath)) {
            continue;
        }

        std::vector<char> wldBuffer;
        if (!loadWldFromArchive(zonePath, "lights.wld", wldBuffer)) {
            continue;
        }

        AmbientLightStats stats = analyzeAmbientLights(wldBuffer);

        std::cout << "Fragment 0x2A Statistics for " << zone << "/lights.wld:\n";
        std::cout << "  Total ambient light entries: " << stats.count << "\n";
        std::cout << "  Total regions referenced: " << stats.totalRegions << "\n";

        // Note: willeq does NOT implement Fragment 0x2A
        if (stats.count > 0) {
            std::cout << "  ** NOTE: willeq does not implement Fragment 0x2A (Ambient Light Region)\n";
        }
    }
}

// Test Fragment 0x35 (Global Ambient Light) - not implemented in willeq
TEST_F(WldLightFragmentTest, Fragment35_RealZoneLights) {
    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons"};

    for (const auto& zone : zones) {
        std::string zonePath = eqClientPath_ + "/" + zone + ".s3d";

        if (!fileExists(zonePath)) {
            continue;
        }

        std::vector<char> wldBuffer;
        if (!loadWldFromArchive(zonePath, "lights.wld", wldBuffer)) {
            continue;
        }

        GlobalAmbientStats stats = analyzeGlobalAmbient(wldBuffer);

        std::cout << "Fragment 0x35 Statistics for " << zone << "/lights.wld:\n";
        std::cout << "  Total global ambient entries: " << stats.count << "\n";

        for (const auto& [r, g, b, a] : stats.colors) {
            std::cout << "  Color: R=" << static_cast<int>(r)
                      << " G=" << static_cast<int>(g)
                      << " B=" << static_cast<int>(b)
                      << " A=" << static_cast<int>(a) << "\n";
        }

        // Note: willeq does NOT implement Fragment 0x35
        if (stats.count > 0) {
            std::cout << "  ** NOTE: willeq does not implement Fragment 0x35 (Global Ambient Light)\n";
        }
    }
}

// Test WldLoader light loading
TEST_F(WldLightFragmentTest, WldLoader_LightData) {
    std::string zonePath = eqClientPath_ + "/qeynos2.s3d";

    if (!fileExists(zonePath)) {
        GTEST_SKIP() << "EQ client files not found at " << eqClientPath_;
    }

    // Load the lights.wld file
    WldLoader loader;
    if (!loader.parseFromArchive(zonePath, "lights.wld")) {
        GTEST_SKIP() << "Could not parse lights.wld";
    }

    const auto& lights = loader.getLights();
    std::cout << "WldLoader light data from qeynos2/lights.wld:\n";
    std::cout << "  Lights loaded: " << lights.size() << "\n";

    if (!lights.empty()) {
        // Sample some lights
        int sampleCount = std::min(static_cast<int>(lights.size()), 5);
        std::cout << "  First " << sampleCount << " lights:\n";
        for (int i = 0; i < sampleCount; ++i) {
            const auto& light = lights[i];
            std::cout << "    [" << i << "] pos=(" << light->x << "," << light->y << "," << light->z
                      << ") color=(" << light->r << "," << light->g << "," << light->b
                      << ") radius=" << light->radius << "\n";

            // Verify light data is reasonable
            EXPECT_FALSE(std::isnan(light->x) || std::isnan(light->y) || std::isnan(light->z))
                << "NaN position in light " << i;
            EXPECT_GE(light->r, 0.0f) << "Negative red in light " << i;
            EXPECT_GE(light->g, 0.0f) << "Negative green in light " << i;
            EXPECT_GE(light->b, 0.0f) << "Negative blue in light " << i;
            EXPECT_GE(light->radius, 0.0f) << "Negative radius in light " << i;
        }
    }
}

// Scan many zones to find Fragment 0x2A and 0x35
TEST_F(WldLightFragmentTest, ScanZonesForAmbientLights) {
    // Pre-Luclin zones (Original EQ, Kunark, Velious) + Luclin + PoP
    std::vector<std::string> zones = {
        // Original EQ cities
        "qeynos", "qeynos2", "qcat", "qrg", "surefall", "halas",
        "freportn", "freportw", "freporte", "commons", "ecommons",
        "nektulos", "lavastorm", "nro", "sro", "oasis", "innothule",
        "grobb", "oggok", "feerrott", "cazicthule", "guktop", "gukbottom",
        "akanon", "steamfont", "lfaydark", "gfaydark", "crushbone", "kaladima", "kaladimb",
        "felwithea", "felwitheb", "unrest", "mistmoore", "kedge",
        "paineel", "erudsxing", "erudnext", "erudnint", "tox", "kerraridge",
        "hole", "highkeep", "kithicor", "rivervale", "misty", "runnyeye",
        "eastkarana", "northkarana", "southkarana", "lakerathe", "rathe",
        "najena", "lavastorm", "soldungb", "permafrost", "everfrost",
        "blackburrow", "befallen", "qeytoqrg", "highpass", "highpasshold",
        "butcher", "oot", "cauldron", "estate", "paw",
        // Kunark
        "timorous", "firiona", "overthere", "swampofnohope", "warslikswood",
        "frontiermtns", "dreadlands", "burningwood", "skyfire", "lakeofillomen",
        "cabwest", "cabeast", "fieldofbone", "kurnscave", "kaesora",
        "charasis", "karnor", "sebilis", "trakanon", "veeshan", "dalnir",
        "chardok", "nurga", "droga",
        // Velious
        "thurgadina", "thurgadinb", "greatdivide", "wakening", "eastwastes",
        "cobaltscar", "sirens", "westwastes", "kael", "velketor",
        "crystal", "necropolis", "templeveeshan", "sleeper", "iceclad",
        "growthplane", "mischiefplane",
        // Luclin (Shadows of Luclin)
        "sseru", "ssratemple", "nexus", "bazaar", "echo", "scarlet",
        "paludal", "fungusgrove", "dawnshroud", "netherbian", "hollowshade",
        "acrylia", "shadeweaver", "umbral", "akheva", "vexthal", "sseru",
        "thedeep", "griegsend", "shadowhaven", "mseru", "sanctus",
        // Planes of Power
        "ponightmare", "potranquility", "postorms", "poair", "poeartha",
        "poearthb", "pofire", "powater", "povalor", "poinnovation",
        "podisease", "pojustice", "potorment", "potimea", "potimeb",
        "codecay", "hohonora", "hohonorb", "solrotower"
    };

    std::cout << "\n=== Scanning zones for Fragment 0x2A and 0x35 ===\n";

    int zonesWithLights = 0;
    int zonesWithAmbient2A = 0;
    int zonesWithGlobal35 = 0;
    std::vector<std::string> zonesHaving2A;
    std::vector<std::string> zonesHaving35;

    for (const auto& zone : zones) {
        std::string zonePath = eqClientPath_ + "/" + zone + ".s3d";

        if (!fileExists(zonePath)) {
            continue;
        }

        // Check both lights.wld and zone.wld
        std::vector<std::string> wldFiles = {"lights.wld", zone + ".wld"};

        for (const auto& wldFile : wldFiles) {
            std::vector<char> wldBuffer;
            if (!loadWldFromArchive(zonePath, wldFile, wldBuffer)) {
                continue;
            }

            if (wldFile == "lights.wld") zonesWithLights++;

            AmbientLightStats ambientStats = analyzeAmbientLights(wldBuffer);
            GlobalAmbientStats globalStats = analyzeGlobalAmbient(wldBuffer);

            if (ambientStats.count > 0) {
                zonesWithAmbient2A++;
                zonesHaving2A.push_back(zone + "/" + wldFile);
                std::cout << "  " << zone << "/" << wldFile << ": Fragment 0x2A count=" << ambientStats.count
                          << " regions=" << ambientStats.totalRegions << "\n";
            }

            if (globalStats.count > 0) {
                zonesWithGlobal35++;
                zonesHaving35.push_back(zone + "/" + wldFile);
                std::cout << "  " << zone << "/" << wldFile << ": Fragment 0x35 count=" << globalStats.count;
                for (const auto& [r, g, b, a] : globalStats.colors) {
                    std::cout << " color=(" << (int)r << "," << (int)g << "," << (int)b << "," << (int)a << ")";
                }
                std::cout << "\n";
            }
        }
    }

    std::cout << "\n=== Summary ===\n";
    std::cout << "  Zones with lights.wld: " << zonesWithLights << "\n";
    std::cout << "  Zones with Fragment 0x2A (Ambient Light Region): " << zonesWithAmbient2A << "\n";
    std::cout << "  Zones with Fragment 0x35 (Global Ambient Light): " << zonesWithGlobal35 << "\n";

    if (!zonesHaving2A.empty()) {
        std::cout << "  Zones with 0x2A: ";
        for (const auto& z : zonesHaving2A) std::cout << z << " ";
        std::cout << "\n";
    }

    if (!zonesHaving35.empty()) {
        std::cout << "  Zones with 0x35: ";
        for (const auto& z : zonesHaving35) std::cout << z << " ";
        std::cout << "\n";
    }
}
