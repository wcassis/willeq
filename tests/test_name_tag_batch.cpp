// B975 (GitLab #293): the entity name-tag batch.
//
// N tags under N entity nodes, rendered once as the pre-B975 CTextSceneNodes
// and once as NameTagNodes through NameTagBatchNode, on a real headless GLES2
// device (surfaceless EGL / llvmpipe -- test_static_ui_frame_cost.cpp's
// harness). The nets:
//  - the two framebuffers are pixel-identical (overlapping tags, every con
//    colour, a non-ASCII name that takes the font->draw() fallback, a tag
//    behind the camera, a hidden tag);
//  - the batch issues TWO draw calls whatever N is (the first glyph through
//    the driver, every other glyph of every tag in one glDrawElements) and
//    one quad per drawn glyph; a fallback tag costs its own draw and splits
//    the run;
//  - setText() relays the tag out; a tag re-registered from outside its
//    parent (ImpostorBatchNode's path for withheld entities) is drawn;
//  - the buffer bytes the owner row reports cover the quads drawn.
// Self-skips when no GLES2 device is available.

#include <gtest/gtest.h>

#include "client/graphics/name_tag_batch.h"

#include <irrlicht.h>
#include <string>
#include <utility>
#include <vector>

using namespace EQT::Graphics;

namespace {

const irr::video::SColor kClear(255, 40, 40, 60);

const wchar_t* kNames[] = {L"a gnoll pup", L"Guard Hezlan", L"a large rat", L"Fippy Darkpaw",
                           L"a decaying skeleton", L"Merchant Tonlar", L"Testchar", L"Wolf"};
const irr::video::SColor kColors[] = {{255, 255, 255, 255}, {255, 128, 128, 128}, {255, 0, 200, 0},
                                      {255, 0, 160, 255},   {255, 255, 255, 0},   {255, 255, 0, 0},
                                      {255, 100, 200, 255}};

class NameTagBatchTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::gui::IGUIFont* font_ = nullptr;
    NameTagBatchNode* batch_ = nullptr;
    std::vector<irr::scene::ISceneNode*> entities_;
    std::vector<irr::scene::ITextSceneNode*> tags_;

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(800, 600);
        params.Fullscreen = true;
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_OGLES2 device (surfaceless EGL unavailable)";
        driver_ = device_->getVideoDriver();
        smgr_ = device_->getSceneManager();
        font_ = device_->getGUIEnvironment()->getBuiltInFont();
        ASSERT_NE(font_, nullptr);
        auto* cam = smgr_->addCameraSceneNode(nullptr, irr::core::vector3df(0, 6, 0),
                                              irr::core::vector3df(0, 6, 100));
        cam->setNearValue(1.0f);
        cam->setFarValue(2000.0f);
        batch_ = new NameTagBatchNode(smgr_->getRootSceneNode(), smgr_, font_);
    }

    void TearDown() override {
        if (batch_) { batch_->remove(); batch_->drop(); }
        if (device_) device_->drop();
    }

    // n entities on a 3-unit grid between nearD and farD ahead.
    void place(int n, float nearD, float farD) {
        int cols = 1;
        while (cols * cols < n) ++cols;
        const float step = cols > 1 ? (farD - nearD) / static_cast<float>(cols - 1) : 0.0f;
        for (int i = 0; i < n; ++i) {
            auto* e = smgr_->addEmptySceneNode(nullptr);
            e->setPosition(irr::core::vector3df((static_cast<float>(i % cols) - (cols - 1) * 0.5f) * 3.0f, 0.0f,
                                                nearD + step * static_cast<float>(i / cols)));
            entities_.push_back(e);
        }
    }

    const wchar_t* nameOf(size_t i) const {
        // One name the glyph table cannot take (U+00E9): the font->draw() fallback.
        if (i == 3) return L"Café Owner";
        return kNames[i % (sizeof(kNames) / sizeof(kNames[0]))];
    }

    void makeTags(bool batched) {
        for (auto* t : tags_) t->remove();
        tags_.clear();
        for (size_t i = 0; i < entities_.size(); ++i) {
            const irr::video::SColor c = kColors[i % (sizeof(kColors) / sizeof(kColors[0]))];
            const irr::core::vector3df off(0.0f, 5.5f, 0.0f);
            if (batched) {
                auto* node = new NameTagNode(entities_[i], smgr_, batch_, nameOf(i), c, off);
                node->drop();
                tags_.push_back(node);
            } else {
                tags_.push_back(smgr_->addTextSceneNode(font_, nameOf(i), c, entities_[i], off));
            }
        }
        if (tags_.size() > 5) tags_[5]->setVisible(false);  // a hidden tag stays hidden
    }

    irr::video::IImage* render() {
        for (int k = 0; k < 2; ++k) {  // the surfaceless harness's first-frame quirk
            driver_->beginScene(true, true, kClear);
            smgr_->drawAll();
            driver_->endScene();
        }
        driver_->beginScene(true, true, kClear);
        smgr_->drawAll();
        irr::video::IImage* img = driver_->createScreenShot();
        driver_->endScene();
        return img;
    }

    static void compare(irr::video::IImage* a, irr::video::IImage* b, size_t* differing, size_t* painted) {
        *differing = 0;
        *painted = 0;
        const auto d = a->getDimension();
        for (irr::u32 y = 0; y < d.Height; ++y) {
            for (irr::u32 x = 0; x < d.Width; ++x) {
                const irr::u32 ca = a->getPixel(x, y).color & 0x00FFFFFF;
                if (ca != (kClear.color & 0x00FFFFFF)) ++*painted;
                if (ca != (b->getPixel(x, y).color & 0x00FFFFFF)) ++*differing;
            }
        }
    }

};

}  // namespace

