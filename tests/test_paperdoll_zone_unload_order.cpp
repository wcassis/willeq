// T457 (board B482): the LIFECYCLE-ORDER net for the inventory paperdoll
// across a zone change.
//
// THE DEFECT THIS PINS. T449 found a live SIGSEGV that any player reaches by
// opening the inventory and then zoning:
//
//   #0  irr::video::COGLES2Texture::getOpenGLTextureName()
//   #1  irr::video::COpenGLES2Driver::bindAlphaCompanionUnit()
//   ...
//   #5  EQT::Graphics::EQAnimatedMeshSceneNode::render()
//   #6  irr::scene::CSceneManager::drawAll()
//   #7  eqt::ui::CharacterModelView::renderToTexture()
//   #8  EQT::Graphics::OverlaysComponent::updatePaperdollForFrame()
//
// The mechanism is a SECOND SCENE GRAPH that the zone unload does not know
// about. ConstrainedTextureCache::clear() -- the zone-unload hook -- calls
// clearTextureReferences() before every driver_->removeTexture(), exactly so
// that no surviving scene node is left holding a raw, non-owning ITexture*
// (SMaterial::setTexture() does not grab; removeTexture() drops the sole
// owning reference and the COGLES2Texture is deleted). That was T119's fix and
// it is correct -- but clearTextureReferences() scans from
// smgr_->getRootSceneNode(), the MAIN scene manager's root, and the paperdoll's
// model does not live there. CharacterModelView::init() builds its own
// isolated scene manager (smgr->createNewSceneManager(false)) and parents
// characterNode_ under THAT root, so the scan can never reach it. Same class as
// P10b/T119/the boids' out-of-graph nodes, one layer further out: not a node
// hidden from the graph, a node in a graph nobody walks.
//
// So after a zone unload the doll's materials still name destroyed textures,
// and the very next frame draws it: renderToTexture() re-renders whenever
// update() ticks the idle animation past animationInterval_, which the o01
// loop guarantees. T432's per-character persistence widens the blast radius --
// a character who ever left the inventory open has [InventoryWindow] Show=1
// saved and the doll is reopened at LOGIN, so the next zone change kills the
// client with no player action at all.
//
// THE FIX IS LIFECYCLE ORDER, NOT A NULL CHECK AT THE DRIVER. The paperdoll is
// RELEASED before the zone's textures are destroyed and REBUILT after zone-in:
// ZoneLifecycleComponent::unloadZone() calls ctx_.Overlays().onZoneUnload(),
// which drops the doll's character and weapon nodes and resets the
// appearance latch so updatePaperdollForFrame() rebuilds the model on the
// first frame the new zone's player entity exists. Nothing is null-checked at
// bindAlphaCompanionUnit(); the dangling pointer is never created.
//
// WHY A TEXT PARSE AND NOT A RUNTIME OBSERVATION (the G05 idiom, and G03
// correction 9 -- check what the container preserves first): there is no
// container, and unloadZone() cannot be run in a unit test at all. It needs a
// live Irrlicht device, a GL context, a scene manager, a loaded zone, a
// simulation worker, an entity prep worker and a driver that can delete
// hardware buffers. The TU IS the order, so the test reads it. The three
// assertions below are spelled out rather than diffed against a golden,
// because what is load-bearing here is a RELATIVE position (the release
// strictly before the two owners that destroy what it names), not a whole
// statement sequence.
//
// The behavioural half -- that CharacterModelView::releaseCharacterModel()
// actually leaves no material naming anything -- is
// tests/test_paperdoll_release.cpp, which needs an EDT_NULL device and the
// shipped model data.

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const std::string kRoot = WILLEQ_REPO_ROOT;

