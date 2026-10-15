// B1025-H2: compare the production rect functions to stock Titanium XML.
#include <gtest/gtest.h>

#include "client/graphics/ui/hotbar_layout.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {
namespace HL = EQT::Graphics::HotbarLayout;
using Rect = HL::Rect;

const std::filesystem::path kXml =
    "/mnt/projects/EverQuestP1999/uifiles/default/EQUI_HotButtonWnd.xml";

std::string readXml() {
    std::ifstream in(kXml);
    std::stringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string element(const std::string& xml, const std::string& name) {
    const size_t at = xml.find("\"" + name + "\"");
    if (at == std::string::npos) return {};
    const size_t open = xml.rfind('<', at);
    const size_t tagEnd = xml.find_first_of(" \t\r\n>", open + 1);
    if (open == std::string::npos || tagEnd == std::string::npos) return {};
    const std::string close = "</" + xml.substr(open + 1, tagEnd - open - 1) + ">";
    const size_t end = xml.find(close, at);
    return end == std::string::npos ? std::string() : xml.substr(at, end - at);
}

int value(const std::string& body, const std::string& tag) {
    const std::string open = "<" + tag + ">";
    const size_t at = body.find(open);
    if (at == std::string::npos) return -99999;
    return std::stoi(body.substr(at + open.size()));
}

void expectRect(const Rect& got, const std::string& body, int originX = 0,
                int originY = 0) {
    ASSERT_FALSE(body.empty());
    EXPECT_EQ(got.UpperLeftCorner.X, originX + value(body, "X"));
    EXPECT_EQ(got.UpperLeftCorner.Y, originY + value(body, "Y"));
    EXPECT_EQ(got.getWidth(), value(body, "CX"));
    EXPECT_EQ(got.getHeight(), value(body, "CY"));
}

TEST(HotbarLayout, StockTitaniumRects) {
    if (!std::filesystem::exists(kXml)) GTEST_SKIP() << kXml;
    const auto xml = readXml();
    const auto window = HL::windowRect();
    expectRect(window, element(xml, "HotButtonWnd"));
    expectRect(HL::pageLeftRect(window), element(xml, "HB_PageLeftButton"),
               window.UpperLeftCorner.X, window.UpperLeftCorner.Y);
    expectRect(HL::pageLabelRect(window), element(xml, "HB_CurrentPageLabel"),
               window.UpperLeftCorner.X, window.UpperLeftCorner.Y);
    expectRect(HL::pageRightRect(window), element(xml, "HB_PageRightButton"),
               window.UpperLeftCorner.X, window.UpperLeftCorner.Y);

    for (int i = 0; i < HL::kSlotCount; ++i) {
        const auto r = HL::slotRect(window, i);
        for (const char* prefix : {"HB_Button", "HB_InvSlot", "HB_SpellGem"}) {
            expectRect(r, element(xml, std::string(prefix) + std::to_string(i + 1)),
                       window.UpperLeftCorner.X, window.UpperLeftCorner.Y);
        }
    }
    for (int i = 2; i <= 4; ++i) {
        const auto hidden = element(xml, "HotButtonWnd" + std::to_string(i));
        ASSERT_FALSE(hidden.empty());
        EXPECT_EQ(value(hidden, "CX"), 0);
        EXPECT_EQ(value(hidden, "CY"), 0);
    }
    EXPECT_EQ(HL::slotRect(window, -1).getWidth(), 0);
    EXPECT_EQ(HL::slotRect(window, HL::kSlotCount).getWidth(), 0);
}

} // namespace
