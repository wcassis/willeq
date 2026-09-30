// B947 (GitLab #265) -- every evictable texture has a restore source, so an
// eviction can never leave a holder with a freed ITexture*.
//
// B944 made eviction keep the texture object and restore its storage on the
// next bind, but only for entries with a source: EQE1 pack entries and the
// zone's own S3D archives. An S3D-path CHARACTER texture (RaceModelLoader's
// per-part decode from *_chr.s3d) had none, so its eviction still DESTROYED
// the object -- and RaceModelLoader's cached EQAnimatedMesh, which every
// entity of the race is built from, kept the freed pointer in its materials
// (B944's pre-fix ASan run read exactly that pointer in
// RaceModelLoader::createAnimatedNodeWithEquipment).
//
// These tests build a REAL cached animated character mesh on the S3D path
// under a real EDT_OGLES2 (surfaceless) driver, push the cache far below the
// working set, and assert that every texture the cached mesh holds is still
// the cache's and the driver's live object and re-binds to the SAME pixels
// (whole-frame byte compare). Around it: the EQE1 character-pack path
// (including a late-opened pack, T266), the plain-file origin the procedural
// textures use, the digest guard, pinning, and protected textures.
//
// Self-skips without EGL, the EQ client tree (data/eq_client), the shipped
// database or the EQE1 tree.

#include <gtest/gtest.h>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/archive_texture_source.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/equipment_model_loader.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eqe1_loader.h"
#include "client/willeq_database.h"
#include <irrlicht.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace EQT::Graphics;

namespace {

bool fileExists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

std::string clientPath() {
    const char* envPath = std::getenv("EQ_CLIENT_PATH");
    std::string p = envPath ? envPath : EQ_CLIENT_DATA_PATH;
    if (!p.empty() && p.back() != '/') p += '/';
    return p;
}

struct Rgb { uint8_t r, g, b; };

std::vector<char> makeBmp(int side, Rgb c) {
    const int rowSize = ((side * 3 + 3) / 4) * 4;
    const uint32_t pixelBytes = static_cast<uint32_t>(rowSize) * side;
    std::vector<char> bmp(54 + pixelBytes, 0);
    auto* p = reinterpret_cast<uint8_t*>(bmp.data());
    p[0] = 'B'; p[1] = 'M';
    const uint32_t fileSize = static_cast<uint32_t>(bmp.size());
    std::memcpy(p + 2, &fileSize, 4);
    const uint32_t dataOffset = 54, dibSize = 40;
    std::memcpy(p + 10, &dataOffset, 4);
    std::memcpy(p + 14, &dibSize, 4);
    const int32_t s = side;
    std::memcpy(p + 18, &s, 4);
    std::memcpy(p + 22, &s, 4);
    const uint16_t planes = 1, bpp = 24;
    std::memcpy(p + 26, &planes, 2);
    std::memcpy(p + 28, &bpp, 2);
    for (int y = 0; y < side; ++y) {
        uint8_t* row = p + 54 + static_cast<size_t>(y) * rowSize;
        for (int x = 0; x < side; ++x) {
            row[x * 3 + 0] = c.b;
            row[x * 3 + 1] = c.g;
            row[x * 3 + 2] = c.r;
        }
    }
    return bmp;
}

// Evictable fillers: served back by name (the zone-archive provider's shape).
class MapSource : public TextureSourceProvider {
public:
    std::map<std::string, std::vector<char>> bytes;
    bool hasSource(const std::string& name) override { return bytes.count(name) != 0; }
    bool readSource(const std::string& name, std::vector<char>& out) override {
        auto it = bytes.find(name);
        if (it == bytes.end()) return false;
        out = it->second;
        return true;
    }
};

class CountingListener : public TextureEvictionListener {
public:
    int calls = 0;
    void onTextureEvicted(const std::string&, irr::video::ITexture*) override { ++calls; }
};

// Registers a listener for one scope, so a failed ASSERT (which returns from
// the test body) cannot leave the cache calling a dead stack object.
struct ScopedListener {
    ConstrainedTextureCache& cache;
    CountingListener listener;
    explicit ScopedListener(ConstrainedTextureCache& c) : cache(c) { cache.addEvictionListener(&listener); }
    ~ScopedListener() { cache.removeEvictionListener(&listener); }
};

constexpr int kScreen = 64;

class CharacterRestoreFixture : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    MapSource fillers_;
    int fillerSerial_ = 0;

