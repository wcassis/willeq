// T468 (board B590) -- a door's texture across the constrained texture cache's
// two destruction paths, through the door manager the renderer really builds.
//
// The lead (T467's census): IrrlichtRenderer::loadGlobalAssets() was the only
// caller of ZoneStreamComponent::ensureDoorTextureGuard() (T124), and nothing
// called loadGlobalAssets(). The DoorManager a session uses is built by
// ZoneLifecycleComponent::setupInstantScene(); Step 1 of the zone load later
// hands it the cache, and loadZoneStep7_Doors() builds one only when none
// exists. So DoorTextureGuard was never an eviction listener.
//
// Why that matters (the T124 mechanism): DoorManager detaches a PVS-hidden
// door's pivotNode from the scene graph, and ConstrainedTextureCache::
// clearTextureReferences() walks the graph from the root, so it cannot reach a
// detached door's materials. The LRU then driver_->removeTexture()s a texture
// the door still names, and the door draws freed memory when it reappears.
//
// Seam: T467's -- a bare, never-Init()'d IrrlichtRenderer whose RendererContext
// is bound to an EDT_NULL device and the REAL ConstrainedTextureCache. The door
// manager is built by the renderer's own setupInstantScene(); the test then does
// what Step 1 (setZone + cache) and Step 7 (registerDoor + rebuildSingleDoor) do
// to it, with a real textured door mesh decoded and uploaded through the cache.

#include <gtest/gtest.h>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/door_manager.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/renderer_context.h"
#include <irrlicht.h>

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef WILLEQ_SOURCE_DIR
#error "WILLEQ_SOURCE_DIR must be defined (tests/cmake/renderer_guards.cmake)"
#endif

using namespace EQT::Graphics;
namespace fs = std::filesystem;

namespace {

const size_t kBudget = 256 * 1024;
const uint8_t kDoorId = 7;

// A 4x4 24-bit BMP -- the format ConstrainedTextureCache::decodeBMP() takes
// when no EQE1 pack is configured.
std::vector<char> makeBmp(uint8_t r, uint8_t g, uint8_t b) {
    const int w = 4, h = 4, row = 12;  // 4 * 3 bytes, already 4-aligned
    const uint32_t pixelBytes = row * h;
    const uint32_t fileSize = 54 + pixelBytes;
    std::vector<char> d(fileSize, 0);
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&d[at], &v, 4); };
    auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&d[at], &v, 2); };
    d[0] = 'B';
    d[1] = 'M';
    put32(2, fileSize);
    put32(10, 54);
    put32(14, 40);
    put32(18, w);
    put32(22, h);
    put16(26, 1);
    put16(28, 24);
    put32(34, pixelBytes);
    for (uint32_t i = 0; i < pixelBytes; i += 3) {
        d[54 + i] = static_cast<char>(b);
        d[54 + i + 1] = static_cast<char>(g);
        d[54 + i + 2] = static_cast<char>(r);
    }
    return d;
}

// One zone whose _obj archive holds a single door model, DOOR1, textured with
// `texName`.
std::shared_ptr<S3DZone> makeZone(const std::string& zoneName, const std::string& texName) {
    auto geom = std::make_shared<ZoneGeometry>();
    geom->name = "DOOR1";
    geom->vertices = {
        {0, 0, 0, 0, 0, 1, 0, 0},
        {2, 0, 0, 0, 0, 1, 1, 0},
        {0, 6, 0, 0, 0, 1, 0, 1},
    };
    geom->triangles = {{0, 1, 2, 0, 0}};
    geom->minX = 0; geom->minY = 0; geom->minZ = 0;
    geom->maxX = 2; geom->maxY = 6; geom->maxZ = 0;
    auto& mats = geom->mutableMaterialData();
    mats.textureNames = {texName};
    mats.textureInvisible = {false};
    mats.textureMasked = {false};

    auto tex = std::make_shared<TextureInfo>();
    tex->name = texName;
    tex->data = makeBmp(200, 120, 40);
    tex->width = 4;
    tex->height = 4;

    auto zone = std::make_shared<S3DZone>();
    zone->zoneName = zoneName;
    zone->objectGeometries["DOOR1"] = geom;
    zone->objectTextures[texName] = tex;
    return zone;
}

class DoorTextureEvictionTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    irr::video::ITexture* placeholder_ = nullptr;
    std::unique_ptr<IrrlichtRenderer> renderer_;

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        ASSERT_NE(device_, nullptr) << "EDT_NULL device";
        driver_ = device_->getVideoDriver();
        smgr_ = device_->getSceneManager();

        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
        config.textureMemoryBytes = kBudget;
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        cache_->setSceneManager(smgr_);
        // Created up front, as the renderer does at init (renderer_init.cpp),
        // so it can never be allocated at a freed door texture's address.
        placeholder_ = cache_->getPlaceholderTexture();

        renderer_ = std::make_unique<IrrlichtRenderer>();
        RendererContext& ctx = renderer_->Context();
        device_->grab();  // IrrlichtRenderer::shutdown() drops the one it holds
        ctx.device = device_;
        ctx.driver = driver_;
        ctx.smgr = smgr_;
        ctx.constrainedTextureCache = cache_.get();
    }

    void TearDown() override {
        renderer_.reset();
        cache_.reset();
        if (device_) device_->drop();
    }

    RendererContext& ctx() { return renderer_->Context(); }

    bool driverHolds(irr::video::ITexture* t) const {
        for (irr::u32 i = 0; i < driver_->getTextureCount(); ++i) {
            if (driver_->getTextureByIndex(i) == t) return true;
        }
        return false;
    }

    // T463's definition: non-null, not the placeholder, still in the driver's
    // own texture list, and a live cache entry.
    ::testing::AssertionResult drawable(irr::video::ITexture* t) const {
        if (!t) return ::testing::AssertionFailure() << "texture is NULL";
        if (t == placeholder_) return ::testing::AssertionFailure() << "texture is the placeholder";
        if (!driverHolds(t))
            return ::testing::AssertionFailure() << "texture is no longer in the driver's list: DANGLING";
        if (!cache_->isLiveTexture(t))
            return ::testing::AssertionFailure() << "texture is not a live cache entry";
        return ::testing::AssertionSuccess();
    }

    // After `evicted` left the cache, a material may hold the placeholder (the
    // zone-geometry policy under LRU, RegionMeshTextureGuard's own) or a live
    // texture -- never the evicted pointer itself, which the driver has freed.
    ::testing::AssertionResult notFreed(irr::video::ITexture* t, irr::video::ITexture* evicted) const {
        if (t == evicted) {
            if (!driverHolds(t))
                return ::testing::AssertionFailure()
                       << "material still names the evicted texture " << static_cast<void*>(t)
                       << ", which the driver no longer lists: DANGLING (freed)";
            // Only reachable if the allocator reused the address for a live
            // texture; safe to query, and it names what the door would draw.
            return ::testing::AssertionFailure()
                   << "material still names the evicted texture's address " << static_cast<void*>(t)
                   << ", now reused by '" << t->getName().getPath().c_str() << "'";
        }
        if (t && t != placeholder_ && !driverHolds(t))
            return ::testing::AssertionFailure() << "material names a texture the driver does not hold";
        return ::testing::AssertionSuccess();
    }

    // What the zone load does to the renderer's door manager: Step 1 gives it
    // the zone and the cache, Step 7 rebuilds every registered door.
    const DoorVisual* buildDoorInZone(const std::shared_ptr<S3DZone>& zone) {
        DoorManager* doors = ctx().doorManager.get();
        if (!doors) return nullptr;
        doors->setZone(zone);
        doors->setConstrainedTextureCache(cache_.get());
        if (!doors->registerDoor(kDoorId, "DOOR1", 10.0f, 20.0f, 0.0f, 0.0f, 0, 100, 0, false)) return nullptr;
        if (!doors->rebuildSingleDoor(kDoorId)) return nullptr;
        return doors->getDoor(kDoorId);
    }

    static irr::video::ITexture* nodeTexture(const DoorVisual* d) {
        return d->sceneNode->getMaterial(0).getTexture(0);
    }
    static irr::video::ITexture* meshTexture(const DoorVisual* d) {
        return d->sceneNode->getMesh()->getMeshBuffer(0)->getMaterial().getTexture(0);
    }

    // Sixteen zone textures of a quarter of the budget each (T460's recipe):
    // the cache must evict, oldest first. Every filler is ALLOCATED before any
    // is registered, so no allocation follows an eviction and a freed door
    // texture's address cannot be handed to a filler -- the driver-list check
    // then proves the free instead of being fooled by address reuse.
    void applyBudgetPressure() {
        std::vector<std::pair<std::string, irr::video::ITexture*>> fillers;
        for (int i = 0; i < 16; ++i) {
            const std::string name = "t468_filler_" + std::to_string(i);
            irr::video::ITexture* t =
                driver_->addTexture(irr::core::dimension2d<irr::u32>(4, 4), name.c_str());
            ASSERT_NE(t, nullptr);
            fillers.emplace_back(name, t);
        }
        for (const auto& [name, t] : fillers) {
            ASSERT_TRUE(cache_->registerTexture(name, t, kBudget / 4)) << name;
        }
        ASSERT_GT(cache_->getEvictionCount(), 0u) << "the fixture did not create budget pressure";
    }
};

}  // namespace

