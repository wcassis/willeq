// B733 byte-identity census (built-model side, FULL production path): for each
// race id, drive RaceModelLoader::ensureModelDataLoaded(raceId, gender) -- the
// same resolver + loader + selection + combine chain the client runs -- and
// print a FNV-1a hash of the exact combinedGeometry bytes (vertices, triangles,
// vertexColors) plus counts. Run against the HEAD build and the fixed build and
// diff: an identical hash means the built model is byte-for-byte identical.
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/willeq_database.h"
#include <irrlicht.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace {
uint64_t fnv1a(const unsigned char* data, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) { h ^= data[i]; h *= 1099511628211ULL; }
    return h;
}
// fold a segment into a running hash so different segment layouts don't collide
uint64_t fold(uint64_t h, uint64_t seg) {
    return h ^ (seg + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: %s <clientPath> <dbPath> <raceId> [raceId...]\n", argv[0]); return 1; }
    const std::string clientPath = argv[1];
    const std::string dbPath = argv[2];

    EQT::WilleqDatabase db;
    if (!db.open(dbPath)) { std::fprintf(stderr, "db open failed: %s\n", dbPath.c_str()); return 1; }
    EQT::WilleqDatabase::setGlobalInstance(&db);

    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_NULL;
    params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
    auto device = irr::createDeviceEx(params);
    if (!device) { std::fprintf(stderr, "no EDT_NULL device\n"); return 1; }

    auto cfg = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
        EQT::Graphics::ConstrainedRenderingPreset::TNT);
    auto tcache = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(cfg);
    tcache->setDriver(device->getVideoDriver());
    auto loader = std::make_unique<EQT::Graphics::RaceModelLoader>(
        device->getSceneManager(), device->getVideoDriver(), device->getFileSystem());
    loader->setClientPath(clientPath);
    loader->setConstrainedTextureCache(tcache.get());
    loader->getMeshBuilder()->setConstrainedTextureCache(tcache.get());

    for (int i = 3; i < argc; ++i) {
        const uint16_t rid = (uint16_t)std::atoi(argv[i]);
        for (uint8_t g = 0; g < 2; ++g) {
            auto model = loader->ensureModelDataLoaded(rid, g);
            if (!model) {
                std::printf("RACE %u g=%u NONE\n", (unsigned)rid, (unsigned)g);
                continue;
            }
            uint64_t h = 0; size_t nv = 0, nt = 0, nvc = 0;
            if (model->combinedGeometry) {
                const auto& gg = *model->combinedGeometry;
                nv = gg.vertices.size(); nt = gg.triangles.size(); nvc = gg.vertexColors.size();
                h = fold(h, fnv1a((const unsigned char*)gg.vertices.data(), gg.vertices.size() * sizeof(EQT::Graphics::Vertex3D)));
                h = fold(h, fnv1a((const unsigned char*)gg.triangles.data(), gg.triangles.size() * sizeof(EQT::Graphics::Triangle)));
                h = fold(h, fnv1a((const unsigned char*)gg.vertexColors.data(), gg.vertexColors.size() * sizeof(uint32_t)));
            }
            std::printf("RACE %u g=%u nv=%zu nt=%zu nvc=%zu h=%016llx\n",
                        (unsigned)rid, (unsigned)g, nv, nt, nvc, (unsigned long long)h);
        }
    }
    device->drop();
    return 0;
}
