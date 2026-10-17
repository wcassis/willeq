// B1088 (GitLab #408): the loading screen's ONE draw body.
// Header: include/client/graphics/overlays/loading_frame_layout.h.

#include "client/graphics/overlays/loading_frame_layout.h"

#include <algorithm>

namespace EQT {
namespace Graphics {

void drawLoadingFrame(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIFont* font,
                      float progress,
                      const std::wstring& title,
                      const std::wstring& entryText,
                      const std::wstring& stageText) {
    if (!driver) return;

    driver->beginScene(true, true, irr::video::SColor(255, 20, 20, 40));

    irr::core::dimension2d<irr::u32> screenSize = driver->getScreenSize();

    // Progress bar dimensions
    const int barWidth = 400;
    const int barHeight = 30;
    const int barX = (screenSize.Width - barWidth) / 2;
    const int barY = (screenSize.Height / 2) + 20;

    // Colors
    irr::video::SColor bgColor(255, 40, 40, 60);
    irr::video::SColor borderColor(255, 100, 100, 120);
    irr::video::SColor fillColor(255, 80, 120, 200);

    // Draw border
    driver->draw2DRectangle(borderColor,
        irr::core::recti(barX - 2, barY - 2, barX + barWidth + 2, barY + barHeight + 2));

    // Draw background
    driver->draw2DRectangle(bgColor,
        irr::core::recti(barX, barY, barX + barWidth, barY + barHeight));

    // Draw progress fill
    float clamped = std::max(0.0f, std::min(1.0f, progress));
    int fillWidth = static_cast<int>(barWidth * clamped);
    if (fillWidth > 0) {
        driver->draw2DRectangle(fillColor,
            irr::core::recti(barX, barY, barX + fillWidth, barY + barHeight));
    }

    if (font) {
        // Title above progress bar
        irr::core::dimension2d<irr::u32> titleSize = font->getDimension(title.c_str());
        int titleX = (screenSize.Width - titleSize.Width) / 2;
        int titleY = barY - 40;
        font->draw(title.c_str(),
            irr::core::recti(titleX, titleY, titleX + titleSize.Width, titleY + titleSize.Height),
            irr::video::SColor(255, 255, 255, 255));

        // B1088 (#408): the zone-name line ("Entering <long name>..."),
        // centred between the title and the bar. Its bottom (barY - 11) stays
        // clear of the bar's top border (barY - 2); with the standard 13 px
        // built-in font its top (barY - 24) is clear of the title's bottom
        // (barY - 27). NOT drawn when empty -- an unknown zone shows no line
        // rather than a raw short name or "Zone <id>".
        if (!entryText.empty()) {
            irr::core::dimension2d<irr::u32> entrySize = font->getDimension(entryText.c_str());
            int entryX = (screenSize.Width - entrySize.Width) / 2;
            int entryY = barY - 24;
            font->draw(entryText.c_str(),
                irr::core::recti(entryX, entryY, entryX + entrySize.Width, entryY + entrySize.Height),
                irr::video::SColor(255, 255, 255, 255));
        }

        // Stage text below progress bar
        irr::core::dimension2d<irr::u32> stageSize = font->getDimension(stageText.c_str());
        int stageX = (screenSize.Width - stageSize.Width) / 2;
        int stageY = barY + barHeight + 10;
        font->draw(stageText.c_str(),
            irr::core::recti(stageX, stageY, stageX + stageSize.Width, stageY + stageSize.Height),
            irr::video::SColor(255, 200, 200, 200));

        // Percentage centered in progress bar
        std::wstring pctText = std::to_wstring(static_cast<int>(clamped * 100)) + L"%";
        irr::core::dimension2d<irr::u32> pctSize = font->getDimension(pctText.c_str());
        int pctX = (screenSize.Width - pctSize.Width) / 2;
        int pctY = barY + (barHeight - pctSize.Height) / 2;
        font->draw(pctText.c_str(),
            irr::core::recti(pctX, pctY, pctX + pctSize.Width, pctY + pctSize.Height),
            irr::video::SColor(255, 255, 255, 255));
    }

    driver->endScene();
}

}  // namespace Graphics
}  // namespace EQT
