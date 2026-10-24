// B1022: report every character model's native animation durations (frames x
// frameMs -> ms) for the S3D archive(s) given on the command line. Used to
// measure the model's own per-swing animation length, which is the constant
// the fixed swing path must play (the OP_Animation speed byte is a constant
// 10 = 1.0x, so the per-swing duration is the model's native animation time).
//
// No device, no DISPLAY, no server: the same loadZone() path s3d_dump uses.
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include "client/graphics/eq/s3d_loader.h"
#include "common/logging.h"

using namespace EQT::Graphics;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "usage: b1022_anim_probe <archive.s3d> [more.s3d ...]\n";
        return 64;
    }
    // The attack animations fable plays (animation_constants.h): c01..c11,
    // c10 swim-attack, t07/t08/t09. Also print o01 (idle) and l01 (walk) as
    // references. B1148: t04/t05/t06 are the cast tracks spell CastingAnim
    // 42/43/44 name (eqgame's id -> code table at 0x8ffbc8 + id * 20).
    static const std::vector<std::string> interesting = {
        "c01", "c02", "c03", "c04", "c05", "c06", "c07", "c08", "c09",
        "c10", "c11", "t04", "t05", "t06", "t07", "t08", "t09", "o01", "l01"
    };
    for (int a = 1; a < argc; ++a) {
        S3DLoader loader;
        std::cout << "\n=== " << argv[a] << " ===\n";
        if (!loader.loadZone(argv[a])) {
            std::cout << "  FAILED to load: " << loader.getError() << "\n";
            return 65;
        }
        const auto& chars = loader.getCharacters();
        std::cout << "  " << chars.size() << " character models\n";
        for (const auto& model : chars) {
            if (!model || !model->animatedSkeleton) continue;
            const auto& skel = model->animatedSkeleton;
            std::cout << "  model " << model->name
                      << " (skeleton " << skel->modelCode << ")\n";
            for (const auto& code : interesting) {
                auto it = skel->animations.find(code);
                if (it == skel->animations.end()) continue;
                const Animation& anim = *it->second;
                std::cout << "    " << code << "  frames=" << anim.frameCount
                          << "  timeMs=" << anim.animationTimeMs
                          << (anim.isLooped ? "  (looped)" : "") << "\n";
            }
        }
    }
    return 0;
}