    bool makeDevice() {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(kScreen, kScreen);
        device_ = irr::createDeviceEx(params);
        if (device_) driver_ = device_->getVideoDriver();
        return device_ != nullptr;
    }

    void makeCache(size_t ceiling) {
        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        config.textureMemoryBytes = ceiling;
        config.enableMipmaps = false;
        config.maxTextureDimension = 256;
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        cache_->addSourceProvider(&fillers_);
    }

    void TearDown() override {
        if (cache_) cache_->removeSourceProvider(&fillers_);
        cache_.reset();
        if (device_) device_->drop();
    }

    // Upload evictable 256x256 fillers until `bytes` of them are resident:
    // pushes everything older out of a ceiling that is at most `bytes`.
    void flood(size_t bytes) {
        const size_t each = 256u * 256u * 4u;
        for (size_t done = 0; done < bytes; done += each) {
            const std::string name = "b947_filler" + std::to_string(fillerSerial_++) + ".bmp";
            fillers_.bytes[name] = makeBmp(256, {static_cast<uint8_t>(fillerSerial_ * 37), 90, 160});
            cache_->getOrLoad(name, fillers_.bytes[name], "test", "B947");
            ASSERT_NE(cache_->uploadTexture(name), nullptr) << name;
        }
    }

    // The whole framebuffer after drawing `tex` over all of it through the
    // driver's own bind path (which is where an evicted texture restores).
    std::vector<uint8_t> drawFrame(irr::video::ITexture* tex) {
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        const auto size = tex->getOriginalSize();
        driver_->draw2DImage(tex, irr::core::rect<irr::s32>(0, 0, kScreen, kScreen),
                             irr::core::rect<irr::s32>(0, 0, static_cast<irr::s32>(size.Width),
                                                       static_cast<irr::s32>(size.Height)),
                             nullptr, nullptr, false);
        std::vector<uint8_t> out;
        if (irr::video::IImage* shot = driver_->createScreenShot()) {
            for (int y = 0; y < kScreen; ++y) {
                for (int x = 0; x < kScreen; ++x) {
                    const irr::video::SColor c = shot->getPixel(x, y);
                    out.push_back(static_cast<uint8_t>(c.getRed()));
                    out.push_back(static_cast<uint8_t>(c.getGreen()));
                    out.push_back(static_cast<uint8_t>(c.getBlue()));
                }
            }
            shot->drop();
        }
        driver_->endScene();
        return out;
    }
};

// ---------------------------------------------------------------------------
// The holder B944's ASan run caught: RaceModelLoader's cached animated mesh.
// ---------------------------------------------------------------------------

class CachedCharacterMeshFixture : public CharacterRestoreFixture {
protected:
    EQT::WilleqDatabase db_;
    std::unique_ptr<RaceModelLoader> loader_;

    bool prepare(size_t ceiling) {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        const std::string eq = clientPath();
        if (!fileExists(dbPath) || !fileExists(eq + "global_chr.s3d")) return false;
        if (!db_.open(dbPath)) return false;
        EQT::WilleqDatabase::setGlobalInstance(&db_);
        if (!makeDevice()) return false;
        makeCache(ceiling);
        loader_ = std::make_unique<RaceModelLoader>(device_->getSceneManager(), driver_,
                                                    device_->getFileSystem());
        loader_->setClientPath(eq);
        loader_->setConstrainedTextureCache(cache_.get());
        loader_->getMeshBuilder()->setConstrainedTextureCache(cache_.get());
        cache_->setMeshBuilder(loader_->getMeshBuilder());
        cache_->setSceneManager(device_->getSceneManager());
        return true;
    }

    void TearDown() override {
        if (cache_) cache_->setMeshBuilder(nullptr);
        // The renderer's order: the loader (and its meshes) goes before the cache.
        loader_.reset();
        CharacterRestoreFixture::TearDown();
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }
};

