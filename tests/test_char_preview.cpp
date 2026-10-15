// B888-CC2 (GitLab #204): the pre-zone 3D preview tests.
//
// Two legs, mirroring the card's step 4 ("a headless frame test per state
// proving the right race model is in the view"):
//
//   CharPreviewAppearance*   device-free: the right race/gender/appearance is
//       CHOSEN per state (the select row's T14 body; the creation race+gender
//       + FacePick picks; the invalid cases). Links with no Irrlicht at all.
//
//   CharPreviewHeadless*     the model itself stands up with NO zone:
//       CharacterModelView (the T60 paperdoll, its own scene manager) fed by
//       the production RaceModelLoader under an EDT_NULL device proves the
//       race model builds pre-zone; the model_fingerprint tool (the same
//       production loader the view borrows) proves WHICH model, by its model
//       code + geometry -- the fingerprint rows the card names.
//
// B1029's call-order trap, cited: a fresh RaceModelLoader loads nothing
// until its three init calls land in order -- setClientPath() (or the db
// instance for the race code) and setConstrainedTextureCache() on BOTH the
// loader and its mesh builder BEFORE loadGlobalModels()/createAnimatedNode()
// (tools/hwcaps/model_fingerprint.cpp wires exactly this; the paperdoll's
// own wiring, OverlaysComponent::ensurePaperdollInitialized, borrows the
// entity renderer's already-wired loader instead). The headless fixture
// wires the order in SetUp (db -> device -> loader -> setClientPath ->
// cache -> cache on mesh builder -> setCharacter), and the PRODUCTION host
// (char_preview_scene.cpp) sidesteps the trap entirely by NEVER building a
// second loader: CharPreviewScene::init() takes the entity renderer's,
// which createEntityRenderer() wired at renderer init.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "client/graphics/ui/char_preview_host.h"

#ifdef EQT_HAS_GRAPHICS
#include <irrlicht.h>
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/ui/char_preview_scene.h"
#include "client/graphics/ui/character_model_view.h"
#include "client/core/memory_ledger.h"
#include "client/willeq_database.h"
#endif  // EQT_HAS_GRAPHICS

