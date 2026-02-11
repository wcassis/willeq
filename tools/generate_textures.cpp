// Offline texture generator for WillEQ procedural textures.
// Generates all texture atlases as PNG files so the runtime
// can load them directly instead of generating at startup.
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
    writeImage(EQT::Graphics::generateParticleAtlas(driver), "particle_atlas.png");

    // 2. Creature atlas
    std::cout << "Creature atlas...\n";
    writeImage(EQT::Graphics::generateCreatureAtlas(driver), "creature_atlas.png");

    // 3. Tumbleweed texture
    std::cout << "Tumbleweed texture...\n";
    writeImage(EQT::Graphics::generateTumbleweedTexture(driver), "tumbleweed.png");

    // 4. Storm cloud frames
    std::cout << "Storm cloud frames (" << cloudFrames << " x " << cloudSize << "x" << cloudSize << ")...\n";
    for (int i = 0; i < cloudFrames; ++i) {
        int seed = 12345 + i * 7919;
        std::string filename = "storm_cloud_" + std::to_string(i) + ".png";
        writeImage(EQT::Graphics::generateCloudFrame(driver, seed, cloudSize, cloudOctaves, cloudPersistence),
                   filename);
    }

    // 5. Detail object atlas
    std::cout << "Detail object atlas...\n";
    {
        EQT::Graphics::Detail::DetailTextureAtlas atlasGenerator;
        video::ITexture* tex = atlasGenerator.createAtlas(driver);
        if (tex) {
            // Lock texture to get image data, then write
            video::IImage* img = driver->createImage(tex, core::position2d<s32>(0, 0),
                                                      tex->getOriginalSize());
            if (img) {
                std::string path = outputDir + "/detail_atlas.png";
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
                std::cerr << "  FAIL: detail_atlas.png (createImage from texture failed)\n";
                failed++;
            }
        } else {
            std::cerr << "  FAIL: detail_atlas.png (createAtlas returned null)\n";
            failed++;
        }
    }

    std::cout << "\nDone: " << generated << " generated, " << failed << " failed.\n";

    device->drop();
    return failed > 0 ? 1 : 0;
}