// Pre-fix RED: no DoorTextureGuard was registered, the hidden door was out of
// the scene walk's reach, and both its node material and its (cached, shared)
// mesh buffer kept the freed texture -- drawn again the moment PVS shows it.
TEST_F(DoorTextureEvictionTest, AHiddenDoorNeverHoldsAFreedTextureUnderBudgetPressure) {
    renderer_->setupInstantScene("t468zone", 0.0f, 0.0f, 0.0f);
    ASSERT_NE(ctx().doorManager, nullptr) << "setupInstantScene() built no door manager";

    const DoorVisual* door = buildDoorInZone(makeZone("t468zone", "t468_door_tex"));
    ASSERT_NE(door, nullptr) << "the door did not build";
    ASSERT_FALSE(door->usePlaceholder) << "the door fell back to a placeholder mesh";
    irr::video::ITexture* tex = nodeTexture(door);
    ASSERT_TRUE(drawable(tex)) << "the built door has no real texture";
    ASSERT_EQ(meshTexture(door), tex);

    // The PVS-driven detach (setDoorState/update/rebuildSingleDoor all do this).
    ctx().doorManager->setAllDoorsVisible(false);
    ASSERT_FALSE(door->inSceneGraph);

    applyBudgetPressure();
    ASSERT_FALSE(cache_->hasTexture("t468_door_tex")) << "the door texture was not evicted";

    EXPECT_TRUE(notFreed(nodeTexture(door), tex)) << "the hidden door's node material";
    EXPECT_TRUE(notFreed(meshTexture(door), tex)) << "the hidden door's mesh buffer material";

    // The moment it would be drawn again.
    ctx().doorManager->setAllDoorsVisible(true);
    ASSERT_TRUE(door->inSceneGraph);
    EXPECT_TRUE(notFreed(nodeTexture(door), tex)) << "the door node after PVS shows it again";
}

// The same with the door IN the scene graph, where the generic walk does reach
// it: pins that registering the guard changes nothing on that path.
TEST_F(DoorTextureEvictionTest, AVisibleDoorNeverHoldsAFreedTextureUnderBudgetPressure) {
    renderer_->setupInstantScene("t468zone", 0.0f, 0.0f, 0.0f);
    const DoorVisual* door = buildDoorInZone(makeZone("t468zone", "t468_door_tex"));
    ASSERT_NE(door, nullptr);
    irr::video::ITexture* tex = nodeTexture(door);
    ASSERT_TRUE(drawable(tex));
    ASSERT_TRUE(door->inSceneGraph);

    applyBudgetPressure();
    ASSERT_FALSE(cache_->hasTexture("t468_door_tex"));
    EXPECT_TRUE(notFreed(nodeTexture(door), tex)) << "the visible door's node material";
    EXPECT_TRUE(notFreed(meshTexture(door), tex)) << "the visible door's mesh buffer material";
}