TEST_F(CachedCharacterMeshFixture, CachedAnimatedMeshSurvivesEvictionBelowTheWorkingSetAndRebindsPixelIdentical) {
    // 1 MiB holds four 256x256 textures: well below one character's set.
    if (!prepare(1u << 20)) GTEST_SKIP() << "EGL, the EQ client tree or the shipped db unavailable";

    EQAnimatedMesh* mesh = loader_->getAnimatedMeshForRace(1, 0);  // human male, S3D path
    ASSERT_NE(mesh, nullptr);
    irr::scene::IMesh* frame = mesh->getMesh(0);
    ASSERT_NE(frame, nullptr);

    // Every distinct texture the CACHED mesh's materials hold, with its name
    // and its pixels as first drawn.
    struct Held { irr::video::ITexture* tex; std::string name; std::vector<uint8_t> pixels; };
    std::vector<Held> held;
    std::set<irr::video::ITexture*> seen;
    for (irr::u32 b = 0; b < frame->getMeshBufferCount(); ++b) {
        irr::video::ITexture* tex = frame->getMeshBuffer(b)->getMaterial().getTexture(0);
        if (!tex || !seen.insert(tex).second) continue;
        ASSERT_TRUE(cache_->isLiveTexture(tex));
        held.push_back({tex, tex->getName().getPath().c_str(), drawFrame(tex)});
    }
    ASSERT_GE(held.size(), 4u) << "a human model has more textures than the ceiling holds";

    ScopedListener scoped(*cache_);
    const CountingListener& listener = scoped.listener;
    flood(1u << 20);  // every character texture is now pushed out

    EXPECT_EQ(cache_->getPinnedTextureCount(), 0u) << "a character texture had no restore source";
    EXPECT_EQ(listener.calls, 0) << "an eviction destroyed something";
    size_t evicted = 0;
    for (const Held& h : held) {
        // Pointer comparisons only until liveness is proven: pre-B947 the
        // object is freed here and dereferencing it is the use-after-free.
        ASSERT_TRUE(cache_->isLiveTexture(h.tex)) << h.name << " was destroyed by an eviction";
        EXPECT_EQ(driver_->findTexture(h.name.c_str()), h.tex) << h.name;
        EXPECT_EQ(std::string(h.tex->getName().getPath().c_str()), h.name);
        if (!cache_->isResident(h.name)) ++evicted;
    }
    EXPECT_EQ(evicted, held.size()) << "the flood should have evicted every character texture";

    // The cached mesh is the one every later entity of the race is built from.
    ASSERT_EQ(loader_->getAnimatedMeshForRace(1, 0), mesh);
    std::set<irr::video::ITexture*> after;
    for (irr::u32 b = 0; b < frame->getMeshBufferCount(); ++b) {
        irr::video::ITexture* tex = frame->getMeshBuffer(b)->getMaterial().getTexture(0);
        if (tex) after.insert(tex);
    }
    EXPECT_EQ(after, seen) << "the cached materials were swapped (placeholder) or lost";

    // Re-binding restores the SAME pixels, whatever else must be evicted.
    const size_t restoresBefore = cache_->getRestoreCount();
    for (const Held& h : held) {
        EXPECT_EQ(drawFrame(h.tex), h.pixels) << h.name << " restored different pixels";
        EXPECT_LE(cache_->getCurrentUsage(), cache_->getMemoryLimit());
    }
    EXPECT_GE(cache_->getRestoreCount() - restoresBefore, held.size());
    EXPECT_EQ(cache_->getRestoreFailureCount(), 0u);
}

// The origin a character TextureInfo carries is the archive member it was
// read from: reading it back yields the loader's own bytes.
TEST_F(CachedCharacterMeshFixture, CharacterAndEquipmentTexturesCarryTheirArchiveOrigin) {
    if (!prepare(64u << 20)) GTEST_SKIP() << "EGL, the EQ client tree or the shipped db unavailable";
    ASSERT_TRUE(loader_->ensureModelDataLoaded(1, 0));
    ArchiveTextureSource reader;

    EquipmentModelLoader equipment(device_->getSceneManager(), driver_, device_->getFileSystem());
    equipment.setClientPath(clientPath());
    ASSERT_TRUE(equipment.loadEquipmentArchives());
    size_t checked = 0;
    for (int id = 1; id < 400 && checked < 5; ++id) {
        const EquipmentModelData* data = equipment.getEquipmentModelData(id);
        if (!data) continue;
        for (const auto& [name, tex] : data->textures) {
            if (!tex || tex->data.empty()) continue;
            const TextureOrigin origin = tex->origin();
            ASSERT_TRUE(origin.isArchiveMember()) << "IT" << id << " " << name;
            std::vector<char> bytes;
            ASSERT_TRUE(reader.readMember(*origin.file, origin.member, bytes)) << *origin.file << ":" << origin.member;
            EXPECT_EQ(bytes, tex->data) << "IT" << id << " " << name;
            ++checked;
        }
    }
    EXPECT_GT(checked, 0u) << "no equipment texture was checked";
}