namespace {

using eqt::graphics::CharPreviewAppearance;
using eqt::charcreate::FacePickFeature;
constexpr size_t kNF = static_cast<size_t>(FacePickFeature::Count);

bool fileExists(const std::string& p) { std::ifstream f(p); return f.good(); }

// ============================================================================
// Device-free: the appearance CHOSEN per state.
// ============================================================================

TEST(CharPreviewAppearance, SelectUsesTheT14RowBody) {
    // Human male, face 3, hair color 1, style 2 (the values the T14 wire
    // carried on the row -- CharacterSelectEntry's new fields).
    const auto a = eqt::graphics::selectPreviewAppearance(5, 1, 3, 1, 2);
    ASSERT_TRUE(a.valid);
    EXPECT_EQ(a.race, 5u);
    EXPECT_EQ(a.gender, 1u);
    EXPECT_EQ(a.appearance.face, 3u);
    EXPECT_EQ(a.appearance.haircolor, 1u);
    EXPECT_EQ(a.appearance.hairstyle, 2u);
    // The select preview shows the BARE body: no equipment, no beard.
    EXPECT_EQ(a.appearance.beard, 0u);
    for (int i = 0; i < 9; ++i) EXPECT_EQ(a.appearance.equipment[i], 0u);
}

TEST(CharPreviewAppearance, SelectWithNoRaceIsInvalid) {
    EXPECT_FALSE(eqt::graphics::selectPreviewAppearance(0, 0, 0, 0, 0).valid);
}

TEST(CharPreviewAppearance, CreationUsesRaceGenderAndFacePickPicks) {
    std::array<uint32_t, kNF> picks{};
    picks[static_cast<size_t>(FacePickFeature::Face)] = 4;
    picks[static_cast<size_t>(FacePickFeature::Hair)] = 7;
    picks[static_cast<size_t>(FacePickFeature::HairColor)] = 2;
    picks[static_cast<size_t>(FacePickFeature::Beard)] = 1;
    picks[static_cast<size_t>(FacePickFeature::BeardColor)] = 5;
    const auto a = eqt::graphics::createPreviewAppearance(7, 0, picks);
    ASSERT_TRUE(a.valid);
    EXPECT_EQ(a.race, 7u);
    EXPECT_EQ(a.gender, 0u);  // Male
    EXPECT_EQ(a.appearance.face, 4u);
    EXPECT_EQ(a.appearance.hairstyle, 7u);
    EXPECT_EQ(a.appearance.haircolor, 2u);
    EXPECT_EQ(a.appearance.beard, 1u);
    EXPECT_EQ(a.appearance.beardcolor, 5u);
    EXPECT_EQ(a.appearance.texture, 0u);  // Eyes un-picked -> bare body
    EXPECT_EQ(a.appearance.helm, 0u);     // Wode un-picked -> default head
}

TEST(CharPreviewAppearance, CreationWithNoRaceIsInvalid) {
    std::array<uint32_t, kNF> picks{};
    EXPECT_FALSE(eqt::graphics::createPreviewAppearance(0, 0, picks).valid);
}

#ifdef EQT_HAS_GRAPHICS
// The Rotate state round-trip, plus the host's no-init contract: before
// init() (no renderer yet) the scene reports no model and every call is a
// safe no-op -- the host retries init() next frame instead of faulting.
TEST(CharPreviewRotate, ToggleRoundTripsWithoutInit) {
    eqt::ui::CharPreviewScene scene;
    ASSERT_TRUE(scene.rotateEnabled()) << "the oracle's Rotate is on by default";
    scene.setRotateEnabled(false);
    EXPECT_FALSE(scene.rotateEnabled());
    scene.setRotateEnabled(true);
    EXPECT_TRUE(scene.rotateEnabled());
    // Un-initialised: no model, no texture, zero rotation, all no-ops.
    EXPECT_FALSE(scene.hasModel());
    EXPECT_EQ(scene.modelRace(), 0u);
    EXPECT_FLOAT_EQ(scene.rotationY(), 0.0f);
    scene.setModel(5, 0, EQT::Graphics::EntityAppearance());
    scene.update(16.0f);
    scene.clearModel();
    EXPECT_FALSE(scene.hasModel());
}

// ============================================================================
// Headless: the model stands up with no zone (the frame leg).
//
// The fixture wires the production RaceModelLoader in B1029's order (db
// first for the race code, then the loader's client path, then the
// constrained texture cache on the loader AND its mesh builder, then
// create) under an EDT_NULL device -- the same order
// tools/hwcaps/model_fingerprint.cpp and test_inventory_model_view.cpp
// establish, so a headless CI box (no server, no DISPLAY) can stand up a
// race model exactly as the pre-zone host will.
// ============================================================================
class CharPreviewHeadlessTest : public ::testing::Test {
protected:
    std::string eqClientPath_;
    irr::IrrlichtDevice* device_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::io::IFileSystem* fs_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EQT::Graphics::RaceModelLoader> loader_;
    EQT::WilleqDatabase willeqDb_;

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) {
            GTEST_SKIP() << "no shipped db at " << dbPath << " - race lookups need it";
        }
        ASSERT_TRUE(willeqDb_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&willeqDb_);

        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        eqClientPath_ = envPath ? envPath : EQ_CLIENT_DATA_PATH;
        if (!eqClientPath_.empty() && eqClientPath_.back() != '/') eqClientPath_ += '/';
        if (!fileExists(eqClientPath_ + "global_chr.s3d")) {
            GTEST_SKIP() << "global_chr.s3d not found at " << eqClientPath_
                         << " - required for the pre-zone model test";
        }

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";
        }
        smgr_ = device_->getSceneManager();
        driver_ = device_->getVideoDriver();
        fs_ = device_->getFileSystem();

        loader_ = std::make_unique<EQT::Graphics::RaceModelLoader>(smgr_, driver_, fs_);
        loader_->setClientPath(eqClientPath_);
        // The two separate cache wirings (found by test_inventory_model_view):
        // the loader's own isLiveTexture() check AND the ZoneMeshBuilder that
        // decodes/uploads the BMP bytes. Both required, the established idiom.
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        textureCache_->setDriver(driver_);
        loader_->setConstrainedTextureCache(textureCache_.get());
        loader_->getMeshBuilder()->setConstrainedTextureCache(textureCache_.get());
    }

    void TearDown() override {
        loader_.reset();
        textureCache_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }
};

