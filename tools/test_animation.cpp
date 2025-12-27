#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include "common/logging.h"
#include <iostream>
#include <string>

using namespace EQT::Graphics;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_global_chr.s3d>" << std::endl;
        return 1;
    }

    std::string archivePath = argv[1];
    std::cout << "Loading: " << archivePath << std::endl;

    // Extract the WLD filename from the archive path
    // e.g., global_chr.s3d -> global_chr.wld
    std::string wldName;
    size_t lastSlash = archivePath.find_last_of("/\\");
    std::string baseName = (lastSlash != std::string::npos) ? archivePath.substr(lastSlash + 1) : archivePath;
    size_t dotPos = baseName.find_last_of('.');
    if (dotPos != std::string::npos) {
        wldName = baseName.substr(0, dotPos) + ".wld";
    } else {
        wldName = baseName + ".wld";
    }
    std::cout << "Looking for WLD: " << wldName << std::endl;

    // Load with WldLoader to check track data
    WldLoader wldLoader;
    if (!wldLoader.parseFromArchive(archivePath, wldName)) {
        LOG_ERROR(MOD_MAIN, "Failed to load WLD from archive");
        return 1;
    }

    // Check track definitions
    const auto& trackDefs = wldLoader.getTrackDefs();
    std::cout << "\nTrack Definitions (Fragment 0x12): " << trackDefs.size() << std::endl;
    int count = 0;
    for (const auto& [fragIdx, trackDef] : trackDefs) {
        if (count < 20) {
            std::cout << "  [" << fragIdx << "] " << trackDef->name
                      << " - " << trackDef->frames.size() << " frames" << std::endl;
        }
        count++;
    }
    if (count > 20) {
        std::cout << "  ... and " << (count - 20) << " more" << std::endl;
    }

    // Check track references
    const auto& trackRefs = wldLoader.getTrackRefs();
    std::cout << "\nTrack References (Fragment 0x13): " << trackRefs.size() << std::endl;
    count = 0;
    for (const auto& [fragIdx, trackRef] : trackRefs) {
        if (count < 20) {
            std::cout << "  [" << fragIdx << "] " << trackRef->name;
            if (trackRef->isNameParsed) {
                std::cout << " -> anim='" << trackRef->animCode
                          << "' model='" << trackRef->modelCode
                          << "' bone='" << trackRef->boneName << "'";
            }
            std::cout << (trackRef->isPoseAnimation ? " (POSE)" : "");
            std::cout << std::endl;
        }
        count++;
    }
    if (count > 20) {
        std::cout << "  ... and " << (count - 20) << " more" << std::endl;
    }

    // Check skeleton tracks
    const auto& skeletonTracks = wldLoader.getSkeletonTracks();
    std::cout << "\nSkeleton Tracks (Fragment 0x10): " << skeletonTracks.size() << std::endl;
    for (const auto& [fragIdx, skeleton] : skeletonTracks) {
        std::cout << "  [" << fragIdx << "] " << skeleton->name
                  << " - " << skeleton->allBones.size() << " bones" << std::endl;
    }

    // Now load with S3DLoader to check character models
    std::cout << "\n--- Loading with S3DLoader ---" << std::endl;
    S3DLoader s3dLoader;
    if (!s3dLoader.loadZone(archivePath)) {
        LOG_ERROR(MOD_MAIN, "Failed to load zone");
        return 1;
    }

    auto zone = s3dLoader.getZone();
    if (!zone) {
        LOG_ERROR(MOD_MAIN, "No zone data");
        return 1;
    }

    std::cout << "\nCharacter Models: " << zone->characters.size() << std::endl;
    for (const auto& character : zone->characters) {
        std::cout << "  " << character->name << std::endl;
        if (character->animatedSkeleton) {
            std::cout << "    Animated Skeleton: " << character->animatedSkeleton->modelCode << std::endl;
            std::cout << "    Bones: " << character->animatedSkeleton->bones.size() << std::endl;
            std::cout << "    Animations: " << character->animatedSkeleton->animations.size() << std::endl;
            for (const auto& [animCode, anim] : character->animatedSkeleton->animations) {
                std::cout << "      " << animCode << ": " << anim->frameCount << " frames, "
                          << anim->animationTimeMs << "ms"
                          << (anim->isLooped ? " (looped)" : "") << std::endl;
            }
        } else {
            std::cout << "    NO animated skeleton!" << std::endl;
        }
    }

    return 0;
}