// The zone-unload half of the lead. unloadZone() runs clearDoors() -- which
// re-attaches, removes and drops every door node and drops the door mesh cache
// -- BEFORE ConstrainedTextureCache::clear(), so no door material survives into
// clear(). The next zone's doors then build against fresh uploads. Expected
// GREEN before the fix as well: this is the evidence that refutes the unload
// half of B590, kept as a pin on that order.
TEST_F(DoorTextureEvictionTest, DoorsDrawTheirRealTextureAfterAZoneUnloadAndTheNextZoneBuild) {
    renderer_->setupInstantScene("t468a", 0.0f, 0.0f, 0.0f);
    const DoorVisual* door = buildDoorInZone(makeZone("t468a", "t468_door_tex"));
    ASSERT_NE(door, nullptr);
    ASSERT_TRUE(drawable(nodeTexture(door)));
    ctx().doorManager->setAllDoorsVisible(false);  // a hidden door at unload

    renderer_->unloadZone();
    ASSERT_NE(ctx().doorManager, nullptr) << "the door manager is renderer-lifetime";
    EXPECT_EQ(ctx().doorManager->getDoorCount(), 0u) << "unloadZone() left door records behind";
    EXPECT_FALSE(cache_->hasTexture("t468_door_tex")) << "clear() kept the old zone's door texture";

    // The next zone reuses the model and texture name, which is the case a
    // stale door mesh cache would serve a freed texture to.
    renderer_->setupInstantScene("t468b", 0.0f, 0.0f, 0.0f);
    const DoorVisual* next = buildDoorInZone(makeZone("t468b", "t468_door_tex"));
    ASSERT_NE(next, nullptr) << "the next zone's door did not build";
    EXPECT_TRUE(drawable(nodeTexture(next))) << "the next zone's door node material";
    EXPECT_TRUE(drawable(meshTexture(next))) << "the next zone's door mesh buffer material";
}

// The structural half: DoorManager is constructed in exactly ONE place under
// src/, which registers the guard, so setupInstantScene() and Step 7 cannot
// diverge again. Pre-fix RED: three sites (setupInstantScene, Step 7, and the
// uncalled loadGlobalAssets()).
TEST(DoorManagerConstructionSource, TheDoorManagerHasOneConstructionSite) {
    const std::string needle = "make_unique<DoorManager>";
    const fs::path src = fs::path(WILLEQ_SOURCE_DIR) / "src";
    ASSERT_TRUE(fs::is_directory(src)) << src;
    std::vector<std::string> sites;
    for (const auto& entry : fs::recursive_directory_iterator(src)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension();
        if (ext != ".cpp" && ext != ".h") continue;
        std::ifstream in(entry.path());
        std::string line;
        int lineNo = 0;
        while (std::getline(in, line)) {
            ++lineNo;
            if (line.find(needle) != std::string::npos) {
                sites.push_back(fs::relative(entry.path(), WILLEQ_SOURCE_DIR).string() + ":" +
                                std::to_string(lineNo));
            }
        }
    }
    std::ostringstream list;
    for (const auto& s : sites) list << "\n  " << s;
    ASSERT_EQ(sites.size(), 1u) << needle << " is constructed at:" << list.str();
    EXPECT_NE(sites[0].find("src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp"), std::string::npos)
        << needle << " is constructed outside ZoneLifecycleComponent::ensureDoorManager():" << list.str();
}

// ---------------------------------------------------------------------------
// T470 (board B621) -- door MESH references, on the same seam.
//
// Every ZoneMeshBuilder build and every placeholder (createCubeMesh,
// createOctagonalPrism) is `new SMesh`: reference count 1, owned by whoever
// asked. A scene node grab()s its own. So once a door's node is gone, the only
// holder a door mesh may have left is DoorManager::doorMeshCache_, and once
// clearDoors() has run, none. MeshWatch grabs the mesh for the length of a
// test (so a mesh that is correctly freed is not read after free) and reports
// how many OTHER references exist.
namespace {

class MeshWatch {
public:
    explicit MeshWatch(irr::scene::IMesh* m) : mesh_(m) { if (mesh_) mesh_->grab(); }
    ~MeshWatch() { if (mesh_) mesh_->drop(); }
    MeshWatch(const MeshWatch&) = delete;
    MeshWatch& operator=(const MeshWatch&) = delete;
    // References held by anyone but this watch.
    int holders() const { return mesh_ ? mesh_->getReferenceCount() - 1 : -1; }
    irr::scene::IMesh* get() const { return mesh_; }
private:
    irr::scene::IMesh* mesh_;
};

// The same zone, but the door texture's pixel data is not here yet and the
// cache does not hold it -- the "async upload still pending" state in which
// ZoneMeshBuilder reports a missing texture and findDoorMesh() does not cache.
std::shared_ptr<S3DZone> makeZoneWithPendingTexture(const std::string& zoneName, const std::string& texName) {
    auto zone = makeZone(zoneName, texName);
    zone->objectTextures[texName]->data.clear();
    return zone;
}

}  // namespace

