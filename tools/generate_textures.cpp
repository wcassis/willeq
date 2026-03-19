// Offline texture generator for WillEQ procedural textures.
// Generates all texture assets as BMP files so the runtime
// can load them via the constrained texture cache.
//
// Usage: generate_textures [options]
//   -o, --output <dir>        Output directory (default: data/textures)
//   --cloud-size <N>          Cloud texture size (default: 256)
//   --cloud-frames <N>        Number of cloud frames (default: 4)
//   --cloud-octaves <N>       Perlin noise octaves (default: 4)
//   --cloud-persistence <F>   Noise persistence (default: 0.5)
//   -h, --help                Show help

#include <irrlicht.h>
#include <iostream>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "client/graphics/texture_generators.h"
#include "client/graphics/detail/detail_texture_atlas.h"

using namespace irr;

static void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "  -o, --output <dir>        Output directory (default: data/textures)\n"
              << "  --cloud-size <N>          Cloud texture size (default: 256)\n"
              << "  --cloud-frames <N>        Number of cloud frames (default: 4)\n"
              << "  --cloud-octaves <N>       Perlin noise octaves (default: 4)\n"
              << "  --cloud-persistence <F>   Noise persistence (default: 0.5)\n"
              << "  -h, --help                Show help\n";
}

static void ensureDirectory(const std::string& path) {
    mkdir(path.c_str(), 0755);
}

