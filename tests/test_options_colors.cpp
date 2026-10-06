// B993 (#313) -- the Options window's Colors page: the user chat-colour
// table, its [TextColors] persistence, the colour picker and the chat line
// drawn in the chosen colour.
//
// What is pinned:
//   * the table's defaults ARE eqclient.ini's [TextColors] (compared to the
//     shipped file when it is present) and the row <-> chat type mapping;
//   * click a row -> Change Selected Color -> a picker swatch -> Accept
//     changes exactly that row; the close box changes nothing; Reset restores;
//   * the table round-trips through UI_<char>_<server>.ini under the oracle's
//     own User_<N>_Red/_Green/_Blue keys, and an official [TextColors] loads;
//   * `/option textcolor` reaches the same table;
//   * a chat line is DRAWN in the row's colour (real GLES2 pixels; self-skips
//     with no device or no willeq.eqe1).

#include <gtest/gtest.h>

#include "client/chat/chat_message_buffer.h"
#include "client/chat/chat_text_colors.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/text_batch.h"
#include "client/graphics/ui/color_picker.h"
#include "client/graphics/ui/options_classification.h"
#include "client/graphics/ui/options_command.h"
#include "client/graphics/ui/options_layout.h"
#include "client/graphics/ui/options_panel.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/ui_preferences.h"
#include "client/graphics/ui/ui_renderer.h"

#include <irrlicht.h>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>

using namespace EQT::Graphics;
namespace OL = EQT::Graphics::OptionsLayout;
using eqt::ui::ChatChannel;
using eqt::ui::UserColor;