// Path: registerDoor()'s placeholder (every door of every zone load --
// Application snapshot -> registerDoor, then Step 7's rebuildSingleDoor Path 1).
TEST_F(DoorTextureEvictionTest, ARegisteredDoorsPlaceholderMeshDiesWithItsNode) {
    renderer_->setupInstantScene("t470zone", 0.0f, 0.0f, 0.0f);
    DoorManager* doors = ctx().doorManager.get();
    ASSERT_NE(doors, nullptr);
    doors->setZone(makeZone("t470zone", "t470_door_tex"));
    doors->setConstrainedTextureCache(cache_.get());
    ASSERT_TRUE(doors->registerDoor(kDoorId, "DOOR1", 10.0f, 20.0f, 0.0f, 0.0f, 0, 100, 0, false));
    const DoorVisual* door = doors->getDoor(kDoorId);
    ASSERT_NE(door, nullptr);
    ASSERT_TRUE(door->usePlaceholder);
    MeshWatch placeholder(door->sceneNode->getMesh());
    EXPECT_EQ(placeholder.holders(), 1) << "a registered placeholder mesh is held by its node alone";

    ASSERT_TRUE(doors->rebuildSingleDoor(kDoorId));
    EXPECT_EQ(placeholder.holders(), 0) << "the placeholder mesh outlived the node that drew it";
}

// Path: a real mesh built while a texture is pending (not cached by
// findDoorMesh()).
TEST_F(DoorTextureEvictionTest, AMeshBuiltWhileItsTextureIsPendingDiesWithItsDoor) {
    renderer_->setupInstantScene("t470zone", 0.0f, 0.0f, 0.0f);
    const DoorVisual* door = buildDoorInZone(makeZoneWithPendingTexture("t470zone", "t470_door_tex"));
    ASSERT_NE(door, nullptr);
    ASSERT_FALSE(door->usePlaceholder) << "a pending texture still builds the real mesh";
    ASSERT_FALSE(ctx().doorManager->getLastMissingTextures().empty()) << "the texture was not reported pending";
    MeshWatch mesh(door->sceneNode->getMesh());
    EXPECT_EQ(mesh.holders(), 1) << "an uncached door mesh is held by its node alone";

    ctx().doorManager->clearDoors();
    EXPECT_EQ(mesh.holders(), 0) << "the uncached door mesh outlived clearDoors()";
}

// Path: invalidateMeshCache() -- by model name and with "" (the shader swap,
// diagnostics_debug.cpp, its one production caller since T475 deleted the
// texture-arrival arm that called it by name).
TEST_F(DoorTextureEvictionTest, InvalidatingTheDoorMeshCacheReleasesTheCachesReference) {
    renderer_->setupInstantScene("t470zone", 0.0f, 0.0f, 0.0f);
    for (const std::string key : {"DOOR1", "door1", ""}) {
        SCOPED_TRACE("invalidateMeshCache(\"" + key + "\")");
        const DoorVisual* door = buildDoorInZone(makeZone("t470zone", "t470_door_tex"));
        ASSERT_NE(door, nullptr);
        MeshWatch mesh(door->sceneNode->getMesh());
        EXPECT_EQ(mesh.holders(), 2) << "a cached door mesh: its node and the cache";

        ctx().doorManager->invalidateMeshCache(key);
        EXPECT_EQ(mesh.holders(), 1) << "the cache entry was erased but its reference kept";

        ctx().doorManager->clearDoors();
        EXPECT_EQ(mesh.holders(), 0) << "the door mesh outlived clearDoors()";
    }
}