TEST_F(NameTagBatchTest, BatchIsPixelIdenticalToPerNodeTextSceneNodes) {
    struct Pose { int n; float nearD, farD; bool allOnScreen; };
    for (const Pose& p : {Pose{10, 20.0f, 60.0f, true}, Pose{200, 115.0f, 160.0f, true},
                          Pose{60, 4.0f, 46.0f, false}}) {
        for (auto* e : entities_) e->remove();
        entities_.clear();
        tags_.clear();
        place(p.n, p.nearD, p.farD);
        // One entity behind the camera: the per-node path drew it at -10000.
        entities_.back()->setPosition(irr::core::vector3df(0.0f, 0.0f, -30.0f));

        // The batch node is hidden for the per-node frame: the scene manager's
        // heapsort then sees the same set of transparent nodes in the same
        // order as the batch's own sort, so even exactly-equidistant tags (the
        // mirrored columns of this grid) land in the same order.
        batch_->setVisible(false);
        makeTags(false);
        irr::video::IImage* before = render();
        batch_->setVisible(true);
        makeTags(true);
        irr::video::IImage* after = render();
        const NameTagBatchNode::FrameStats st = batch_->lastFrame();

        size_t differing = 0, painted = 0;
        compare(before, after, &differing, &painted);
        before->drop();
        after->drop();
        SCOPED_TRACE("n=" + std::to_string(p.n) + " " + std::to_string(p.nearD) + "-" + std::to_string(p.farD));
        EXPECT_GT(painted, 50u * static_cast<size_t>(std::min(p.n, 10))) << "the frame drew no tags";
        EXPECT_EQ(differing, 0u);
        // Two draws for every batchable tag together, plus one per fallback tag.
        // A fallback tag in the middle of the order splits the run: the
        // glyphs before it (2 draws), its font->draw() (1), the rest (2).
        EXPECT_LE(st.fallbackTags, 1u);
        EXPECT_LE(st.driverDraws, 2u + 3u * st.fallbackTags);
        if (p.allOnScreen) {
            EXPECT_EQ(st.fallbackTags, 1u);
            EXPECT_EQ(st.driverDraws, 5u);
            EXPECT_EQ(st.tags, static_cast<size_t>(p.n) - 2u);  // hidden + behind the camera
        }
    }
}

TEST_F(NameTagBatchTest, TwoDrawsAndOneQuadPerGlyphWhateverTheCount) {
    for (int n : {1, 50, 200}) {
        for (auto* e : entities_) e->remove();
        entities_.clear();
        tags_.clear();
        place(n, 115.0f, 160.0f);
        makeTags(true);
        // No fallback name this time.
        if (n > 3) tags_[3]->setText(L"Fippy Darkpaw");
        irr::video::IImage* img = render();
        img->drop();
        const NameTagBatchNode::FrameStats st = batch_->lastFrame();
        SCOPED_TRACE("n=" + std::to_string(n));
        EXPECT_EQ(st.fallbackTags, 0u);
        EXPECT_EQ(st.driverDraws, 2u);
        size_t quads = 0;
        for (size_t i = 0; i < tags_.size(); ++i) {
            if (i == 5) continue;
            const wchar_t* name = (i == 3) ? L"Fippy Darkpaw" : nameOf(i);
            for (const wchar_t* c = name; *c; ++c)
                if (*c != L' ') ++quads;
        }
        EXPECT_EQ(st.glyphQuads, quads);
        // The owner row's bytes hold at least this frame's compact quads
        // (20 B vertex x 4 + 6 u16 indices a glyph; the run's first glyph
        // goes through the driver, so quads - 1 of them are in the buffers).
        EXPECT_GE(batch_->bufferBytes(), (quads - 1) * (4 * 20 + 6 * 2));
    }
}