int main(int argc, char* argv[]) {
    std::string outputDir = "data/textures";
    int cloudSize = 256;
    int cloudFrames = 4;
    int cloudOctaves = 4;
    float cloudPersistence = 0.5f;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (arg == "--cloud-size" && i + 1 < argc) {
            cloudSize = std::stoi(argv[++i]);
        } else if (arg == "--cloud-frames" && i + 1 < argc) {
            cloudFrames = std::stoi(argv[++i]);
        } else if (arg == "--cloud-octaves" && i + 1 < argc) {
            cloudOctaves = std::stoi(argv[++i]);
        } else if (arg == "--cloud-persistence" && i + 1 < argc) {
            cloudPersistence = std::stof(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // Create a minimal Irrlicht device (software renderer, no window needed)
    SIrrlichtCreationParameters params;
    params.DriverType = video::EDT_SOFTWARE;
    params.WindowSize = core::dimension2d<u32>(64, 64);
    params.Fullscreen = false;
    params.Stencilbuffer = false;
    params.Vsync = false;

    IrrlichtDevice* device = createDeviceEx(params);
    if (!device) {
        std::cerr << "ERROR: Failed to create Irrlicht device\n";
        return 1;
    }

    video::IVideoDriver* driver = device->getVideoDriver();
    if (!driver) {
        std::cerr << "ERROR: Failed to get video driver\n";
        device->drop();
        return 1;
    }

    // Ensure output directory exists
    // Create parent directories
    std::string parentDir = outputDir.substr(0, outputDir.rfind('/'));
    if (!parentDir.empty() && parentDir != outputDir) {
        ensureDirectory(parentDir);
    }
    ensureDirectory(outputDir);

    int generated = 0;
    int failed = 0;

    auto writeImage = [&](video::IImage* img, const std::string& filename) {
        if (!img) {
            std::cerr << "  FAIL: " << filename << " (generation returned null)\n";
            failed++;
            return;
        }
        std::string path = outputDir + "/" + filename;
        if (driver->writeImageToFile(img, path.c_str())) {
            auto dim = img->getDimension();
            std::cout << "  OK:   " << path << " (" << dim.Width << "x" << dim.Height << ")\n";
            generated++;
        } else {
            std::cerr << "  FAIL: " << path << " (write failed)\n";
            failed++;
        }
        img->drop();
    };

    std::cout << "Generating textures to " << outputDir << "/\n\n";

    // 1. Particle atlas
    std::cout << "Particle atlas...\n";
    writeImage(EQT::Graphics::generateParticleAtlas(driver), "particle_atlas.bmp");

    // 2. Creature atlas
    std::cout << "Creature atlas...\n";
    writeImage(EQT::Graphics::generateCreatureAtlas(driver), "creature_atlas.bmp");

    // 3. Tumbleweed texture
    std::cout << "Tumbleweed texture...\n";
    writeImage(EQT::Graphics::generateTumbleweedTexture(driver), "tumbleweed.bmp");

    // 4. Storm cloud blend frames (source frames + interpolated blends)
    {
        const int blendStepsPerPair = 8;
        int totalBlendFrames = cloudFrames * blendStepsPerPair;
        std::cout << "Storm cloud blend frames (" << totalBlendFrames << " total: "
                  << cloudFrames << " sources x " << blendStepsPerPair << " steps, "
                  << cloudSize << "x" << cloudSize << ")...\n";

        // Generate source frames first
        std::vector<video::IImage*> sourceImages;
        for (int i = 0; i < cloudFrames; ++i) {
            int seed = 12345 + i * 7919;
            video::IImage* img = EQT::Graphics::generateCloudFrame(
                driver, seed, cloudSize, cloudOctaves, cloudPersistence);
            sourceImages.push_back(img);
        }

        // Generate all blend frames
        for (int pair = 0; pair < cloudFrames; ++pair) {
            int nextPair = (pair + 1) % cloudFrames;
            for (int step = 0; step < blendStepsPerPair; ++step) {
                int frameIndex = pair * blendStepsPerPair + step;
                float t = static_cast<float>(step) / static_cast<float>(blendStepsPerPair);
                std::string filename = "storm_cloud_blend_" + std::to_string(frameIndex) + ".bmp";

                video::IImage* blended = EQT::Graphics::generateBlendedCloudFrame(
                    driver, sourceImages[pair], sourceImages[nextPair], t);
                writeImage(blended, filename);
            }
        }

        // Clean up source images
        for (auto* img : sourceImages) {
            if (img) img->drop();
        }
    }

    // 5. Spell particle fallback texture
    std::cout << "Spell particle fallback...\n";
    writeImage(EQT::Graphics::generateSpellParticleFallback(driver), "spell_particle_fallback.bmp");

    // 6. Software mouse cursor
    std::cout << "Software cursor...\n";
    writeImage(EQT::Graphics::generateSoftwareCursor(driver), "software_cursor.bmp");

    // 7. Detail object atlas
    std::cout << "Detail object atlas...\n";
    {
        EQT::Graphics::Detail::DetailTextureAtlas atlasGenerator;
        video::ITexture* tex = atlasGenerator.createAtlas(driver);
        if (tex) {
            video::IImage* img = driver->createImage(tex, core::position2d<s32>(0, 0),
                                                      tex->getOriginalSize());
            if (img) {
                std::string path = outputDir + "/detail_atlas.bmp";
                if (driver->writeImageToFile(img, path.c_str())) {
                    auto dim = img->getDimension();
                    std::cout << "  OK:   " << path << " (" << dim.Width << "x" << dim.Height << ")\n";
                    generated++;
                } else {
                    std::cerr << "  FAIL: " << path << " (write failed)\n";
                    failed++;
                }
                img->drop();
            } else {
                std::cerr << "  FAIL: detail_atlas.bmp (createImage from texture failed)\n";
                failed++;
            }
        } else {
            std::cerr << "  FAIL: detail_atlas.bmp (createAtlas returned null)\n";
            failed++;
        }
    }

    // 8. Rain textures
    std::cout << "Rain textures...\n";
    for (int intensity = 1; intensity <= 10; ++intensity) {
        int numStreaks = 20 + (intensity - 1) * (100 - 20) / 9;
        char filename[64];
        snprintf(filename, sizeof(filename), "rain_intensity_%02d.bmp", intensity);
        writeImage(EQT::Graphics::generateRainTexture(driver, numStreaks, 100 + intensity, 30, 70), filename);
    }
    writeImage(EQT::Graphics::generateRainTexture(driver, 40, 200, 20, 50), "rain_layer_mid.bmp");
    writeImage(EQT::Graphics::generateRainTexture(driver, 35, 201, 10, 35), "rain_layer_far.bmp");
    writeImage(EQT::Graphics::generateRainTexture(driver, 50, 42, 10, 40), "rain_streaks.bmp");

    // 9. Snow textures
    std::cout << "Snow textures...\n";
    for (int intensity = 1; intensity <= 10; ++intensity) {
        int density = 50 + (intensity - 1) * 60;
        float minSz = 1.0f + (intensity - 1) * 0.1f;
        float maxSz = 3.0f + (intensity - 1) * 0.2f;
        int alpha = 140 + intensity * 8;
        char filename[64];
        snprintf(filename, sizeof(filename), "snow_intensity_%02d.bmp", intensity);
        writeImage(EQT::Graphics::generateSnowTexture(driver, density, 42 + density, minSz, maxSz, alpha), filename);
    }
    writeImage(EQT::Graphics::generateSnowTexture(driver, 200, 242, 1.5f, 3.5f, 160), "snow_layer_mid.bmp");
    writeImage(EQT::Graphics::generateSnowTexture(driver, 150, 292, 0.8f, 2.0f, 120), "snow_layer_far.bmp");
    writeImage(EQT::Graphics::generateSnowTexture(driver, 300, 342, 1.0f, 3.0f, 180), "snow_flakes.bmp");

    std::cout << "\nDone: " << generated << " generated, " << failed << " failed.\n";

    device->drop();
    return failed > 0 ? 1 : 0;
}