// ---------------------------------------------------------------------------
// Plain-file origins (data/textures/*.bmp, SpellEffects) and the digest guard.
// ---------------------------------------------------------------------------

TEST_F(CharacterRestoreFixture, PlainFileOriginRestoresAndAChangedSourceIsPinnedNeverMisrestored) {
    if (!makeDevice()) GTEST_SKIP() << "EDT_OGLES2 unavailable";
    makeCache(1u << 20);
    const std::string dir = ::testing::TempDir();
    const std::string goodPath = dir + "/b947_good.bmp";
    const std::string changedPath = dir + "/b947_changed.bmp";
    auto writeFile = [](const std::string& path, const std::vector<char>& bytes) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };
    const std::vector<char> good = makeBmp(64, {220, 30, 40});
    const std::vector<char> changed = makeBmp(64, {10, 200, 30});
    writeFile(goodPath, good);
    writeFile(changedPath, changed);

    const TextureOrigin goodOrigin = TextureOrigin::plainFile(goodPath);
    const TextureOrigin changedOrigin = TextureOrigin::plainFile(changedPath);
    cache_->getOrLoad("b947_good", good, "test", "B947", false, &goodOrigin);
    irr::video::ITexture* goodTex = cache_->uploadTexture("b947_good");
    cache_->getOrLoad("b947_changed", changed, "test", "B947", false, &changedOrigin);
    irr::video::ITexture* changedTex = cache_->uploadTexture("b947_changed");
    ASSERT_NE(goodTex, nullptr);
    ASSERT_NE(changedTex, nullptr);
    const std::vector<uint8_t> goodPixels = drawFrame(goodTex);
    const std::vector<uint8_t> changedPixels = drawFrame(changedTex);

    // The second file changes on disk before its first eviction.
    writeFile(changedPath, makeBmp(64, {0, 0, 255}));
    flood(1u << 20);

    EXPECT_FALSE(cache_->isResident("b947_good")) << "the file-backed texture was released";
    EXPECT_FALSE(cache_->isPinned("b947_good"));
    EXPECT_TRUE(cache_->isPinned("b947_changed")) << "bytes that no longer match must not be restorable";
    EXPECT_TRUE(cache_->isResident("b947_changed"));
    EXPECT_EQ(drawFrame(goodTex), goodPixels) << "the file-backed restore drew other pixels";
    EXPECT_EQ(drawFrame(changedTex), changedPixels) << "the pinned texture must keep its own pixels";
    EXPECT_EQ(cache_->getRestoreFailureCount(), 0u);
}

// registerTexture() externals have no source at all: pinned, counted, kept.
TEST_F(CharacterRestoreFixture, AnExternalTextureIsPinnedAndNeverEvictedAgain) {
    if (!makeDevice()) GTEST_SKIP() << "EDT_OGLES2 unavailable";
    makeCache(1u << 20);
    irr::video::IImage* img = driver_->createImage(irr::video::ECF_A8R8G8B8,
                                                   irr::core::dimension2d<irr::u32>(128, 128));
    img->fill(irr::video::SColor(255, 12, 34, 56));
    irr::video::ITexture* ext = driver_->addTexture("b947_external", img);
    img->drop();
    ASSERT_NE(ext, nullptr);
    ASSERT_TRUE(cache_->registerTexture("b947_external", ext, 128u * 128u * 4u));
    ScopedListener scoped(*cache_);
    const CountingListener& listener = scoped.listener;

    flood(1u << 20);
    flood(1u << 20);

    EXPECT_TRUE(cache_->isPinned("b947_external"));
    EXPECT_TRUE(cache_->isResident("b947_external"));
    EXPECT_EQ(driver_->findTexture("b947_external"), ext);
    EXPECT_EQ(listener.calls, 0);
    EXPECT_EQ(cache_->getPinnedTextureCount(), 1u);
    EXPECT_LE(cache_->getCurrentUsage(), cache_->getMemoryLimit());
}