// Path: createDoor()'s placeholder (IrrlichtBridge's live door spawn), then
// rebuildSingleDoor Path 2's setMesh() swap to the real, cached mesh.
TEST_F(DoorTextureEvictionTest, ACreatedDoorsPlaceholderDiesAtTheMeshSwap) {
    renderer_->setupInstantScene("t470zone", 0.0f, 0.0f, 0.0f);
    DoorManager* doors = ctx().doorManager.get();
    ASSERT_NE(doors, nullptr);
    doors->setZone(nullptr);  // no zone data yet: createDoor() must fall back
    ASSERT_TRUE(doors->createDoor(kDoorId, "DOOR1", 10.0f, 20.0f, 0.0f, 0.0f, 0, 100, 0, false));
    const DoorVisual* door = doors->getDoor(kDoorId);
    ASSERT_NE(door, nullptr);
    ASSERT_TRUE(door->usePlaceholder);
    MeshWatch placeholder(door->sceneNode->getMesh());
    EXPECT_EQ(placeholder.holders(), 1) << "a created placeholder mesh is held by its node alone";

    doors->setZone(makeZone("t470zone", "t470_door_tex"));
    doors->setConstrainedTextureCache(cache_.get());
    ASSERT_TRUE(doors->rebuildSingleDoor(kDoorId));
    EXPECT_EQ(placeholder.holders(), 0) << "the placeholder mesh outlived the swap that replaced it";
    MeshWatch real(door->sceneNode->getMesh());
    EXPECT_EQ(real.holders(), 2) << "the swapped-in mesh: its node and the cache";

    doors->clearDoors();
    EXPECT_EQ(real.holders(), 0) << "the swapped-in mesh outlived clearDoors()";
}

// The cache-hit path (25 DOOR1 instances share one mesh). Green before the fix
// too: it pins that the shared mesh is not over-dropped by the new ownership
// rule, and that the next zone starts from nothing.
TEST_F(DoorTextureEvictionTest, DoorsSharingACachedMeshReleaseItExactlyOnceAtZoneChange) {
    renderer_->setupInstantScene("t470a", 0.0f, 0.0f, 0.0f);
    DoorManager* doors = ctx().doorManager.get();
    ASSERT_NE(doors, nullptr);
    doors->setZone(makeZone("t470a", "t470_door_tex"));
    doors->setConstrainedTextureCache(cache_.get());
    ASSERT_TRUE(doors->createDoor(1, "DOOR1", 0.0f, 0.0f, 0.0f, 0.0f, 0, 100, 0, false));
    ASSERT_TRUE(doors->createDoor(2, "DOOR1", 10.0f, 0.0f, 0.0f, 0.0f, 0, 100, 0, false));
    ASSERT_TRUE(doors->registerDoor(3, "DOOR1", 20.0f, 0.0f, 0.0f, 0.0f, 0, 100, 0, false));
    ASSERT_TRUE(doors->rebuildSingleDoor(3));
    ASSERT_EQ(doors->getDoor(1)->sceneNode->getMesh(), doors->getDoor(2)->sceneNode->getMesh());
    ASSERT_EQ(doors->getDoor(1)->sceneNode->getMesh(), doors->getDoor(3)->sceneNode->getMesh());
    MeshWatch shared(doors->getDoor(1)->sceneNode->getMesh());
    EXPECT_EQ(shared.holders(), 4) << "three nodes and the cache";

    renderer_->unloadZone();
    EXPECT_EQ(shared.holders(), 0) << "the shared door mesh outlived the zone unload";
}

