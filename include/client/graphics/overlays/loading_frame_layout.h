#pragma once

// B1088 (GitLab #408): the loading screen's ONE layout.
//
// Two draw bodies used to hold the same layout (LoadingThread::drawLoadingFrame,
// drawn by the render thread during the 0-50 % network phase, and
// OverlaysComponent::drawLoadingScreen, drawn by the loading thread during
// loadZoneSequential and by FrameComponent while loadingScreenVisible). This
// helper is the single body both go through -- same bar, same title, and
// B1088's zone-name line centred between the title and the bar -- so the two
// screens cannot drift apart.

#include <irrlicht.h>
#include <string>

namespace EQT {
namespace Graphics {

/**
 * Render one loading-screen frame: progress bar, the fixed title above it,
 * the zone-name line (`entryText`, e.g. "Entering North Qeynos...") centred
 * below the title and above the bar (NOT drawn when empty), the stage text
 * below the bar, and the percentage inside the bar.
 *
 * Layout (from the shared barY):
 *   title      barY - 40  (as before)
 *   entry line barY - 24  (B1088: below the title's bottom at the standard
 *                          1024x768 gate size, above the bar's top border;
 *                          the built-in font is 13 px)
 *   bar        barY       (as before)
 *   stage text barY + barHeight + 10  (as before)
 */
void drawLoadingFrame(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIFont* font,
                      float progress,
                      const std::wstring& title,
                      const std::wstring& entryText,
                      const std::wstring& stageText);

}  // namespace Graphics
}  // namespace EQT