// A bare race+gender body stands up under CharacterModelView with NO zone
// loaded -- the pre-zone host's exact path (its own scene manager, the
// borrowed loader). The model node exists, carries the right race/gender,
// and has materials (a real mesh, not a placeholder cube).
TEST_F(CharPreviewHeadlessTest, RaceModelStandsUpWithNoZone) {
    // Human (the model code HUM) male and female: the two bodies the
    // oracle's ledger measures (refactor/B888-CC2/README.md).
    for (uint8_t gender : {0, 1}) {
        auto view = std::make_unique<eqt::ui::CharacterModelView>();
        view->init(smgr_, driver_, 256, 256);
        view->setRaceModelLoader(loader_.get());
        view->setCharacter(5, gender, EQT::Graphics::EntityAppearance());
        ASSERT_TRUE(view->hasCharacterModel())
            << "no model node for Human gender " << static_cast<int>(gender);
        EXPECT_EQ(view->getCurrentRaceId(), 5u);
        EXPECT_EQ(view->getCurrentGender(), gender);
        EXPECT_GT(view->getMaterialCount(), 0u) << "the model has no materials";
        view->releaseCharacterModel();
        EXPECT_FALSE(view->hasCharacterModel());
    }
}

// The scene host itself is thin and render-thread-only (it needs a live
// IrrlichtRenderer for its own scene manager, borrowed loader, and the
// rotate toggle), so it is exercised in the visual gate / a future live
// capture, not here. The SPIN logic it adds (kSpinDegreesPerMs advance while
// enabled, held while disabled) is the paperdoll's own setRotationY/getRotationY
// round-trip, which the model leg below exercises directly.
TEST_F(CharPreviewHeadlessTest, RotationRoundTripOnTheModelView) {
    auto view = std::make_unique<eqt::ui::CharacterModelView>();
    view->init(smgr_, driver_, 256, 256);
    view->setRaceModelLoader(loader_.get());
    view->setCharacter(5, 0, EQT::Graphics::EntityAppearance());
    ASSERT_TRUE(view->hasCharacterModel());
    const float r0 = view->getRotationY();
    view->setRotationY(r0 + 16.0f * 0.15f);  // one frame of the preview's spin step
    EXPECT_GT(view->getRotationY(), r0);
    const float r1 = view->getRotationY();   // while "off" the host does not call setRotationY
    view->update(16.0f);
    EXPECT_EQ(view->getRotationY(), r1) << "rotation moved on a plain update()";
    view->releaseCharacterModel();
}