// ---------------------------------------------------------------------------
// T475 (boards B670, B640) -- the deleted texture-ARRIVAL path stays deleted.
//
// Until T475 the renderer carried a whole path for a texture that "arrives"
// after the mesh that needs it was built:
//   * Step 6 / Step 7 / rebuildRegionMesh() filed the object, door or region
//     under each missing texture name (pendingTextureObjects,
//     pendingTextureDoors_, pendingTextureRegions_);
//   * renderer_gpu_upload.cpp's Texture/CompressedTexture arm took a GPU upload
//     result keyed `uint64_t(3) << 56`, registered it, and pushed the waiting
//     ids to doorTextureRebuildQueue / objectTextureRebuildQueue /
//     textureRebuildQueue_;
//   * FrameComponent Priority 8/9 and ZoneStreamComponent P1.6/P1.7 drained
//     those into rebuildSingleDoor() and buildDeferredObject().
// The producer was ConstrainedTextureCache's async upload queue (it also wrote
// pendingAsyncUploads_). Batch T's T04 (aaee58d1) made texture loading
// synchronous and T326 (77c842b4) deleted the queue, so from March the path
// had no producer at all; T473 proved it and T475 deleted it, together with
// the GPUUploadThread texture request types and TextureAtlas's uncalled async
// page upload. B640 (rebuildSingleDoor() has no re-texture branch for a real
// door mesh) closed with it: nothing can ask for that branch.
//
// A deleted path's net is a census, not a behaviour test: this fails if any
// half of the path reappears in code, because a half alone is exactly the
// shape B670 was -- a queue written and never drained, or an arm with no
// producer. Comments are stripped before matching, so tombstone prose that
// names the old symbols is fine. Bringing an async texture producer back is a
// decision, not a cleanup: it needs a consumer for every queue it fills, and
// a rebuildSingleDoor() re-texture branch under T470's ownership rule (B640).
namespace {

std::string stripCppComments(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool inBlock = false, inLine = false, inString = false, inChar = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const char n = (i + 1 < text.size()) ? text[i + 1] : '\0';
        if (inBlock) {
            if (c == '*' && n == '/') { inBlock = false; ++i; }
            else if (c == '\n') out.push_back('\n');
            continue;
        }
        if (inLine) {
            if (c == '\n') { inLine = false; out.push_back('\n'); }
            continue;
        }
        if (inString || inChar) {
            out.push_back(c);
            if (c == '\\' && i + 1 < text.size()) { out.push_back(n); ++i; continue; }
            if ((inString && c == '"') || (inChar && c == '\'')) { inString = inChar = false; }
            continue;
        }
        if (c == '/' && n == '/') { inLine = true; ++i; continue; }
        if (c == '/' && n == '*') { inBlock = true; ++i; continue; }
        if (c == '"') inString = true;
        else if (c == '\'') inChar = true;
        out.push_back(c);
    }
    return out;
}

// One forbidden signature: a regular expression over comment-stripped code,
// and what its return would mean.
struct ArrivalSignature {
    const char* pattern;
    const char* meaning;
};

const ArrivalSignature kArrivalSignatures[] = {
    {R"(<<\s*56\b)",
     "a GPU upload keyed on its high byte -- the arrival key was uint64_t(3) << 56"},
    {R"(>>\s*56\b)",
     "a GPU upload result routed on its high byte -- the arrival arm's sourceType"},
    {R"(UploadRequestType\s*::\s*(Texture|CompressedTexture)\b)",
     "a texture request type on GPUUploadThread -- T475 left it VBO-only"},
    {R"(\bpendingAsyncUploads_\b)",
     "the texture cache's pending-async set, written by no one since T04"},
    {R"(\bpendingTexture(Doors|Objects|Regions)_?\b)",
     "a pending-texture door/object/region map with no arrival to drain it"},
    {R"(\b(door|object)?[Tt]extureRebuildQueue_?\b)",
     "a texture rebuild queue with no producer"},
    {R"(\b(processTextureUpload|processCompressedTextureUpload|uploadPreloadedPageAsync|setPageTexture|setGPUUploadThread|clearPendingAsync|buildDeferredObject)\b)",
     "a function T475 deleted with the arrival path"},
};