// Process-lifetime textures (the UI atlas, item icons, cursor, creature and
// tumbleweed atlases) are protectTexture()d: never evicted, never released.
TEST_F(CharacterRestoreFixture, ProtectedProcessLifetimeTexturesAreNeverEvicted) {
    if (!makeDevice()) GTEST_SKIP() << "EDT_OGLES2 unavailable";
    makeCache(1u << 20);
    const std::vector<char> bytes = makeBmp(128, {1, 2, 3});
    fillers_.bytes["b947_ui_like"] = bytes;  // restorable, and STILL not evicted
    cache_->protectTexture("b947_ui_like");
    cache_->getOrLoad("b947_ui_like", bytes, "test", "B947");
    irr::video::ITexture* ui = cache_->uploadTexture("b947_ui_like");
    ASSERT_NE(ui, nullptr);

    flood(1u << 20);
    flood(1u << 20);

    EXPECT_TRUE(cache_->isResident("b947_ui_like"));
    EXPECT_FALSE(cache_->isPinned("b947_ui_like")) << "protected is its own, deliberate state";
    EXPECT_EQ(cache_->getTexture("b947_ui_like"), ui);
    cache_->clear();
    EXPECT_EQ(cache_->getTexture("b947_ui_like"), ui) << "clear() (zone unload) keeps it too";
}

// ---------------------------------------------------------------------------
// EQE1 (the shipped path): a character-pack entry restores from its mapped
// pack, including a pack opened late through the resolver (T266 rule R2).
// ---------------------------------------------------------------------------

TEST_F(CharacterRestoreFixture, AnEqe1CharacterEntryFromALateOpenedPackRestoresFromThePack) {
    const std::string packPath = "/mnt/projects/EverQuest_ETC1/global2_chr.eqe1";
    EQE1Loader pack;
    if (!fileExists(packPath) || !pack.open(packPath)) GTEST_SKIP() << packPath << " unavailable";
    if (!makeDevice()) GTEST_SKIP() << "EDT_OGLES2 unavailable";
    std::vector<std::string> names;
    for (const auto& n : pack.getTextureNames()) {
        if (n.find("_alpha") == std::string::npos) names.push_back(n);
        if (names.size() == 3) break;
    }
    ASSERT_EQ(names.size(), 3u);

    // Measure, then rebuild with a ceiling that cannot hold all three.
    makeCache(64u << 20);
    cache_->addEQE1Loader(&pack);
    for (const auto& n : names) {
        std::vector<char> none;
        cache_->getOrLoad(n, none, "test", "B947");
        ASSERT_NE(cache_->uploadTexture(n), nullptr) << n;
    }
    const size_t all = cache_->getCurrentUsage();
    cache_->removeSourceProvider(&fillers_);
    makeCache(all - 1);
    // No loader registered up front: the first lookup opens the pack late.
    int lateOpens = 0;
    cache_->setEQE1ArchiveResolver([&](const std::string&) -> const EQE1Loader* {
        if (lateOpens++ == 0) {
            cache_->addEQE1Loader(&pack);
            cache_->noteEQE1LateOpen("global2_chr");
            return &pack;
        }
        return nullptr;
    });
    std::vector<irr::video::ITexture*> textures;
    std::vector<std::vector<uint8_t>> pixels;
    for (const auto& n : names) {
        std::vector<char> none;
        cache_->getOrLoad(n, none, "test", "B947");
        irr::video::ITexture* t = cache_->uploadTexture(n);
        ASSERT_NE(t, nullptr) << n;
        textures.push_back(t);
        pixels.push_back(drawFrame(t));  // as first uploaded, before any eviction of it
    }
    EXPECT_EQ(lateOpens, 1);
    EXPECT_GE(cache_->getEvictionCount(), 1u);
    EXPECT_FALSE(cache_->isResident(names[0]));
    EXPECT_EQ(cache_->getPinnedTextureCount(), 0u);
    // Round the ring twice: each bind restores from the pack, and draws what
    // the first upload drew.
    for (int round = 0; round < 2; ++round) {
        for (size_t i = 0; i < textures.size(); ++i) {
            EXPECT_EQ(drawFrame(textures[i]), pixels[i]) << names[i] << " round " << round;
        }
    }
    EXPECT_GE(cache_->getRestoreCount(), 2u);
    EXPECT_EQ(cache_->getRestoreFailureCount(), 0u);
}

}  // namespace

#else
TEST(TextureEvictionCharacterRestore, SkippedNoGraphics) { GTEST_SKIP() << "EQT_HAS_GRAPHICS not compiled in"; }
#endif