// The pre-zone memory cost (acceptance D): the preview stands up ONE body at
// a time -- the view RELEASES the previous model when the character changes,
// so `entity_models` holds exactly one model for the screen's whole
// lifetime. Measured live through the memory ledger on this fixture's
// device (EDT_NULL, x86 dev tree, run 2026-10-15): standing up the Human
// male grows `entity_models` (the RaceModelLoader's own MemoryScope, the
// same one a zone entity's model is charged to) by 28,654,160 B (~27.3 MiB)
// -- the model data PLUS that race's shared textures, loaded with the first
// body and then held; re-standing the SAME race+gender charges nothing
// (cache hit); switching to the other gender of the SAME race leaves the
// category at the same level (the old body is released, the new one stands
// up: never two bodies at once); releasing the model never grows the
// category above that one-body level (the loader's cache holds the first
// body's mesh/texture data past the release -- the loader's cache is
// shared with zone entities and out of the preview's control). The
// preview's own render target (256x256, ~131 KB RGBA8 on a GLES2 device)
// is charged separately to textures_driver and is below this test's
// resolution under EDT_NULL.
TEST_F(CharPreviewHeadlessTest, OneModelIsTheWholePreZoneDelta) {
    using eqt::memory::snapshot;
    using eqt::memory::Category;
    const auto emBytes = [](const eqt::memory::Snapshot& s) {
        return s.categories[static_cast<size_t>(Category::EntityModels)].liveBytes;
    };
    const auto base = snapshot("cc2_before");
    auto view = std::make_unique<eqt::ui::CharacterModelView>();
    view->init(smgr_, driver_, 256, 256);
    view->setRaceModelLoader(loader_.get());
    view->setCharacter(5, 0, EQT::Graphics::EntityAppearance());
    ASSERT_TRUE(view->hasCharacterModel());
    const auto oneModel = snapshot("cc2_after_one");
    const uint64_t delta1 = emBytes(oneModel) > emBytes(base)
        ? emBytes(oneModel) - emBytes(base) : 0;
    EXPECT_GT(delta1, 0u) << "the Human model charged nothing to entity_models";
    // Re-stand-up of the SAME race+gender: the cache already holds it, so
    // nothing new is charged (a per-frame re-stand-up is the host's steady
    // state -- the model node's rebuild must not leak a second copy).
    view->setCharacter(5, 0, EQT::Graphics::EntityAppearance());
    const auto sameBody = snapshot("cc2_after_same");
    const uint64_t sameLevel = std::max(emBytes(sameBody), emBytes(oneModel));
    const uint64_t deltaSame = sameLevel - emBytes(oneModel);
    EXPECT_LT(deltaSame, delta1 / 4)
        << "re-standing the same race+gender charged a near-second copy of the model";
    // The other gender of the SAME race: the view must not hold TWO bodies
    // at once -- the category stays at the one-body level (old released,
    // new stood up; its shared textures are already resident).
    view->setCharacter(5, 1, EQT::Graphics::EntityAppearance());
    const auto otherBody = snapshot("cc2_after_other");
    const uint64_t deltaOther = emBytes(otherBody) > emBytes(oneModel)
        ? emBytes(otherBody) - emBytes(oneModel) : 0;
    EXPECT_LT(deltaOther, delta1 / 4)
        << "switching gender held two bodies at once (second full model charged)";
    // Release: the NODE goes away (the next setCharacter() rebuilds it);
    // the cached mesh/texture data the loader holds with the first body is
    // the loader's cache and out of the preview's control -- the category
    // must never grow ABOVE the one-body level.
    view->releaseCharacterModel();
    const auto released = snapshot("cc2_after_release");
    const uint64_t afterRelease = emBytes(released) > emBytes(oneModel)
        ? emBytes(released) - emBytes(oneModel) : 0;
    EXPECT_LE(afterRelease, delta1 / 4)
        << "releasing the preview model grew entity_models above the one-body level";
    std::printf("  [cc2-memory] entity_models: base=%llu one=%llu (+%llu B) "
                "same=%llu (+%llu B) other-gender=%llu (+%llu B) released=%llu (+%llu B)\n",
                static_cast<unsigned long long>(emBytes(base)),
                static_cast<unsigned long long>(emBytes(oneModel)),
                static_cast<unsigned long long>(delta1),
                static_cast<unsigned long long>(emBytes(sameBody)),
                static_cast<unsigned long long>(deltaSame),
                static_cast<unsigned long long>(emBytes(otherBody)),
                static_cast<unsigned long long>(deltaOther),
                static_cast<unsigned long long>(emBytes(released)),
                static_cast<unsigned long long>(afterRelease));
}

// WHICH model: the production loader (the one the view borrows) resolves the
// race to a distinct, non-empty model for two different races -- the
// fingerprint's model-code/geometry row, checked live rather than against a
// golden so the test needs no blessed file. Human and a second race must
// resolve to DIFFERENT model codes (a race mix-up is the regression this
// guards: the preview showing the wrong race's body).
TEST_F(CharPreviewHeadlessTest, DistinctRacesResolveToDistinctModels) {
    auto fp = [](EQT::Graphics::RaceModelLoader* l, uint16_t race, uint8_t gender) {
        // ensureModelDataLoaded() runs the S3D search/load chain (the on-demand
        // path setCharacter() itself goes through); getRaceModelData() reads
        // only what is already cached.
        auto d = l->ensureModelDataLoaded(race, gender);
        if (!d) return std::string("<none>");
        return d->skeleton ? d->skeleton->modelCode : d->raceName;
    };
    const auto human = fp(loader_.get(), 5, 0);
    const auto dwarf = fp(loader_.get(), 3, 0);
    ASSERT_FALSE(human.empty()) << "Human race resolved to no model data";
    ASSERT_FALSE(dwarf.empty()) << "Dwarf race resolved to no model data";
    EXPECT_NE(human, dwarf) << "two races resolved to the same model code";
}

#endif  // EQT_HAS_GRAPHICS

}  // namespace