namespace {

UILayout layout800() {
    UILayout l;
    l.computeLayout(800, 600);
    return l;
}

int controlIndex(const char* xmlItem) {
    for (int i = 0; i < OL::CONTROL_COUNT; ++i) {
        if (std::string(OL::kControls[i].xmlItem) == xmlItem) return i;
    }
    return -1;
}

irr::core::vector2di centre(const irr::core::rect<irr::s32>& r) {
    return irr::core::vector2di((r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2,
                                (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2);
}

OL::Setting click(OptionsPanelState& s, int x, int y) {
    StaticUIInput input;
    OL::Setting changed = OL::Setting::None;
    input.processOptionsInput(x, y, true, layout800(), s, changed);
    return changed;
}

OL::Setting clickAt(OptionsPanelState& s, const irr::core::rect<irr::s32>& r) {
    const auto p = centre(r);
    return click(s, p.X, p.Y);
}

OptionsPanelState colorsPage() {
    OptionsPanelState s;
    s.isOpen = true;
    s.activeTab = static_cast<int>(OL::Tab::Colors);
    return s;
}

// The rect of list row `n` (1-based) on the Colors page, scrolling to it.
irr::core::rect<irr::s32> rowRect(OptionsPanelState& s, int n) {
    const int i = controlIndex("OCP_ColorChooseList");
    const int rows = OL::listVisibleRows(layout800(), i);
    if (n < s.colorListTop || n >= s.colorListTop + rows) s.colorListTop = n;
    return OL::listRowRect(layout800(), i, n - s.colorListTop);
}

UserColor rgb(int r, int g, int b) {
    UserColor c;
    c.r = static_cast<uint8_t>(r);
    c.g = static_cast<uint8_t>(g);
    c.b = static_cast<uint8_t>(b);
    return c;
}

OptionsPanelState roundTrip(OptionsPanelState& s) {
    UiPreferencesBinding b;
    b.options = &s;
    const std::string text = SerializeUiPreferences(CollectUiPreferences(b, 1));
    OptionsPanelState back;
    UiPreferencesBinding b2;
    b2.options = &back;
    ApplyUiPreferences(ParseUiPreferences(text), b2);
    return back;
}

}  // namespace

// ===========================================================================
// The table
// ===========================================================================

TEST(ChatTextColors, TheDefaultsAreEqclientIniTextColorsRowForRow) {
    const auto& d = eqt::ui::defaultChatTextColors();
    // Spot rows, quoted from /mnt/projects/EverQuestP1999/eqclient.ini.
    EXPECT_EQ(d[0], rgb(255, 255, 255));   // User_1 Say
    EXPECT_EQ(d[1], rgb(190, 40, 190));    // User_2 Tell
    EXPECT_EQ(d[2], rgb(0, 255, 255));     // User_3 Group
    EXPECT_EQ(d[3], rgb(40, 240, 40));     // User_4 Guild
    EXPECT_EQ(d[85], rgb(255, 155, 155));  // User_86

    std::ifstream in("/mnt/projects/EverQuestP1999/eqclient.ini");
    if (!in) GTEST_SKIP() << "the oracle's eqclient.ini is not on this host";
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string text = buf.str();
    const std::regex key(R"(User_(\d+)_(Red|Green|Blue)=(\d+))");
    int seen = 0;
    for (std::sregex_iterator it(text.begin(), text.end(), key), end; it != end; ++it) {
        const int n = std::stoi((*it)[1]);
        const int v = std::stoi((*it)[3]);
        ASSERT_GE(n, 1);
        ASSERT_LE(n, eqt::ui::kUserColorCount);
        const UserColor& c = d[n - 1];
        const std::string ch = (*it)[2];
        EXPECT_EQ(ch == "Red" ? c.r : ch == "Green" ? c.g : c.b, v) << "User_" << n << "_" << ch;
        ++seen;
    }
    EXPECT_EQ(seen, eqt::ui::kUserColorCount * 3) << "eqclient.ini has 86 rows of three";
}

TEST(ChatTextColors, RowNIsChatType255PlusNAndItsEqstrName) {
    EXPECT_EQ(eqt::ui::userColorForChatType(256), 1);   // Chat::Say
    EXPECT_EQ(eqt::ui::userColorForChatType(257), 2);   // Chat::Tell
    EXPECT_EQ(eqt::ui::userColorForChatType(341), 86);
    EXPECT_EQ(eqt::ui::userColorForChatType(342), 0);
    EXPECT_EQ(eqt::ui::userColorForChatType(255), 0);   // a raw colour, not a row
    EXPECT_EQ(eqt::ui::userColorForChatType(15), 0);
    EXPECT_STREQ(eqt::ui::userColorName(1), "Say");           // eqstr 6200
    EXPECT_STREQ(eqt::ui::userColorName(10), "You hit other");  // eqstr 6209
    EXPECT_STREQ(eqt::ui::userColorName(82), "Pet Responses");  // eqstr 6281
    EXPECT_STREQ(eqt::ui::userColorName(87), "");
}

TEST(ChatTextColors, ChannelsMapToTheirRowAndTheServerTypeWins) {
    EXPECT_EQ(eqt::ui::userColorForChannel(ChatChannel::Say), 1);
    EXPECT_EQ(eqt::ui::userColorForChannel(ChatChannel::Tell), 2);
    EXPECT_EQ(eqt::ui::userColorForChannel(ChatChannel::Group), 3);
    EXPECT_EQ(eqt::ui::userColorForChannel(ChatChannel::Guild), 4);
    EXPECT_EQ(eqt::ui::userColorForChannel(ChatChannel::Shout), 7);
    // Mixed channels name no row and keep their compiled colour.
    EXPECT_EQ(eqt::ui::userColorForChannel(ChatChannel::CombatSelf), 0);
    EXPECT_EQ(eqt::ui::userColorForChannel(ChatChannel::Error), 0);
    EXPECT_EQ(eqt::ui::userColorFor(ChatChannel::System, 284), 29);  // Spell worn off
    EXPECT_EQ(eqt::ui::userColorFor(ChatChannel::System, 0), 80);
}

TEST(ChatTextColors, TheBufferStampsTheRowAndTheResolverReadsTheTable) {
    eqt::ui::ChatMessageBuffer buf(10);
    eqt::ui::ChatMessage m;
    m.channel = ChatChannel::Tell;
    m.text = "hi";
    buf.addMessage(m);
    buf.processPending();
    ASSERT_EQ(buf.size(), 1u);
    EXPECT_EQ(buf.getMessages()[0].userColor, 2);

    eqt::ui::ChatTextColors t = eqt::ui::defaultChatTextColors();
    t[1] = rgb(1, 2, 3);
    EXPECT_EQ(eqt::ui::resolveChatLineColor(&t, 2, 0xFF123456u), 0xFF010203u);
    EXPECT_EQ(eqt::ui::resolveChatLineColor(&t, 0, 0xFF123456u), 0xFF123456u);
    EXPECT_EQ(eqt::ui::resolveChatLineColor(nullptr, 2, 0xFF123456u), 0xFF123456u);
}

// ===========================================================================
// The page and the picker
// ===========================================================================

TEST(OptionsColors, TheThreeColorsRowsAreWired) {
    for (const char* item : {"OCP_UserColorDefault", "OCP_ColorChooseList", "OCP_UserColorChange"}) {
        const auto* cls = OL::findControlClass(item);
        ASSERT_NE(cls, nullptr) << item;
        EXPECT_EQ(cls->disposition, OL::Disposition::Wired) << item;
        EXPECT_NE(OL::kControls[controlIndex(item)].setting, OL::Setting::None) << item;
    }
}

TEST(OptionsColors, RowThenChangeThenSwatchThenAcceptChangesThatRowOnly) {
    OptionsPanelState s = colorsPage();
    EXPECT_EQ(clickAt(s, rowRect(s, 2)), OL::Setting::None);  // select Tell
    EXPECT_EQ(s.colorSelectedRow, 2);
    clickAt(s, OL::controlRect(layout800(), controlIndex("OCP_UserColorChange")));
    ASSERT_TRUE(s.colorPicker.isOpen);
    EXPECT_EQ(s.colorPicker.row, 2);
    EXPECT_EQ(s.colorPicker.working, rgb(190, 40, 190)) << "opens on the row's colour";

    // CPW_TC1Button (0,240,0), then Accept.
    EXPECT_EQ(clickAt(s, ColorPickerLayout::swatchRect(layout800(), 1)), OL::Setting::None);
    EXPECT_EQ(s.colorPicker.working, rgb(0, 240, 0));
    EXPECT_EQ(s.textColors[1], rgb(190, 40, 190)) << "nothing written before Accept";
    EXPECT_EQ(clickAt(s, ColorPickerLayout::acceptRect(layout800())), OL::Setting::TextColors);
    EXPECT_FALSE(s.colorPicker.isOpen);
    EXPECT_EQ(s.textColors[1], rgb(0, 240, 0));
    for (int n = 1; n <= eqt::ui::kUserColorCount; ++n) {
        if (n != 2) EXPECT_EQ(s.textColors[n - 1], eqt::ui::defaultChatTextColors()[n - 1]) << n;
    }
}

TEST(OptionsColors, TheSlidersSetOneChannelAndTheCloseBoxCancels) {
    OptionsPanelState s = colorsPage();
    s.colorSelectedRow = 7;
    clickAt(s, OL::controlRect(layout800(), controlIndex("OCP_UserColorChange")));
    ASSERT_TRUE(s.colorPicker.isOpen);
    const auto green = ColorPickerLayout::sliderRect(layout800(), 1);
    click(s, green.UpperLeftCorner.X, centre(green).Y);
    EXPECT_EQ(s.colorPicker.working.g, 0);
    click(s, green.LowerRightCorner.X - 1, centre(green).Y);
    EXPECT_EQ(s.colorPicker.working.g, 255);
    EXPECT_EQ(s.colorPicker.working.r, 255) << "Shout's red is untouched";
    EXPECT_EQ(clickAt(s, ColorPickerLayout::closeBoxRect(layout800())), OL::Setting::None);
    EXPECT_FALSE(s.colorPicker.isOpen);
    EXPECT_EQ(s.textColors[6], rgb(255, 0, 0)) << "the close box writes nothing";
}

TEST(OptionsColors, ResetRestoresTheDefaultsAndTheListScrolls) {
    OptionsPanelState s = colorsPage();
    s.textColors[0] = rgb(9, 9, 9);
    EXPECT_EQ(clickAt(s, OL::controlRect(layout800(), controlIndex("OCP_UserColorDefault"))),
              OL::Setting::TextColorReset);
    EXPECT_EQ(s.textColors, eqt::ui::defaultChatTextColors());
    EXPECT_EQ(clickAt(s, OL::controlRect(layout800(), controlIndex("OCP_UserColorDefault"))),
              OL::Setting::None) << "a reset that moves nothing reports nothing";

    // The VScroll strip: lower half pages down, upper half back up.
    const auto list = OL::controlRect(layout800(), controlIndex("OCP_ColorChooseList"));
    const int x = list.LowerRightCorner.X - 4;
    click(s, x, list.LowerRightCorner.Y - 4);
    EXPECT_GT(s.colorListTop, 1);
    for (int k = 0; k < 10; ++k) click(s, x, list.LowerRightCorner.Y - 4);
    const int rows = OL::listVisibleRows(layout800(), controlIndex("OCP_ColorChooseList"));
    EXPECT_EQ(s.colorListTop, eqt::ui::kUserColorCount - rows + 1) << "clamped at the last page";
    click(s, x, list.UpperLeftCorner.Y + OL::LIST_HEADER_H + 2);
    EXPECT_LT(s.colorListTop, eqt::ui::kUserColorCount - rows + 1);
}

// ===========================================================================
// Persistence and the slash command
// ===========================================================================

TEST(OptionsColors, TheTablePersistsUnderTheOracleKeysAndReloads) {
    OptionsPanelState s = colorsPage();
    s.textColors[1] = rgb(0, 240, 0);
    UiPreferencesBinding b;
    b.options = &s;
    const std::string text = SerializeUiPreferences(CollectUiPreferences(b, 1));
    EXPECT_NE(text.find("[TextColors]\n"), std::string::npos);
    EXPECT_NE(text.find("User_2_Red=0\nUser_2_Green=240\nUser_2_Blue=0\n"), std::string::npos);
    EXPECT_NE(text.find("User_86_Red=255\nUser_86_Green=155\nUser_86_Blue=155\n"), std::string::npos);
    const OptionsPanelState back = roundTrip(s);
    EXPECT_EQ(back.textColors, s.textColors);
}

TEST(OptionsColors, AnOfficialTextColorsSectionLoadsAndJunkKeepsTheDefault) {
    const std::string official =
        "[TextColors]\r\nUser_1_Red=10\r\nUser_1_Green=20\r\nUser_1_Blue=30\r\n"
        "User_3_Red=300\r\nUser_87_Red=1\r\nUser_x_Red=1\r\nUser_4_Pink=1\r\nUser_5_Blue=abc\r\n";
    const UiPreferences p = ParseUiPreferences(official);
    EXPECT_EQ(p.textColors[0], rgb(10, 20, 30));
    EXPECT_EQ(p.textColors[2], eqt::ui::defaultChatTextColors()[2]) << "300 is off the scale";
    EXPECT_EQ(p.textColors[3], eqt::ui::defaultChatTextColors()[3]);
    EXPECT_EQ(p.textColors[4], eqt::ui::defaultChatTextColors()[4]);
}

TEST(OptionsColors, SlashOptionTextcolorSetsPrintsAndResets) {
    OptionsPanelState s;
    auto r = RunOptionsCommand(s, "/option", "textcolor tell 1 2 3");
    ASSERT_EQ(r.changed.size(), 1u);
    EXPECT_EQ(r.changed[0], OL::Setting::TextColors);
    EXPECT_EQ(s.textColors[1], rgb(1, 2, 3));
    ASSERT_EQ(r.lines.size(), 1u);
    EXPECT_EQ(r.lines[0], "textcolor 2 (Tell) = 1 2 3");

    r = RunOptionsCommand(s, "/option", "textcolor you hit other 4 5 6");
    EXPECT_EQ(s.textColors[9], rgb(4, 5, 6));
    r = RunOptionsCommand(s, "/option", "textcolor 10");
    EXPECT_EQ(r.lines[0], "textcolor 10 (You hit other) = 4 5 6");
    EXPECT_TRUE(r.changed.empty());

    r = RunOptionsCommand(s, "/option", "textcolor tell 1 2 999");
    EXPECT_EQ(s.textColors[1], rgb(1, 2, 3)) << "a value off 0..255 is refused";
    EXPECT_NE(r.lines[0].find("Usage"), std::string::npos);
    r = RunOptionsCommand(s, "/option", "textcolor 87");
    EXPECT_NE(r.lines[0].find("Usage"), std::string::npos);

    r = RunOptionsCommand(s, "/option", "textcolor reset");
    ASSERT_EQ(r.changed.size(), 1u);
    EXPECT_EQ(r.changed[0], OL::Setting::TextColorReset);
    EXPECT_EQ(s.textColors, eqt::ui::defaultChatTextColors());
}

// ===========================================================================
// The pixels: a chat line drawn in the row's colour
// ===========================================================================

namespace {

const char* kWilleqEqe1 = "/mnt/projects/EverQuest_ETC1/willeq.eqe1";

class ChatColorPixelTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    EQE1Loader loader_;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    std::unique_ptr<UIAtlas> atlas_;
    TextBatch textBatch_;
    UILayout layout_;
    UIRenderer ui_;

    void SetUp() override {
        std::ifstream probe(kWilleqEqe1);
        if (!probe.good()) GTEST_SKIP() << "willeq.eqe1 not present";
        if (!loader_.open(kWilleqEqe1)) GTEST_SKIP() << "failed to open willeq.eqe1";
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(800, 600);
        params.Fullscreen = true;
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_OGLES2 device";
        driver_ = device_->getVideoDriver();
        textBatch_.init(device_->getGUIEnvironment());
        layout_.computeLayout(800, 600);
        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        cache_->addEQE1Loader(&loader_);
        atlas_ = std::make_unique<UIAtlas>();
        if (!atlas_->load(driver_, cache_.get())) GTEST_SKIP() << "UIAtlas::load() failed";
        ui_.init(driver_, atlas_.get(), &textBatch_);
    }

    void TearDown() override {
        atlas_.reset();
        cache_.reset();
        if (device_) device_->drop();
    }

    // Pixels of exactly `c` inside the chat message area, after drawing `chat`.
    int countColor(ChatPanelState& chat, const UserColor& c) {
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        driver_->endScene();
        for (int frame = 0; frame < 2; ++frame) {
            driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
            ui_.beginFrame();
            renderChatPanel(ui_, layout_, chat);
            ui_.endFrame();
            driver_->endScene();
        }
        irr::video::IImage* img = driver_->createScreenShot();
        if (!img) return -1;
        int n = 0;
        const auto& a = layout_.chatMessages;
        for (int y = a.UpperLeftCorner.Y; y < a.LowerRightCorner.Y; ++y) {
            for (int x = a.UpperLeftCorner.X; x < a.LowerRightCorner.X; ++x) {
                const irr::video::SColor p = img->getPixel(x, y);
                if (p.getRed() == c.r && p.getGreen() == c.g && p.getBlue() == c.b) ++n;
            }
        }
        img->drop();
        return n;
    }
};

}  // namespace

TEST_F(ChatColorPixelTest, ATellLineIsDrawnInTheTellRowAndFollowsAChange) {
    eqt::ui::ChatMessageBuffer buffer(50);
    eqt::ui::ChatMessage m;
    m.channel = ChatChannel::Tell;
    m.sender = "Oracle";
    m.text = "WWWWWWWWWWWWWWWW MMMMMMMMMMMMMMMM";
    buffer.addMessage(m);

    OptionsPanelState options;
    ChatPanelState chat;
    chat.messageBuffer = &buffer;
    chat.textColors = &options.textColors;

    const UserColor tellDefault = rgb(190, 40, 190);
    const UserColor chosen = rgb(0, 240, 0);  // CPW_TC1Button
    EXPECT_GT(countColor(chat, tellDefault), 50) << "the default Tell row";
    EXPECT_EQ(countColor(chat, chosen), 0);

    // The click path: select Tell, Change, the green swatch, Accept.
    options.isOpen = true;
    options.activeTab = static_cast<int>(OL::Tab::Colors);
    clickAt(options, rowRect(options, 2));
    clickAt(options, OL::controlRect(layout800(), controlIndex("OCP_UserColorChange")));
    clickAt(options, ColorPickerLayout::swatchRect(layout800(), 1));
    ASSERT_EQ(clickAt(options, ColorPickerLayout::acceptRect(layout800())), OL::Setting::TextColors);

    EXPECT_GT(countColor(chat, chosen), 50) << "the line already on screen follows the row";
    EXPECT_EQ(countColor(chat, tellDefault), 0);

    // Persisted and reloaded into a fresh table, the line keeps the colour.
    OptionsPanelState reloaded = roundTrip(options);
    chat.textColors = &reloaded.textColors;
    EXPECT_GT(countColor(chat, chosen), 50);
}