std::string ReadFile(const std::string& path) {
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "cannot open " << path;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The body of `void <Class>::<Method>(` up to the closing brace at column 0.
// Every TU this test reads is clang-formatted with method bodies opening on
// the signature line and closing at column 0, which is what makes this safe.
std::string BodyOf(const std::string& src, const std::string& signature) {
    const size_t start = src.find(signature);
    EXPECT_NE(start, std::string::npos) << "signature not found: " << signature;
    if (start == std::string::npos) return {};
    const size_t end = src.find("\n}\n", start);
    EXPECT_NE(end, std::string::npos) << "unterminated body: " << signature;
    if (end == std::string::npos) return {};
    return src.substr(start, end - start);
}

// Index of `needle` inside `body`, as a position that can be ordered against
// another. Fails the test (and returns npos) when absent, so a missing call
// reports as itself rather than as a confusing ordering failure.
size_t PositionOf(const std::string& body, const std::string& needle,
                  const char* what) {
    const size_t at = body.find(needle);
    EXPECT_NE(at, std::string::npos) << what << " -- not found: " << needle;
    return at;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. The order inside unloadZone(): release the doll BEFORE either owner of
//    the resources it names destroys them.
// ---------------------------------------------------------------------------
TEST(PaperdollZoneUnloadOrder, TheDollIsReleasedBeforeTheZoneTexturesAreDestroyed) {
    const std::string tu =
        ReadFile(kRoot + "/src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp");
    const std::string body = BodyOf(tu, "void ZoneLifecycleComponent::unloadZone()");
    ASSERT_FALSE(body.empty());

    // The release hop. It is a call on the overlays component, because the
    // paperdoll is OverlaysComponent's member -- unloadZone() must not reach
    // into CharacterModelView itself.
    const size_t release =
        PositionOf(body, "ctx_.Overlays().onZoneUnload();",
                   "unloadZone() must release the paperdoll (B482)");

    // RaceModelLoader::clearMeshCaches() drops the per-instance meshes the
    // doll's characterNode_ was built from.
    const size_t meshCaches =
        PositionOf(body, "rml->clearMeshCaches();", "the mesh-cache drop");

    // ConstrainedTextureCache::clear() destroys the textures those meshes'
    // materials name -- this is the call whose removeTexture() deletes the
    // COGLES2Texture the crash dereferences.
    const size_t textureClear =
        PositionOf(body, "ctx_.constrainedTextureCache->clear();",
                   "the zone texture-cache wipe");

    ASSERT_NE(release, std::string::npos);
    ASSERT_NE(meshCaches, std::string::npos);
    ASSERT_NE(textureClear, std::string::npos);

    EXPECT_LT(release, meshCaches)
        << "the paperdoll must be released BEFORE RaceModelLoader's mesh "
           "caches are dropped -- otherwise characterNode_ outlives the "
           "meshes it renders";
    EXPECT_LT(release, textureClear)
        << "the paperdoll must be released BEFORE the constrained texture "
           "cache is cleared -- that clear() is what deletes the textures the "
           "T449 backtrace dereferences, and clearTextureReferences() cannot "
           "reach the doll's separate scene manager to null them";
}

// ---------------------------------------------------------------------------
// 2. What onZoneUnload() has to do: drop the model AND arm the rebuild.
//    Dropping alone would leave the doll permanently blank, because
//    updatePaperdollForFrame() only calls setCharacter() when the appearance
//    it reads differs from paperdollLastAppearance_ -- and a zone change does
//    not change the player's appearance.
// ---------------------------------------------------------------------------
TEST(PaperdollZoneUnloadOrder, OnZoneUnloadDropsTheModelAndArmsTheRebuild) {
    const std::string tu =
        ReadFile(kRoot + "/src/client/graphics/overlays/overlays_hud_env.cpp");
    const std::string body = BodyOf(tu, "void OverlaysComponent::onZoneUnload()");
    ASSERT_FALSE(body.empty());

    EXPECT_NE(body.find("releaseCharacterModel()"), std::string::npos)
        << "onZoneUnload() must drop the doll's model nodes";
    EXPECT_NE(body.find("paperdollHasAppearance_ = false;"), std::string::npos)
        << "onZoneUnload() must reset the appearance latch, or "
           "updatePaperdollForFrame() sees no change after zone-in and never "
           "rebuilds the model it just dropped";
}

// ---------------------------------------------------------------------------
// 3. What releaseCharacterModel() has to drop. The two weapon nodes are
//    children of characterNode_ and carry equipment textures out of the same
//    cache, so they go first and by name -- exactly as updateCharacterModel()
//    already does before it rebuilds.
// ---------------------------------------------------------------------------
TEST(PaperdollZoneUnloadOrder, ReleaseCharacterModelDropsWeaponsAndTheCharacter) {
    const std::string tu =
        ReadFile(kRoot + "/src/client/graphics/ui/character_model_view.cpp");

    // The teardown is factored into removeModelNodes(), which
    // updateCharacterModel() shares -- the two differ only in what they do
    // AFTER it, so the node removal must not be duplicated. The assertion
    // therefore follows the delegation rather than demanding the member names
    // appear literally in releaseCharacterModel()'s own body.
    const std::string release =
        BodyOf(tu, "void CharacterModelView::releaseCharacterModel()");
    ASSERT_FALSE(release.empty());
    EXPECT_NE(release.find("removeModelNodes();"), std::string::npos)
        << "releaseCharacterModel() must go through the shared node teardown";

    const std::string remove =
        BodyOf(tu, "void CharacterModelView::removeModelNodes()");
    ASSERT_FALSE(remove.empty());
    for (const char* member : {"primaryWeaponNode_", "secondaryWeaponNode_",
                               "characterNode_"}) {
        EXPECT_NE(remove.find(member), std::string::npos)
            << "removeModelNodes() must drop " << member;
    }

    EXPECT_NE(release.find("hasAppearance_ = false;"), std::string::npos)
        << "a released doll must not claim to have an appearance -- "
           "updateCharacterModel() returns early on !hasAppearance_ and would "
           "rebuild nothing";
    // ...and the reset must NOT be inside the shared teardown, or
    // setCharacter() -> updateCharacterModel() would clear the flag it just
    // set and build nothing.
    EXPECT_EQ(remove.find("hasAppearance_"), std::string::npos)
        << "removeModelNodes() must not touch hasAppearance_: "
           "updateCharacterModel() calls it AFTER setCharacter() has set the "
           "flag, and clearing it there would make every rebuild a no-op";
}