// Every code file in the production trees and the tools. tests/ is not
// scanned: this file quotes the signatures.
std::vector<std::string> arrivalPathHits(const fs::path& root) {
    std::vector<std::regex> compiled;
    for (const auto& sig : kArrivalSignatures) compiled.emplace_back(sig.pattern);
    std::vector<std::string> hits;
    for (const char* dir : {"src", "include", "tools", "benchmarks"}) {
        const fs::path base = root / dir;
        if (!fs::is_directory(base)) continue;
        for (const auto& entry : fs::recursive_directory_iterator(base)) {
            if (!entry.is_regular_file()) continue;
            const auto ext = entry.path().extension();
            if (ext != ".cpp" && ext != ".h" && ext != ".hpp" && ext != ".cc" && ext != ".inl") continue;
            std::ifstream in(entry.path());
            std::stringstream buf;
            buf << in.rdbuf();
            std::istringstream code(stripCppComments(buf.str()));
            std::string line;
            int lineNo = 0;
            while (std::getline(code, line)) {
                ++lineNo;
                for (size_t k = 0; k < compiled.size(); ++k) {
                    if (std::regex_search(line, compiled[k])) {
                        hits.push_back(fs::relative(entry.path(), root).string() + ":" +
                                       std::to_string(lineNo) + ": " + kArrivalSignatures[k].meaning +
                                       "\n      " + line);
                    }
                }
            }
        }
    }
    return hits;
}

}  // namespace

TEST(TextureArrivalPathCensus, NoHalfOfTheDeletedArrivalPathIsBackInCode) {
    const fs::path root(WILLEQ_SOURCE_DIR);
    ASSERT_TRUE(fs::is_directory(root / "src")) << root;
    const auto hits = arrivalPathHits(root);
    std::ostringstream list;
    for (const auto& h : hits) list << "\n  " << h;
    EXPECT_TRUE(hits.empty())
        << "part of the texture-ARRIVAL path T475 deleted (board B670) is back in code. On its own "
           "it is dead again: a pending-texture queue nothing drains, or an arrival arm nothing "
           "feeds. If an async texture producer is being restored deliberately, it needs a consumer "
           "for every queue it fills, and DoorManager::rebuildSingleDoor() needs a re-texture "
           "branch for a door whose REAL mesh was built without its texture (board B640, under "
           "T470's ownership rule) -- then replace this census with a behaviour test of that path:"
        << list.str();
}

// The census must be able to see what it forbids: the stripper keeps code and
// drops comments, and each signature matches the line that used to exist.
TEST(TextureArrivalPathCensus, TheSignaturesMatchTheDeletedCodeAndIgnoreTombstones) {
    EXPECT_EQ(stripCppComments("a; // pendingTextureDoors_\nb; /* doorTextureRebuildQueue */ c;"),
              "a; \nb;  c;");
    const std::vector<std::pair<const char*, size_t>> deleted = {
        {"    req.callbackKey = uint64_t(3) << 56;", 0},
        {"uint8_t sourceType = static_cast<uint8_t>(result.callbackKey >> 56);", 1},
        {"case UploadRequestType::CompressedTexture: {", 2},
        {"pendingAsyncUploads_.insert(name);", 3},
        {"ctx_.ZoneLifecycle().pendingTextureDoors()[texName].insert(doorId);", 4},
        {"ctx_.pendingTextureObjects[texName].insert(oi);", 4},
        {"pendingTextureRegions_[texName].insert(regionIdx);", 4},
        {"ctx_.doorTextureRebuildQueue.push_back(doorId);", 5},
        {"if (!ctx_.objectTextureRebuildQueue.empty()) {", 5},
        {"textureRebuildQueue_.push_back({regionIdx, distSq});", 5},
        {"ctx_.ZoneStream().buildDeferredObject(objIdx);", 6},
    };
    for (const auto& [line, index] : deleted) {
        EXPECT_TRUE(std::regex_search(stripCppComments(line), std::regex(kArrivalSignatures[index].pattern)))
            << "signature " << index << " no longer matches the deleted line: " << line;
    }
    // Live neighbours that must NOT match: the VBO key (high 16 bits) and the
    // region-mesh texture guard, and T473's missing-texture accessors (B671).
    for (const char* live : {"req.callbackKey = (static_cast<uint64_t>(i) << 48) |",
                             "const auto& missing = ctx_.doorManager->getLastMissingTextures();",
                             "case UploadRequestType::VertexBuffer: {",
                             "builder.getMissingTextures()"}) {
        for (const auto& sig : kArrivalSignatures) {
            EXPECT_FALSE(std::regex_search(live, std::regex(sig.pattern)))
                << "signature '" << sig.pattern << "' matches a live line: " << live;
        }
    }
}

#endif  // EQT_HAS_GRAPHICS