TEST_F(NameTagBatchTest, ReRegisteredTagOfAWithheldParentIsDrawn) {
    place(1, 30.0f, 30.0f);
    makeTags(true);
    // ImpostorBatchNode's path: the parent is not drawn, its tag is registered
    // by the batch node directly.
    entities_[0]->setVisible(false);
    driver_->beginScene(true, true, kClear);
    smgr_->drawAll();
    driver_->endScene();
    EXPECT_EQ(batch_->lastFrame().tags, 0u);

    struct Reregister : irr::scene::ISceneNode {
        irr::scene::ISceneNode* tag;
        Reregister(irr::scene::ISceneManager* m, irr::scene::ISceneNode* t)
            : irr::scene::ISceneNode(m->getRootSceneNode(), m, -1), tag(t) {}
        void OnRegisterSceneNode() override {
            tag->OnRegisterSceneNode();
            ISceneNode::OnRegisterSceneNode();
        }
        void render() override {}
        const irr::core::aabbox3d<irr::f32>& getBoundingBox() const override { return box; }
        irr::core::aabbox3d<irr::f32> box;
    };
    auto* rr = new Reregister(smgr_, tags_[0]);
    tags_[0]->updateAbsolutePosition();
    driver_->beginScene(true, true, kClear);
    smgr_->drawAll();
    driver_->endScene();
    EXPECT_EQ(batch_->lastFrame().tags, 1u);
    EXPECT_EQ(batch_->lastFrame().driverDraws, 2u);
    rr->remove();
    rr->drop();
}

TEST_F(NameTagBatchTest, SetTextRelaysTheTagOut) {
    place(1, 30.0f, 30.0f);
    makeTags(true);
    irr::video::IImage* img = render();
    img->drop();
    EXPECT_EQ(batch_->lastFrame().glyphQuads, 9u);  // "a gnoll pup"
    tags_[0]->setText(L"Wolf");
    img = render();
    img->drop();
    EXPECT_EQ(batch_->lastFrame().glyphQuads, 4u);
    EXPECT_EQ(static_cast<NameTagNode*>(tags_[0])->text(), std::wstring(L"Wolf"));
}

TEST_F(NameTagBatchTest, ReusedIndicesHandleChunkBoundariesAndShorterRuns) {
    place(2, 30.0f, 40.0f);
    entities_[1]->setPosition(irr::core::vector3df(0.0f, 2.0f, 45.0f));
    // Each per-node string fits its own 16-bit draw; their batch crosses the
    // chunk boundary. Later frames must not draw the retained index suffix.
    for (const auto& lengths : {std::pair<size_t, size_t>{9000, 9000}, {4, 1},
                                {1, 0}, {0, 0}, {4, 9}}) {
        SCOPED_TRACE(std::to_string(lengths.first) + "/" + std::to_string(lengths.second));
        const std::wstring names[] = {std::wstring(lengths.first, L'A'),
                                      std::wstring(lengths.second, L'W')};
        batch_->setVisible(false);
        makeTags(false);
        for (size_t i = 0; i < tags_.size(); ++i) tags_[i]->setText(names[i].c_str());
        irr::video::IImage* before = render();
        batch_->setVisible(true);
        makeTags(true);
        for (size_t i = 0; i < tags_.size(); ++i) tags_[i]->setText(names[i].c_str());
        irr::video::IImage* after = render();
        size_t differing = 0, painted = 0;
        compare(before, after, &differing, &painted);
        before->drop();
        after->drop();
        EXPECT_EQ(differing, 0u);
        const size_t quads = lengths.first + lengths.second;
        EXPECT_EQ(batch_->lastFrame().glyphQuads, quads);
        EXPECT_EQ(batch_->lastFrame().driverDraws,
                  quads ? 1u + (quads - 1u + 16383u) / 16384u : 0u);
        if (quads) EXPECT_GT(painted, 0u);
        else EXPECT_EQ(painted, 0u);
    }
}
